// ============================================================================
//  doppler-sip - tiny Pexip Pulse + PJSIP video-call demo
// ----------------------------------------------------------------------------
//
//  The companion to src/main.cpp from the parent project. That one drives
//  Pulse's *built-in* REST + signalling path (pulse_connect_with_rest_async).
//  This one instead uses Pulse purely as a media engine:
//
//      1. pulse_new_external_rest() + the usual options/callback registration.
//         (external-rest mode tells Pulse "the application owns signalling",
//         which is exactly what we want when PJSIP is driving SIP.)
//      2. pulse_setup_stage_1_from_structure(is_sip=true)
//                          -> Pulse hands us a local SDP offer.
//      3. We give that offer to PJSIP, which sends a SIP INVITE.
//      4. PJSIP receives 200 OK; we feed the remote SDP + the dialog's
//         Call-ID back into pulse_setup_stage_2_from_structure().
//      5. Media flows through Pulse exactly as in the REST demo.
//      6. Hang-up -> SIP BYE via PJSIP, then pulse_disconnect_async().
//      7. pulse_free().
//
//  Everything else is just Dear ImGui plumbing, copied from src/main.cpp.
// ============================================================================

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <pexpulse/pulse.h>
#include <pexpulse/pulse_data_session.h>

#include "app_transport.h"
#include "sip_ua.h"

// Forward declarations - the texture helpers are defined further down,
// but lazy_pulse_init() (just above start_call) needs to reach them.
struct GLTextureContext;
static void attach_video_data_session(Pulse * pulse, GLTextureContext * ctx);

// ----------------------------------------------------------------------------
//  Application state
// ----------------------------------------------------------------------------
//
//  Same general shape as the REST demo: a single struct shared between the
//  UI thread, Pulse's internal worker threads, and PJSIP's worker thread.
//  Atomics for the polled flags, a mutex for the free-form status strings.
// ----------------------------------------------------------------------------

enum class CallStage {
    Idle = 0,
    Stage1Done,    // Pulse gave us an offer, INVITE sent, waiting for 200 OK.
    Stage2Done,    // SIP answer in, Pulse media engaged.
};

// ---------------------------------------------------------------------------
// STAGED BRING-UP: AppTransport bridge (decided at first Call)
// ---------------------------------------------------------------------------
// The architecture is: Pulse hands its RTP/RTCP packets to the
// `AppTransport` via the pulse_options_set_app_transport() callback API,
// and we relay them over UDP sockets we own ourselves (one per m=/RTCP
// wire). The same sockets receive the peer's RTP/RTCP and feed it back
// into Pulse via pulse_app_transport_push().
//
// IMPORTANT - the bind must happen on a freshly-created Pulse handle,
// before the first pulse_setup_stage_1_from_structure() /
// _from_response_buffer(). That is the named barrier in
// pulse_options.h: while session status is UNINITIALIZED the call is
// accepted; after stage 1 it returns PULSE_ERROR_ALREADY_CONNECTED.
// The connect_default_devices / data_session_connect_output /
// device_session_connect_* calls do NOT advance the status, so they're
// fine to run between the bind and stage 1.
//
// We do NOTHING to Pulse before the operator presses "Call": no
// pulse_new_external_rest(), no bind, no callback registration, no
// device or data-session connects. The checkbox below is a pure intent
// flag while in that "idle" state, so toggling it has zero effect on
// Pulse. On the first Call press, `lazy_pulse_init()` walks the whole
// startup sequence in one shot - including the AppTransport bind, iff
// the checkbox was still ticked at that moment - and then issues stage
// 1. After that, Pulse's session status has advanced and Pulse rejects
// any further (un)bind, so the checkbox locks for the rest of the
// process. `DOPPLER_SIP_BRIDGE` only seeds the initial checkbox state.

struct AppState
{
    Pulse *                  pulse     = nullptr;
    doppler::SipUA *         sip       = nullptr;
    // Non-owning pointer to the AppTransport instance. Populated by
    // lazy_pulse_init() on the first Call press iff `bridge_enabled`
    // was true at that moment AND `bind_to_pulse()` succeeded. Stays
    // nullptr otherwise. Every `if (app.transport)` site below short-
    // circuits in that case, routing media through Pulse-owned sockets.
    doppler::AppTransport *  transport = nullptr;

    // Bridge state. `bridge_enabled` is the operator's intent, seeded
    // from DOPPLER_SIP_BRIDGE at startup (default = true) and freely
    // togglable via the UI checkbox until the first Call has been
    // placed - i.e. until `app.pulse` becomes non-null. After that the
    // checkbox locks, because Pulse will reject any further (un)bind.
    // `bridge_bound` mirrors whether pulse_options_set_app_transport()
    // currently has our callback registered with Pulse; only flips to
    // true inside lazy_pulse_init(). Atomics keep the UI-thread writes
    // tidy against the UI-thread reads in the render loop.
    std::atomic<bool>        bridge_enabled{false};
    std::atomic<bool>        bridge_bound{false};
    // Storage for the AppTransport instance lives in main(); we keep a
    // non-owning pointer here so call-handling code can reach it (e.g.
    // configure_local_offer / configure_remote_answer).
    doppler::AppTransport *  transport_storage = nullptr;

    // Non-owning pointers to the GL texture contexts main() owns. We
    // need them inside lazy_pulse_init() to wire up Pulse data-session
    // outputs once - and only once - the operator presses Call.
    GLTextureContext *       remote_ctx_storage   = nullptr;
    GLTextureContext *       selfview_ctx_storage = nullptr;

    // The single input the demo needs: a SIP URI to call.
    char sip_uri[256]      = "";          // e.g. "havard@pexipdemo.com"
    char display_name[128] = "Doppler SIP demo";

    std::atomic<int>  connection_status{PULSE_CONNECTION_STATUS_DISCONNECTED};
    std::atomic<int>  last_async_error{PULSE_SUCCESS};
    std::atomic<int>  stage{static_cast<int>(CallStage::Idle)};

    // Set whenever something (the UI Hang-up button, or a SIP-side end /
    // failure callback) wants Pulse fully torn down - i.e. pulse_free()'d,
    // not just pulse_disconnect()'d. The main loop drains this flag every
    // frame and runs destroy_pulse_now() on the UI thread, which is the
    // only thread where the GL context is current and so the only place
    // it's safe to release the Pulse-owned video data sessions.
    std::atomic<bool> destroy_pulse_pending{false};

    std::mutex   text_mutex;
    std::string  status_text = "Idle. Enter a SIP URI and press Call.";
    std::string  progress_text;
};

static void set_status(AppState & app, std::string text)
{
    std::lock_guard<std::mutex> lock(app.text_mutex);
    app.status_text = std::move(text);
}
static void set_progress(AppState & app, std::string text)
{
    std::lock_guard<std::mutex> lock(app.text_mutex);
    app.progress_text = std::move(text);
}

static const char * status_to_string(int status)
{
    switch (status) {
        case PULSE_CONNECTION_STATUS_DISCONNECTED:  return "Disconnected";
        case PULSE_CONNECTION_STATUS_CONNECTING:    return "Connecting...";
        case PULSE_CONNECTION_STATUS_RECONNECTING:  return "Reconnecting...";
        case PULSE_CONNECTION_STATUS_CONNECTED:     return "Connected";
        case PULSE_CONNECTION_STATUS_DISCONNECTING: return "Disconnecting...";
        default:                                    return "Unknown";
    }
}

// ----------------------------------------------------------------------------
//  Pulse callbacks (identical to src/main.cpp - kept verbatim on purpose so
//  it's obvious the only thing that changes between the two demos is the
//  connect/setup path).
// ----------------------------------------------------------------------------

static void on_conference_status(const PulseConferenceStatusInfo * info, void * user_context)
{
    auto * app = static_cast<AppState *>(user_context);
    app->connection_status.store(static_cast<int>(info->status));
    set_status(*app, std::string("Conference status: ") + status_to_string(info->status));
}

static void on_async_result(const PulseError err, void * user_context)
{
    auto * app = static_cast<AppState *>(user_context);
    app->last_async_error.store(static_cast<int>(err));
    if (err == PULSE_SUCCESS) {
        set_status(*app, "Async operation completed successfully.");
    } else {
        set_status(*app, std::string("Async operation failed: ") + pulse_strerror(err));
    }
    set_progress(*app, "");
}

static void on_progress(const PulseOperationProgressInfo * info, void * user_context)
{
    auto * app = static_cast<AppState *>(user_context);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "[%3d%%] %s",
                  static_cast<int>(info->progress * 100.0f),
                  info->desc ? info->desc : "");
    set_progress(*app, buf);
}

static void on_pulse_log(void * /*user_context*/, PulseDebugLevel level,
                         const char * category, int64_t /*wall_time_us*/,
                         int64_t /*elapsed_nano*/, unsigned int /*pid*/,
                         const char * /*file*/, const char * /*function*/,
                         int /*line*/, const char * /*object_debug_str*/,
                         const char * message)
{
    //if (level > PULSE_LEVEL_WARNING) return;
    std::fprintf(stderr, "[pulse:%s] %s\n",
                 category ? category : "?", message ? message : "");
}

// Fired by Pulse (external-rest mode) when its local SDP changes after
// stage 2 — e.g. it added a content channel, or renegotiated codecs. A
// real client would forward this as a SIP re-INVITE via PJSIP; for this
// demo we just surface it in the status line so the user can see it
// happen.
static void on_pulse_update_sdp(void * user_context, const char * update_sdp)
{
    auto * app = static_cast<AppState *>(user_context);
    const size_t len = update_sdp ? std::strlen(update_sdp) : 0;
    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "Pulse requested SDP update (%zu bytes) - re-INVITE not implemented in demo.",
                  len);
    set_status(*app, buf);
}

// ----------------------------------------------------------------------------
//  Pulse setup (same device bindings as the REST demo).
// ----------------------------------------------------------------------------

static void install_callbacks(AppState & app)
{
    PulseConferenceStatusCallbackConfig conf_cb{ on_conference_status, &app };
    pulse_options_set_conference_state_callback(app.pulse, &conf_cb);
    pulse_options_set_application_user_agent_string(app.pulse, "doppler-sip/0.1");
}

static void connect_default_devices(AppState & app)
{
    struct Binding {
        const char *        name;
        PulseMediaType      type;
        PulseMediaDirection direction;
    };
    const Binding bindings[] = {
        { "camera",     PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT  },
        { "microphone", PULSE_MEDIA_AUDIO, PULSE_MEDIA_INPUT  },
        { "speaker",    PULSE_MEDIA_AUDIO, PULSE_MEDIA_OUTPUT },
    };
    for (const Binding & b : bindings) {
        PulseError err = pulse_device_session_connect_system_default(
            app.pulse, PULSE_MEDIA_CONTENT_MAIN, b.type, b.direction);
        if (err != PULSE_SUCCESS) {
            std::fprintf(stderr, "Failed to attach default %s: %s\n",
                         b.name, pulse_strerror(err));
        }
    }
}

// Mirror of connect_default_devices(): release the camera / mic / speaker
// device sessions that were attached on the way up. pulse_free() would
// also tear these down, but doing it explicitly here keeps the
// setup/teardown narrative symmetric (one named step in lazy_pulse_init
// gets one named step in destroy_pulse_now), the same pattern
// setup_pulse_callbacks/clear_pulse_callbacks use in pexninja.cpp.
//
// The two disconnect entry points are deliberately less granular than
// the connect side: pulse_device_session_disconnect_main_audio() drops
// both microphone and speaker in one call, and the main-video disconnect
// takes only a direction (we only ever connect MAIN/VIDEO/INPUT, so we
// only undo that one).
static void disconnect_default_devices(AppState & app)
{
    pulse_device_session_disconnect_main_video(
        app.pulse, PULSE_MEDIA_CONTENT_MAIN, PULSE_MEDIA_INPUT);
    pulse_device_session_disconnect_main_audio(app.pulse);
}

static void uninstall_callbacks(AppState & app)
{
    pulse_options_set_conference_state_callback(app.pulse, nullptr);
}

// ----------------------------------------------------------------------------
//  The SIP-driven call flow
// ----------------------------------------------------------------------------

// Stage 2 of Pulse setup, fired from the SIP worker thread once we have
// the 200 OK / answer SDP.
static void on_sip_answer(AppState & app, const doppler::SipAnswer & ans)
{
    // Hand the answer's IP + port table to the app-transport bridge so its
    // outbound callback knows where to sendto(). Must happen before
    // pulse_setup_stage_2_from_structure() returns, because Pulse will
    // start invoking the outbound callback as soon as stage 2 succeeds.
    if (app.transport) {
        std::string err = app.transport->configure_remote_answer(ans.remote_sdp);
        if (!err.empty()) {
            set_status(app, std::string("app-transport: ") + err);
            return;
        }
    }

    // The Call-ID is what we (for now) feed into Pulse's call_uuid slot —
    // see the README for the open question on this.
    PulseSetupStage2Config cfg{};
    cfg.call_uuid  = ans.call_id.c_str();
    cfg.remote_sdp = ans.remote_sdp.c_str();

    PulseError err = pulse_setup_stage_2_from_structure(app.pulse, &cfg);
    if (err != PULSE_SUCCESS) {
        set_status(app, std::string("pulse_setup_stage_2_from_structure failed: ")
                            + pulse_strerror(err));
        return;
    }
    app.stage.store(static_cast<int>(CallStage::Stage2Done));
    set_status(app, "SIP 200 OK received - Pulse media engaged.");
}

static void on_sip_failure(AppState & app, const std::string & reason)
{
    set_status(app, std::string("SIP call failed: ") + reason);
    set_progress(app, "");
    // Pulse is in the middle of stage-1; ask the UI thread to tear it
    // fully down (pulse_free + drop transport + uninstall callbacks) so
    // the next Call starts from a clean slate. We can't do it here -
    // this fires on PJSIP's worker thread and the GL context is owned
    // by the UI thread.
    app.destroy_pulse_pending.store(true);
    app.stage.store(static_cast<int>(CallStage::Idle));
}

static void on_sip_ended(AppState & app, const std::string & reason)
{
    set_status(app, std::string("SIP call ended: ") + reason);
    set_progress(app, "");
    // Same deal as on_sip_failure: walk Pulse all the way back to
    // "never created" rather than just disconnecting, so the operator's
    // bridge checkbox unlocks and the next Call rebuilds Pulse fresh.
    app.destroy_pulse_pending.store(true);
    app.stage.store(static_cast<int>(CallStage::Idle));
}

// One-shot Pulse bring-up. Called from start_call() the first time the
// operator presses Call (idempotent: re-entry is a no-op once `app.pulse`
// is set). Bundles together every Pulse-touching action we used to do
// eagerly at startup: handle creation, AppTransport bind (iff the
// checkbox is still ticked), callback registration, native-window
// suppression, default device attachment, video data-session attach.
//
// Doing it here, just before stage 1, means the operator's checkbox
// state is the one Pulse actually sees - there is no longer any window
// in which Pulse can wire up sessions against an app-transport callback
// the operator hadn't yet had a chance to refuse. After stage 1 runs
// (just below this in start_call), Pulse's session status advances out
// of UNINITIALIZED and any further (un)bind would be rejected; that's
// what locks the checkbox for the remainder of the process.
//
// Returns true on success; false if pulse_new_external_rest() failed,
// in which case the caller must abort the call attempt.
static bool lazy_pulse_init(AppState & app)
{
    if (app.pulse) return true;     // already initialised on a prior Call

    // The numbered steps below are mirrored, in reverse, by the matching
    // steps in destroy_pulse_now(). Lesson from pexninja.cpp: every
    // named setup step earns a named teardown step. Keep them in sync
    // whenever you add or remove one.

    // 1. pulse_new_external_rest(): PJSIP owns SIP signalling, Pulse is
    //    purely a media engine. The update_sdp callback fires if Pulse
    //    later wants to renegotiate (e.g. add a content stream); we
    //    register it against `&app` so it can post into the UI status
    //    line.
    PulseExternalRestCallbackConfig ext_rest_cfg{};
    ext_rest_cfg.update_sdp_callback     = on_pulse_update_sdp;
    ext_rest_cfg.update_sdp_user_context = &app;
    app.pulse = pulse_new_external_rest(ext_rest_cfg);
    if (!app.pulse) {
        set_status(app, "pulse_new_external_rest() returned NULL");
        return false;
    }

    // 2. AppTransport bind. The checkbox value here is whatever the
    //    operator last left it at - if they unchecked it before pressing
    //    Call, we skip the bind entirely and Pulse never even sees our
    //    callback.
    if (app.bridge_enabled.load() && app.transport_storage) {
        PulseError xport_err = app.transport_storage->bind_to_pulse(app.pulse);
        if (xport_err != PULSE_SUCCESS) {
            std::fprintf(stderr,
                "pulse_options_set_app_transport failed: %s "
                "- continuing with Pulse-owned sockets.\n",
                pulse_strerror(xport_err));
            app.bridge_enabled.store(false);
        } else {
            app.transport = app.transport_storage;
            app.bridge_bound.store(true);
        }
    }

    // 3. install_callbacks(): conference-state callback + UA string.
    install_callbacks(app);

    // 4. We render the video ourselves below (see GLTextureContext) by
    //    pulling RGBA frames out of Pulse via the data-session API. Tell
    //    Pulse NOT to also spawn its own native windows for self-view,
    //    the far end or presentation - these have to be cleared BEFORE
    //    the first connect (i.e. before stage_1), otherwise Pulse pops
    //    them up the moment media starts flowing. Mirrors src/main.cpp.
    pulse_options_set_self_view_window_handle         (app.pulse, nullptr);
    pulse_options_set_remote_video_window_handle      (app.pulse, nullptr);
    pulse_options_set_presentation_video_window_handle(app.pulse, nullptr);

    // 5. connect_default_devices(): camera / microphone / speaker.
    connect_default_devices(app);

    // 6. attach_video_data_session(): open the RGBA data-session
    //    outputs for the streams the UI tiles already have GL textures
    //    for (MAIN = incoming far-end video, SELFVIEW = our own camera
    //    feed). Pulse starts feeding frames as soon as media exists.
    if (app.remote_ctx_storage)
        attach_video_data_session(app.pulse, app.remote_ctx_storage);
    if (app.selfview_ctx_storage)
        attach_video_data_session(app.pulse, app.selfview_ctx_storage);

    return true;
}

// "Call" button handler. UI thread only.
static void start_call(AppState & app)
{
    if (app.sip_uri[0] == '\0') {
        set_status(app, "Please enter a SIP URI.");
        return;
    }
    if (app.stage.load() != static_cast<int>(CallStage::Idle)) {
        set_status(app, "A call is already in progress.");
        return;
    }

    // Lazy one-shot Pulse bring-up. Until this runs - i.e. for the
    // entire pre-Call lifetime of the process - we have done nothing
    // to Pulse at all: no handle, no bind, no callbacks, no devices,
    // no data sessions. This is what makes the AppTransport checkbox
    // meaningful: its value at the moment of this call is the value
    // Pulse will see.
    if (!lazy_pulse_init(app)) {
        return;
    }

    set_status(app, "Asking Pulse for a SIP-mode SDP offer...");

    // ---- Stage 1: get a local SDP offer from Pulse ---------------------
    // The AppTransport bind (if any) was made just above inside
    // lazy_pulse_init(), still in UNINITIALIZED status. Stage 1 is the
    // named barrier in pulse_options.h: after this call Pulse rejects
    // any (un)bind with PULSE_ERROR_ALREADY_CONNECTED, which is what
    // locks the UI checkbox for the rest of the process.
    PulseSetupStage1Config cfg{};
    cfg.is_sip              = true;
    cfg.disable_trickle_ice = true;   // simpler exchange for direct SIP
    cfg.stun_config         = nullptr;
    cfg.turn_config         = nullptr;

    const char * local_sdp_ptr = nullptr;
    PulseError err = pulse_setup_stage_1_from_structure(app.pulse, &cfg,
                                                        &local_sdp_ptr);
    if (err != PULSE_SUCCESS || !local_sdp_ptr) {
        set_status(app, std::string("pulse_setup_stage_1_from_structure failed: ")
                            + pulse_strerror(err));
        return;
    }
    // Copy the SDP out immediately — Pulse owns the underlying buffer.
    std::string local_sdp = local_sdp_ptr;

    // Replace the m=/c=/a=rtcp ports in Pulse's offer with the ports of
    // the UDP sockets the AppTransport owns, so the SIP peer sends RTP to
    // *us* and we relay it back into Pulse via pulse_app_transport_push().
    //
    // Only runs when the bridge was opted into (checkbox ticked at the
    // moment Call was pressed) AND bind_to_pulse() succeeded inside
    // lazy_pulse_init(). Otherwise app.transport is nullptr and Pulse's
    // SDP is forwarded to PJSIP unmodified, with Pulse's own UDP sockets
    // advertised in the INVITE.
    if (app.transport) {
        std::string xport_err;
        std::string rewritten = app.transport->configure_local_offer(local_sdp, xport_err);
        if (!xport_err.empty()) {
            set_status(app, std::string("app-transport: ") + xport_err);
            app.stage.store(static_cast<int>(CallStage::Idle));
            return;
        }
        local_sdp = std::move(rewritten);
    }

    app.stage.store(static_cast<int>(CallStage::Stage1Done));

    // ---- Send the SIP INVITE ------------------------------------------
    set_status(app, std::string("Calling ") + app.sip_uri + " ...");
    const std::string target = app.sip_uri;
    const std::string from   = app.display_name;
    bool ok = app.sip->place_call(
        target, local_sdp, from,
        [app_ptr = &app](const doppler::SipAnswer & ans) { on_sip_answer(*app_ptr, ans); },
        [app_ptr = &app](const std::string & reason)     { on_sip_failure(*app_ptr, reason); },
        [app_ptr = &app](const std::string & reason)     { on_sip_ended(*app_ptr, reason); });

    if (!ok) {
        // place_call has already invoked on_sip_failure via its callback,
        // which has tidied up Pulse - nothing left to do here.
    }
}

// destroy_pulse_now() lives further down, after the GLTextureContext
// definition (it needs ctx.media_content). Forward-declared here so
// start_hangup() / main loop can refer to it.
static void destroy_pulse_now(AppState &              app,
                              doppler::AppTransport & transport,
                              struct GLTextureContext & remote_ctx,
                              struct GLTextureContext & selfview_ctx);

// "Hang up" button handler. UI thread only.
static void start_hangup(AppState & app)
{
    set_status(app, "Hanging up...");
    // Send SIP BYE (or CANCEL for an early dialog) if we have an
    // outbound dialog. The on_sip_ended callback will eventually fire
    // on PJSIP's worker thread, but we don't depend on that for
    // teardown - we request the full Pulse destruction right here so
    // the operator sees media stop immediately, and so that even if
    // there is no live dialog yet (stage-1 with no answer) Pulse is
    // still torn down cleanly.
    if (app.sip)
        app.sip->hangup();
    // The actual pulse_free() happens on the UI thread inside the
    // main loop's per-frame drain of destroy_pulse_pending; doing it
    // there keeps all GL / Pulse data-session teardown on the thread
    // that owns the GL context.
    app.destroy_pulse_pending.store(true);
}

// ----------------------------------------------------------------------------
//  Video rendering (Pulse data-session -> GL texture -> ImGui::Image)
// ----------------------------------------------------------------------------
//
//  Lifted (with credit) from src/main.cpp's GLTextureContext pipeline,
//  which is itself a slimmed-down copy of pexninja's render_gl_ctx_image.
//
//  We want a visible preview of what Pulse is producing once a SIP call
//  goes through, both so the operator can verify that media really is
//  flowing and so we have a fast feedback loop while debugging the
//  Pulse/PJSIP/AppTransport plumbing. Same recipe as the REST demo:
//
//      1. Tell Pulse NOT to spawn its own native windows for self-view,
//         far-end and presentation (set_*_window_handle(nullptr) BEFORE
//         the first connect, otherwise Pulse will pop them up the moment
//         media flows).
//      2. Open a PulseDataSession output per content slot we want to
//         render, asking for `video/x-raw, format=RGBA` so the frames
//         are already in a format glTexImage2D understands.
//      3. Every ImGui frame, pull the freshest decoded frame
//         (timeout=0 - non-blocking; we'd rather skip a frame than
//         stall the UI) and re-upload it into a GL texture.
//      4. ImGui::Image draws the texture inside a tile.
// ----------------------------------------------------------------------------

struct GLTextureContext
{
    GLuint            texture       = 0;
    PulseMediaContent media_content = PULSE_MEDIA_CONTENT_MAIN;
    int               last_width    = 0;
    int               last_height   = 0;
};

static PulseDataSessionConfig * make_video_data_session_config()
{
    PulseDataSessionConfig * cfg =
        pulse_data_session_config_new(PULSE_DATA_SESSION_VIDEO_FROM_CAPS);
    pulse_data_session_config_video_from_caps(cfg, "video/x-raw, format=RGBA");
    return cfg;
}

// GL-side init for a video tile: create the texture and remember which
// Pulse content slot it will display. Safe to call before any Pulse
// handle exists - the data-session attach is deferred to
// `attach_video_data_session()`, which lazy_pulse_init() runs once the
// operator presses Call.
static void init_video_render_ctx(GLTextureContext & ctx,
                                  PulseMediaContent media_content)
{
    glGenTextures(1, &ctx.texture);
    glBindTexture(GL_TEXTURE_2D, ctx.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ctx.media_content = media_content;
}

// Pulse-side attach: open the RGBA data-session output for this tile's
// content slot. Must run after pulse_new_external_rest() (so we have a
// handle) but before stage 1 is irrelevant - data-session connects do
// not advance Pulse's session status. Driven from lazy_pulse_init().
static void attach_video_data_session(Pulse * pulse, GLTextureContext * ctx)
{
    PulseDataSessionConfig * cfg = make_video_data_session_config();
    pulse_data_session_connect_output(pulse, cfg, ctx->media_content);
    pulse_data_session_config_free(cfg);
}

// Mirror of attach_video_data_session(): close the RGBA output Pulse was
// pushing into this tile's GL texture. Used by destroy_pulse_now() to
// keep the setup/teardown pair symmetric - the GL texture itself is
// left alive, owned by main()'s GLTextureContext, so the next
// lazy_pulse_init() can re-attach to the same texture. Compare with
// shutdown_video_render_ctx() below, which also deletes the texture
// (that one runs at process exit, this one runs on hang-up).
static void detach_video_data_session(Pulse * pulse, GLTextureContext * ctx)
{
    pulse_data_session_disconnect(pulse, PULSE_MEDIA_VIDEO,
                                  PULSE_MEDIA_OUTPUT, ctx->media_content);
}

static void shutdown_video_render_ctx(Pulse * pulse, GLTextureContext & ctx)
{
    // Pulse may be null if the operator quit before ever pressing Call -
    // in which case attach_video_data_session() never ran, so there's
    // nothing to disconnect. The GL texture, on the other hand, was
    // created up front by init_video_render_ctx() so it always needs
    // releasing.
    if (pulse) {
        pulse_data_session_disconnect(pulse, PULSE_MEDIA_VIDEO,
                                      PULSE_MEDIA_OUTPUT, ctx.media_content);
    }
    if (ctx.texture) {
        glDeleteTextures(1, &ctx.texture);
        ctx.texture = 0;
    }
}

// Definition for the forward declaration up near start_hangup(). Lives
// here because it needs the full GLTextureContext type for
// ctx.media_content. See the doc comment on the forward declaration
// for the contract; concretely this fully tears Pulse down (sync
// disconnect -> data-session disconnect -> transport unbind ->
// callbacks off -> pulse_free) and returns AppState to its pre-Call
// shape so the next Call rebuilds via lazy_pulse_init().
//
// CONTRACT: must remain idempotent. Both the UI thread (start_hangup)
// and the PJSIP worker thread (on_sip_ended / on_sip_failure) can
// flip destroy_pulse_pending to true between two consecutive main-loop
// drains, so this helper may be invoked when Pulse is already gone -
// the early `if (!app.pulse) return` is what makes that safe. Don't
// let later additions to this function predicate side effects on
// "we've definitely just torn something down" without first re-
// asserting that invariant.
static void destroy_pulse_now(AppState &              app,
                              doppler::AppTransport & transport,
                              GLTextureContext &      remote_ctx,
                              GLTextureContext &      selfview_ctx)
{
    if (!app.pulse) return;

    // The steps below are the literal reverse of lazy_pulse_init() (look
    // for the matching numbered comments there). Lesson taken from
    // pexninja.cpp's setup_pulse_callbacks / clear_pulse_callbacks pair:
    // every named setup step has a named teardown step in the inverse
    // order, even when pulse_free() would clean it up anyway. Keeps the
    // demo's lifecycle obvious and prevents "the call lingered after
    // hang-up" bugs.

    // Inverse of pulse_setup_stage_1/stage_2 (which run in start_call /
    // on_sip_answer, not in lazy_pulse_init). Sync variant - we're
    // about to pulse_free() anyway, blocking here just moves the wait
    // out of pulse_free(). Skipped if we never reached CONNECTED (e.g.
    // hang-up between stage-1 and the 200 OK).
    if (pulse_is_connected(app.pulse))
        pulse_disconnect(app.pulse, nullptr);

    // 6'. Inverse of attach_video_data_session(): close the RGBA outputs
    //     so Pulse stops trying to deliver frames to buffers it's about
    //     to free. GL textures stay alive for the next call.
    detach_video_data_session(app.pulse, &selfview_ctx);
    detach_video_data_session(app.pulse, &remote_ctx);

    // 5'. Inverse of connect_default_devices(): drop camera / mic /
    //     speaker device sessions.
    disconnect_default_devices(app);

    // 4'. Inverse of the three pulse_options_set_*_window_handle(nullptr)
    //     calls in lazy_pulse_init(). They're already null, so there's
    //     nothing to undo here - the named step exists only as
    //     documentation of the mirror.

    // 3'. Inverse of install_callbacks(): clear the conference-state
    //     callback so Pulse doesn't invoke it during pulse_free().
    uninstall_callbacks(app);

    // 2'. Inverse of transport_storage->bind_to_pulse(): stop the
    //     reader thread, close sockets, clear Pulse's pointer to our
    //     callback so it can't fire on_outbound after we free. Idempotent
    //     - safe even when the bridge was never bound.
    transport.shutdown();

    // 1'. Inverse of pulse_new_external_rest(): release the handle.
    pulse_free(app.pulse);
    app.pulse = nullptr;

    // Mirror back the AppState bookkeeping lazy_pulse_init() set up.
    // transport_storage stays - it's the long-lived AppTransport instance
    // owned by main(); the next lazy_pulse_init() will re-bind it.
    // bridge_bound goes back to false so the UI checkbox unlocks and
    // reflects "(pending - will bind on Call)" again. bridge_enabled
    // (operator's intent) is preserved.
    app.transport = nullptr;
    app.bridge_bound.store(false);
    app.connection_status.store(PULSE_CONNECTION_STATUS_DISCONNECTED);
    app.stage.store(static_cast<int>(CallStage::Idle));
}

// Try to pull a fresh RGBA frame and upload it to the GL texture. No-op if
// nothing is ready yet (we poll with timeout=0 so we never block the UI).
// If the resolution changes mid-call (re-INVITE, layout change, ...) the
// next glTexImage2D will resize the texture automatically.
static void pump_frame_into_texture(Pulse * pulse, GLTextureContext & ctx)
{
    // Before the first Call we have no Pulse handle at all - skip the
    // pull rather than dereference a null and crash.
    if (!pulse) return;

    PulseDataSessionFrameData * frame = nullptr;
    pulse_data_session_pull_frame_data(pulse, PULSE_MEDIA_VIDEO, &frame,
                                       ctx.media_content, 0);
    if (!frame) return;

    int w = 0, h = 0;
    if (pulse_frame_data_get_resolution(frame, &w, &h) && w > 0 && h > 0) {
        glBindTexture(GL_TEXTURE_2D, ctx.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, frame->data);
        ctx.last_width  = w;
        ctx.last_height = h;
    }
    pulse_data_session_frame_data_free(frame);
}

// ----------------------------------------------------------------------------
//  ImGui control panel
// ----------------------------------------------------------------------------

static void draw_ui(AppState & app, GLTextureContext & remote_ctx,
                    GLTextureContext & selfview_ctx)
{
    // Pull the freshest frame from each Pulse output session BEFORE we
    // start drawing - the upload itself is a GL state mutation, so we
    // want it sandwiched between glfwMakeContextCurrent (already done
    // by the GLFW backend before draw_ui) and ImGui::Image. The pull
    // is non-blocking (timeout=0) and harmless if no session has
    // produced a frame yet - the texture just keeps showing whatever
    // it last had.
    pump_frame_into_texture(app.pulse, remote_ctx);
    pump_frame_into_texture(app.pulse, selfview_ctx);

    ImGuiViewport * vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGui::Begin("Doppler - Pexip Pulse + SIP demo", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    ImGui::TextUnformatted("Pexip Pulse video-call demo (signalling via PJSIP)");
    ImGui::Separator();

    ImGui::InputText("SIP URI",      app.sip_uri,      sizeof(app.sip_uri));
    ImGui::InputText("Display name", app.display_name, sizeof(app.display_name));
    ImGui::TextDisabled("e.g. havard@pexipdemo.com");

    ImGui::Spacing();

    const int stage = app.stage.load();
    const bool can_call   = (stage == static_cast<int>(CallStage::Idle));
    const bool can_hangup = (stage != static_cast<int>(CallStage::Idle));

    ImGui::BeginDisabled(!can_call);
    if (ImGui::Button("Call")) start_call(app);
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!can_hangup);
    if (ImGui::Button("Hang up")) start_hangup(app);
    ImGui::EndDisabled();

    // ---- AppTransport bridge toggle -----------------------------------
    // Pure intent flag. Nothing about Pulse happens until the operator
    // presses Call - at that point lazy_pulse_init() reads this value
    // and decides whether to call pulse_options_set_app_transport().
    // The checkbox stays interactive until the first Call (`app.pulse`
    // is still null); after that Pulse's session status has advanced
    // past UNINITIALIZED and any (un)bind would be rejected, so the
    // checkbox locks for the remainder of the process.
    ImGui::SameLine();
    const bool toggle_allowed =
        app.transport_storage != nullptr
        && app.pulse == nullptr;
    bool bridge_enabled_view = app.bridge_enabled.load();
    ImGui::BeginDisabled(!toggle_allowed);
    if (ImGui::Checkbox("AppTransport bridge", &bridge_enabled_view)) {
        app.bridge_enabled.store(bridge_enabled_view);
        set_status(app, bridge_enabled_view
                            ? "AppTransport bridge will engage on next Call."
                            : "AppTransport bridge will stay off on next Call.");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (app.bridge_bound.load()) {
        ImGui::TextDisabled("(bound - locked)");
    } else if (app.pulse != nullptr) {
        // Call has been placed without binding (either checkbox was off,
        // or bind_to_pulse() failed inside lazy_pulse_init()). Either
        // way, no further (un)bind is possible on this Pulse handle.
        ImGui::TextDisabled(bridge_enabled_view
                            ? "(bind failed - see status)"
                            : "(disabled - locked, Call has been placed)");
    } else if (app.transport_storage == nullptr) {
        ImGui::TextDisabled("(unavailable)");
    } else {
        ImGui::TextDisabled(bridge_enabled_view
                            ? "(pending - will bind on Call)"
                            : "(disabled)");
    }

    ImGui::Separator();
    ImGui::Text("Pulse state: %s", status_to_string(app.connection_status.load()));
    const char * stage_str = "Idle";
    switch (static_cast<CallStage>(stage)) {
        case CallStage::Idle:        stage_str = "Idle";                    break;
        case CallStage::Stage1Done:  stage_str = "INVITE sent (waiting 200)"; break;
        case CallStage::Stage2Done:  stage_str = "In call";                  break;
    }
    ImGui::Text("SIP stage:   %s", stage_str);

    std::string status_text, progress_text;
    {
        std::lock_guard<std::mutex> lock(app.text_mutex);
        status_text   = app.status_text;
        progress_text = app.progress_text;
    }
    ImGui::TextWrapped("%s", status_text.c_str());
    if (!progress_text.empty())
        ImGui::TextWrapped("%s", progress_text.c_str());

    // ---- Video tiles --------------------------------------------------
    // Two side-by-side tiles in 16:9: "Remote" shows what Pulse decoded
    // from the SIP peer's RTP (MAIN content slot - this is the whole
    // point: "did we actually receive media?"), "Self-view" shows our
    // local camera feed (SELFVIEW slot) as a sanity check that capture
    // is working. Each tile draws an empty grey rectangle placeholder
    // until its first frame arrives so the layout doesn't jump.
    ImGui::Spacing();
    ImGui::Separator();
    {
        const float avail  = ImGui::GetContentRegionAvail().x;
        const float tile_w = (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        const float tile_h = tile_w * 9.0f / 16.0f;

        auto draw_tile = [&](const char * label, GLTextureContext & ctx) {
            ImGui::BeginGroup();
            ImGui::TextUnformatted(label);
            if (ctx.texture && ctx.last_width > 0 && ctx.last_height > 0) {
                ImGui::Image((ImTextureID)(uintptr_t)ctx.texture,
                             ImVec2(tile_w, tile_h));
            } else {
                ImGui::Dummy(ImVec2(tile_w, tile_h));
                ImGui::GetWindowDrawList()->AddRect(
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    IM_COL32(80, 80, 80, 255));
            }
            ImGui::EndGroup();
        };

        draw_tile("Remote (what we are receiving)", remote_ctx);
        ImGui::SameLine();
        draw_tile("Self-view (local camera)",       selfview_ctx);
    }

    // ---- Live per-bridge UDP counters ---------------------------------
    // One row per AppTransport channel (= one UDP socket), built fresh
    // every frame so the panel auto-resizes from one m= section up to
    // however many wires Pulse negotiated.
    //
    // `app.transport` is non-null iff lazy_pulse_init() bound the bridge
    // on the first Call. Before any Call, or if the user opted out, or
    // if the bind failed, it stays null and we render a one-line notice.
    ImGui::Spacing();
    ImGui::Separator();
    if (!app.transport) {
        if (app.pulse == nullptr)
            ImGui::TextDisabled(
                "UDP bridges: no Call placed yet - nothing has been wired "
                "into Pulse. Tick the AppTransport bridge above and press "
                "Call to engage.");
        else if (app.bridge_enabled.load())
            ImGui::TextDisabled(
                "UDP bridges: AppTransport bind failed when Call was "
                "placed - Pulse owns sockets directly. See status above.");
        else
            ImGui::TextDisabled(
                "UDP bridges: AppTransport was unchecked when Call was "
                "placed - Pulse owns sockets directly.");
    } else {
        std::vector<doppler::BridgeStat> stats = app.transport->snapshot();
        doppler::TransportTotals tot          = app.transport->totals();
        if (stats.empty()) {
            ImGui::TextDisabled("UDP bridges: none yet (no SDP offered).");
        } else {
            ImGui::Text("UDP bridges (%zu):", stats.size());
            // -------------------------------------------------------------
            // Papa -> socket routing-health banner.
            //
            // The whole point of this panel: a fat, colour-coded summary
            // of whether Pulse's outbound packets are even reaching a
            // channel we know how to route. This is the case the user
            // is hunting -- "packets coming out of papa but not making
            // it into the socket". We render it as its own box (not just
            // a `Text`) so the operator notices it at a glance even when
            // the rest of the bridges table looks healthy.
            //
            // Three buckets matter:
            //   * cb_no_channel  - Pulse emitted a channel id we never
            //                      registered (e.g. it sends MUX while
            //                      we registered split RTP/RTCP, or vice
            //                      versa). The packet is dropped before
            //                      any per-row counter can possibly tick.
            //   * cb_null_data   - defensive: NULL/empty buffer from Pulse.
            //   * cb_unbound     - callback after we cleared the binding.
            // If any of them is non-zero we colour the box red; otherwise
            // green ("all callbacks routed to a channel").
            uint64_t cb_lost = tot.cb_no_channel + tot.cb_null_data
                             + tot.cb_unbound;
            const ImVec4 red   (0.85f, 0.30f, 0.30f, 1.0f);
            const ImVec4 green (0.30f, 0.75f, 0.30f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  cb_lost > 0 ? red : green);
            ImGui::BeginChild("papa_to_socket_box",
                              ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 4.5f),
                              true);
            ImGui::TextColored(cb_lost > 0 ? red : green,
                "Papa -> socket routing: %s",
                cb_lost > 0
                    ? "PACKETS LOST BEFORE sendto() - see counters below"
                    : "OK - every callback routed to a channel");
            ImGui::Text("  on_outbound() callbacks total : %llu",
                        static_cast<unsigned long long>(tot.cb_total));
            ImGui::Text("  -> dropped, no matching channel id : %llu",
                        static_cast<unsigned long long>(tot.cb_no_channel));
            ImGui::Text("  -> dropped, NULL/empty buffer      : %llu",
                        static_cast<unsigned long long>(tot.cb_null_data));
            ImGui::Text("  -> dropped, callback after unbind  : %llu",
                        static_cast<unsigned long long>(tot.cb_unbound));
            if (tot.cb_no_channel > 0) {
                // The most common reason for cb_no_channel ticking up is
                // a kind mismatch: Pulse hands us {content,type,MUX} but
                // we registered {content,type,RTP}+{content,type,RTCP},
                // or vice versa. Spell that out so the user doesn't have
                // to dig through stderr to figure it out.
                ImGui::TextColored(red,
                    "  Hint: id mismatch (e.g. Pulse sends MUX while we "
                    "registered split RTP/RTCP). See stderr for the "
                    "actual {content,type,kind} Pulse is using.");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            // Per-channel breakdown follows. The drops in this table are
            // ONLY the ones we could attribute to a specific channel
            // (i.e. the lookup matched); the unattributable ones are in
            // the banner above.
            // Legend for the Callbacks/Drops column further down. Kept
            // inline (rather than as a tooltip on the header) because
            // ImGui table headers don't expose a reliable per-column
            // hover state in the version we link against.
            ImGui::TextDisabled(
                "Drops legend: nr=no-remote  bf=bad-fd  z=zero-size  "
                "se=send-err  errno=last sendto() errno");
            const ImGuiTableFlags flags = ImGuiTableFlags_Borders
                                        | ImGuiTableFlags_RowBg
                                        | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("udp_bridges", 8, flags)) {
                ImGui::TableSetupColumn("Media");
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("Local");
                ImGui::TableSetupColumn("Remote");
                ImGui::TableSetupColumn("TX (pkts / bytes)");
                ImGui::TableSetupColumn("RX (pkts / bytes)");
                ImGui::TableSetupColumn("Callbacks / Drops");
                ImGui::TableSetupColumn("Direction");
                ImGui::TableHeadersRow();

                for (const auto & s : stats) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(s.media.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(s.kind.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(s.local_endpoint.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(s.remote_endpoint.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu / %llu",
                                static_cast<unsigned long long>(s.tx_packets),
                                static_cast<unsigned long long>(s.tx_bytes));
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu / %llu",
                                static_cast<unsigned long long>(s.rx_packets),
                                static_cast<unsigned long long>(s.rx_bytes));
                    ImGui::TableNextColumn();
                    // "cb=N | drops: no-remote/bad-fd/zero/send-err
                    //  [errno=E]" - compact but tells you at a glance
                    // which bucket is absorbing the callbacks.
                    uint64_t drops_total = s.tx_drops_no_remote
                                         + s.tx_drops_bad_fd
                                         + s.tx_drops_zero_size
                                         + s.tx_drops_send_err;
                    if (s.last_send_errno != 0) {
                        ImGui::Text("cb=%llu drops=%llu (nr=%llu/bf=%llu/"
                                    "z=%llu/se=%llu, errno=%d)",
                                    static_cast<unsigned long long>(s.cb_packets),
                                    static_cast<unsigned long long>(drops_total),
                                    static_cast<unsigned long long>(s.tx_drops_no_remote),
                                    static_cast<unsigned long long>(s.tx_drops_bad_fd),
                                    static_cast<unsigned long long>(s.tx_drops_zero_size),
                                    static_cast<unsigned long long>(s.tx_drops_send_err),
                                    s.last_send_errno);
                    } else {
                        ImGui::Text("cb=%llu drops=%llu (nr=%llu/bf=%llu/"
                                    "z=%llu/se=%llu)",
                                    static_cast<unsigned long long>(s.cb_packets),
                                    static_cast<unsigned long long>(drops_total),
                                    static_cast<unsigned long long>(s.tx_drops_no_remote),
                                    static_cast<unsigned long long>(s.tx_drops_bad_fd),
                                    static_cast<unsigned long long>(s.tx_drops_zero_size),
                                    static_cast<unsigned long long>(s.tx_drops_send_err));
                    }
                    ImGui::TableNextColumn();
                    // Directional indicator: arrows light up as soon as the
                    // first packet flows in each direction, so at a glance
                    // you can see which legs are "live".
                    const char * tx_arrow = (s.tx_packets > 0) ? "TX>" : "tx ";
                    const char * rx_arrow = (s.rx_packets > 0) ? "<RX" : " rx";
                    ImGui::Text("%s %s", tx_arrow, rx_arrow);
                }
                ImGui::EndTable();
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped(
        "Pulse owns all media. PJSIP only signals: it sends INVITE over TCP "
        "with Pulse's SDP offer and pipes the 200 OK's answer SDP straight "
        "back into pulse_setup_stage_2_from_structure(). No REGISTER, no "
        "credentials, no TLS - this is the most minimal SIP UA we could "
        "build.");

    ImGui::End();
}

// ----------------------------------------------------------------------------
//  main()
// ----------------------------------------------------------------------------

static void glfw_error_callback(int error, const char * description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int main()
{
    // ---- GLFW + ImGui (same as src/main.cpp) ----------------------------
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow * window = glfwCreateWindow(960, 720,
                                           "Doppler - Pulse + SIP demo",
                                           nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    // ---- Pre-Call setup --------------------------------------------------
    // CRITICAL: nothing in this block touches Pulse state. We do not
    // even create the Pulse handle until the operator presses Call -
    // see lazy_pulse_init() above. The point is to give the operator
    // a real choice with the AppTransport checkbox: until they press
    // Call, no callback has been registered with Pulse, no devices
    // have been attached, no data sessions exist.
    pulse_global_logger_callback(on_pulse_log, nullptr);

    AppState app;

    // Seed the AppTransport bridge checkbox from DOPPLER_SIP_BRIDGE
    // (default = on, because routing media through our app-owned UDP
    // sockets is the whole point of this demo). This is *only* an
    // initial value for the UI checkbox - the actual bind doesn't
    // happen until lazy_pulse_init() runs on the first Call press, at
    // which point whatever value the checkbox holds is what Pulse sees.
    bool bridge_wanted = true;
    if (const char * env = std::getenv("DOPPLER_SIP_BRIDGE")) {
        bridge_wanted = !(env[0] == '0' && env[1] == '\0');
    }
    app.bridge_enabled.store(bridge_wanted);

    // The AppTransport instance itself - its constructor only probes
    // the local egress IP (one UDP socket connect()ed to 8.8.8.8:53,
    // no packets sent) and does NOT call into Pulse. The actual
    // bind_to_pulse() is deferred to lazy_pulse_init() on first Call.
    doppler::AppTransport transport;
    app.transport_storage = &transport;

    // GL-side init for the video tiles. This is harmless to do up
    // front (it's just glGenTextures + glTexParameteri); the Pulse
    // data-session attach is deferred to lazy_pulse_init().
    GLTextureContext remote_ctx;
    GLTextureContext selfview_ctx;
    init_video_render_ctx(remote_ctx,   PULSE_MEDIA_CONTENT_MAIN);
    init_video_render_ctx(selfview_ctx, PULSE_MEDIA_CONTENT_SELFVIEW);
    app.remote_ctx_storage   = &remote_ctx;
    app.selfview_ctx_storage = &selfview_ctx;

    // ---- Boot PJSIP ------------------------------------------------------
    // PJSIP setup does not touch Pulse, so it's fine to run here. Note
    // we still bring SIP up unconditionally: starting the PJSIP UA does
    // not initiate any outbound traffic until place_call() is invoked
    // from inside start_call().
    doppler::SipUA sip;
    std::string sip_err = sip.start("doppler-sip/0.1", /*local_port=*/0);
    if (!sip_err.empty()) {
        std::fprintf(stderr, "PJSIP start failed: %s\n", sip_err.c_str());
        // Carry on without SIP so the UI still surfaces the error to the user.
    }
    app.sip = &sip;

    // ---- Main loop -------------------------------------------------------
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        // Drain any pending Pulse-destruction request from the SIP
        // worker thread (or from the Hang-up button). Doing this
        // before draw_ui() means the very next frame after a hangup
        // already reflects the post-teardown state in the UI.
        if (app.destroy_pulse_pending.exchange(false)) {
            destroy_pulse_now(app, transport, remote_ctx, selfview_ctx);
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        draw_ui(app, remote_ctx, selfview_ctx);
        ImGui::Render();
        int display_w = 0, display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // ---- Shutdown --------------------------------------------------------
    sip.stop();                     // sends BYE if needed
    // Run the same Pulse teardown the Hang-up button uses (no-op if
    // the operator quit before ever pressing Call - destroy_pulse_now
    // returns immediately when app.pulse is null).
    destroy_pulse_now(app, transport, remote_ctx, selfview_ctx);
    // Release the GL textures - destroy_pulse_now() deliberately leaves
    // them alive so the Hang-up path can keep the tiles around for the
    // next Call, but at process exit there's no next Call. Passing
    // nullptr for Pulse skips the data-session disconnect (already done
    // inside destroy_pulse_now or never attached) and just deletes the
    // texture. Also covers the "Pulse never created" case.
    shutdown_video_render_ctx(nullptr, remote_ctx);
    shutdown_video_render_ctx(nullptr, selfview_ctx);
    // Belt-and-braces: transport.shutdown() is idempotent and was
    // already called inside destroy_pulse_now if Pulse existed; this
    // catches the "Pulse never created, transport never bound" path
    // (where shutdown is still safe and a no-op).
    transport.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
