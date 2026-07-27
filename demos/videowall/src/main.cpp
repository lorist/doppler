// ============================================================================
//  videowall - a multi-instance Pexip Pulse compositor & production switcher
// ----------------------------------------------------------------------------
//
//  Think of a control-room production desk. You "prepare" a handful of *sources*
//  - cameras, RTSP/RTMP feeds, still images, mp4 clips, and live Pexip
//  video-conferences - and each one, once started, becomes an *active* source
//  living in a little **library** down the left-hand rail (the way a hardware
//  switcher keeps a stack of inputs).
//
//  From that library you point-click-drag sources onto one or more **canvases**:
//
//      * the PROGRAM (wall) canvas  - the big superwide video wall;
//      * one SEND canvas per dialled-in conference - exactly what *that* far end
//        gets to see (so two different conferences can be shown two different
//        things, all built from the same library);
//      * an optional PRESENTATION canvas paired with each conference, lit up by a
//        "Start presentation" button so the far end sees *both* streams.
//
//  Two architectural ideas make this work, both inherited from the sibling demos:
//
//    1. ONE Pulse instance *per source* (like the original videowall / gateway).
//       Each source hides a whole Pulse behind it; we pull its single frame out
//       and composite everything ourselves - here on the CPU - rather than using
//       Pulse's own mixer. The same active source can be dropped onto many
//       canvases, and even twice onto the same canvas, because a *placement* is
//       just a lightweight {source, x, y, w, h} record that references the source
//       by id; the heavy media object is shared.
//
//    2. A video-conference is BOTH a source AND a sink. Its far-end video is
//       pulled *out* and shown in the library like any other source (the
//       inbound-pull path: pulse_data_session_connect_output +
//       pulse_data_session_pull_frame_data). But it ALSO has an outbound canvas
//       we composite and push back *in* - the mirror-image path
//       (pulse_data_session_connect_input + pulse_data_session_push_frame),
//       lifted from gateway. We render the SEND canvas to an RGBA buffer and push
//       it as the conference's MAIN input; the PRESENTATION canvas, when active,
//       is pushed on the PRESENTATION content slot.
//
//  The per-source *input* recipe is unchanged from the original videowall:
//
//      camera  -> pulse_device_session_connect_device
//      rtsp    -> pulse_rtsp_session_connect_input + bind_to_content(MAIN)
//      rtmp    -> pulse_rtmp_session_connect_input(MAIN)
//      image   -> pulse_video_mix_input_from_file        -> mix onto MAIN
//      mp4     -> pulse_video_mix_input_from_file_with_loop -> mix onto MAIN
//      conf    -> pulse_connect_with_rest_async, render MAIN (the far end)
//
//  Everything else is Dear ImGui plumbing: a library rail on the left (prepare /
//  list / drag / stop sources) and a tabbed, switcher-style canvas area on the
//  right (Program wall + one tab per conference with its Send/Presentation buses).
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Dear ImGui + the two backends we link against in CMake.
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

// The single header that pulls in the full Pulse API surface.
#include <pexpulse/pulse.h>

// ImGui-Addons file browser (same one pexninja uses) for the "Browse..."
// buttons that point Image / MP4 sources at a file on disk.
#include <ImGuiFileBrowser.h>

// ----------------------------------------------------------------------------
//  Source kinds
// ----------------------------------------------------------------------------

enum class SourceKind
{
    Camera = 0,
    Microphone,
    Rtsp,
    Rtmp,
    Image,
    Mp4,
    Conference,
};

static const char * kSourceKindLabels[] = {
    "Camera", "Microphone", "RTSP stream", "RTMP stream", "Image (jpg/png)", "MP4 file",
    "Video-conference",
};

// ----------------------------------------------------------------------------
//  Media type - is a source audio, video, or both?
// ----------------------------------------------------------------------------
//
//  A source carries a media type so the UI knows where it belongs: an *audio*
//  source feeds the audio mixer, a *video* source can be dropped onto a canvas,
//  and a *both* source does both. The kind picks a sensible default (a
//  microphone is audio-only, an image is video-only, a conference is both) which
//  the user can still override in the prepare form.
// ----------------------------------------------------------------------------

enum class MediaType
{
    Video = 0,
    Audio,
    Both,
};

static const char * kMediaTypeLabels[] = { "Video", "Audio", "Both" };

static MediaType default_media_type(SourceKind kind)
{
    switch (kind) {
        case SourceKind::Camera:     return MediaType::Video;
        case SourceKind::Microphone: return MediaType::Audio;
        case SourceKind::Rtsp:       return MediaType::Both;
        case SourceKind::Rtmp:       return MediaType::Both;
        case SourceKind::Image:      return MediaType::Video;
        case SourceKind::Mp4:        return MediaType::Both;
        case SourceKind::Conference: return MediaType::Both;
    }
    return MediaType::Video;
}

// The resolution we send to each conference far end. The video *wall* (Program)
// is deliberately superwide, but what we push back into a conference must match a
// sensible endpoint resolution - so the SEND (and PRESENTATION) buses are a plain
// 1080p frame rather than the wall's geometry.
static const int kSendCanvasW = 1920;
static const int kSendCanvasH = 1080;

// How many audio-level samples Pulse keeps in its smoothing window for the VU
// meters (the size of the level-averaging window Pulse maintains internally).
static const uint32_t kAudioLevelWindow = 25;

// VU-meter mapping: Pulse reports input levels as a (negated) dB magnitude where
// 0 is loud and ~127 is silence. These bound the 0..1 bar, and the smoothing
// factor sets how quickly the bar chases a new level each frame.
static const float kMinAudioLevelDb     = -127.0f;
static const float kMaxAudioLevelDb     = 0.0f;
static const float kVuMeterSmoothing    = 0.35f;
static const float kPi                  = 3.14159265358979323846f;

// ----------------------------------------------------------------------------
//  A small CPU-side RGBA image - the freshest frame for a source, and the
//  scratch buffers we composite outbound canvases into. Compositing happens on
//  the CPU (not via Pulse's mixer or a GL FBO) to keep the demo dependency-free
//  and the data path obvious: pull RGBA out, paint it into a bigger RGBA buffer,
//  push that buffer back in.
// ----------------------------------------------------------------------------

struct RgbaImage
{
    std::vector<uint8_t> px;   // tightly packed RGBA, row-major, top-down
    int w = 0;
    int h = 0;
};

// ----------------------------------------------------------------------------
//  Outbound sink - the "send side" of a conference source
// ----------------------------------------------------------------------------
//
//  Only conference sources own one of these. It holds the two canvases we
//  compose for that far end (the main SEND bus and the optional PRESENTATION
//  bus), the input data-sessions we push them into, and a little bookkeeping so
//  we only re-describe the format to Pulse when the canvas size changes.
// ----------------------------------------------------------------------------

struct Placement;  // fwd

struct Canvas
{
    std::string name;
    int  w = 1280;
    int  h = 720;
    std::vector<Placement> items;  // back of the vector == top of the z-order
    int  selected = -1;            // index into items, or -1
};

struct OutboundSink
{
    Canvas send;          // pushed to the conference MAIN input (what they see)
    Canvas presentation;  // pushed to the PRESENTATION input when active

    bool present_active = false;  // is the presentation bus being sent?

    bool main_input_open = false;
    bool pres_input_open = false;

    // The last dimensions we described to Pulse, per slot. When the canvas is
    // resized we attach a fresh update_config on the next pushed frame.
    int last_main_w = 0, last_main_h = 0;
    int last_pres_w = 0, last_pres_h = 0;

    // Scratch buffers reused every frame so we are not reallocating ~3.5 MB a
    // tick. Composited from the library, then pushed.
    RgbaImage main_buf;
    RgbaImage pres_buf;

    double last_push_time = 0.0;  // simple FPS throttle for the push path
};

// ----------------------------------------------------------------------------
//  An active source in the library
// ----------------------------------------------------------------------------
//
//  This is what the original demo called `Source`, minus the placement: a live
//  media object plus the Pulse instance behind it and the GL texture / CPU frame
//  we paint from. Placement now lives separately (see Placement) so the same
//  source can appear on many canvases.
// ----------------------------------------------------------------------------

struct ActiveSource
{
    int        id   = 0;            // stable identity; placements reference this
    SourceKind kind = SourceKind::Camera;
    MediaType  media_type = MediaType::Video;  // audio / video / both (routing)
    char       name[64] = "Source";

    // ---- per-kind configuration --------------------------------------------
    char camera_name[128] = "";     // empty => system default / first camera
    char microphone_name[128] = ""; // empty => system default / first microphone
    char file_path[512]   = "";     // image / mp4 path
    char rtsp_url[512]    = "rtsp://";
    int  rtmp_port        = 1935;   // each RTMP source listens on its own port
    char rtmp_path[64]    = "live";
    char conference_id[256] = "";   // "name@server", e.g. "havard@pexipdemo.com"
    char display_name[128]  = "Video wall";
    char pin_code[64]       = "";   // conference PIN (empty => none). Display is
                                    // masked in the UI; like the other config
                                    // fields it is kept in plain memory only.
    bool loop = true;               // mp4 looping

    // ---- the Pulse instance hiding behind this source ----------------------
    Pulse * pulse   = nullptr;
    bool    started = false;
    std::string last_error;

    // Which media-content slot we pull frames from for rendering. For every
    // local source this is SELFVIEW (Pulse's preview of our own input); for a
    // conference it is MAIN (the far end).
    PulseMediaContent render_content = PULSE_MEDIA_CONTENT_SELFVIEW;

    // Resources we have to release on stop().
    PulseVideoMixInputID mix_input  = PULSE_VIDEO_MIX_INPUT_ID_NONE;  // image / mp4
    PulseRtspSessionID   rtsp_session = 0;                            // rtsp (0 == none)
    bool rtmp_listening = false;

    // Live conference status (written from a Pulse callback thread).
    std::atomic<int> conn_status{PULSE_CONNECTION_STATUS_DISCONNECTED};

    // Conference sources additionally own an outbound sink (the send side).
    std::unique_ptr<OutboundSink> sink;

    // ---- audio metering + mixer strip --------------------------------------
    //
    //  Every source feeds one channel strip in the audio mixer. The VU meter is
    //  driven by Pulse's input audio-level callback
    //  (pulse_register_device_audio_level_callback), the same API pexninja uses
    //  for its mic meter. `audio_levels` is filled from a Pulse callback thread
    //  and drained on the UI thread, so it is guarded by `audio_mutex`.
    std::mutex            audio_mutex;
    std::deque<uint8_t>   audio_levels;        // raw |dB| samples, newest at back
    bool                  audio_cb_registered = false;
    float                 vu = 0.0f;           // smoothed 0..1 level (UI thread)

    // Mixer controls. These are real UI today but only *wired* to Pulse later -
    // they are the knobs/switches the demo exposes so the plumbing has a home.
    float gain_db           = 0.0f;            // channel fader, -60..+12 dB
    bool  mute              = false;
    bool  solo              = false;
    bool  noise_suppression = false;           // -> a Pulse NS API, later
    float eq_low            = 0.0f;            // 3-band EQ, -12..+12 dB each
    float eq_mid            = 0.0f;
    float eq_high           = 0.0f;

    // ---- rendering ----------------------------------------------------------
    GLuint    texture = 0;          // the inbound frame, for on-screen thumbnails
    int       tex_w   = 0;
    int       tex_h   = 0;
    RgbaImage frame_cpu;            // same frame kept CPU-side for compositing
};

// ----------------------------------------------------------------------------
//  A placement - one appearance of a library source on a canvas
// ----------------------------------------------------------------------------

struct Placement
{
    int   source_id = 0;            // which ActiveSource (by id)
    float x = 0.0f, y = 0.0f;       // top-left corner, in canvas pixels
    float w = 480.0f, h = 270.0f;   // size, in canvas pixels
    float aspect = 480.0f / 270.0f; // w/h, preserved while resizing
};

struct AppState
{
    // The library of prepared, live sources.
    std::vector<std::unique_ptr<ActiveSource>> library;
    int next_source_id = 1;
    int selected_source = -1;       // id of the library source shown in the inspector

    // The PROGRAM (wall) canvas - superwide by default, a control-room wall.
    Canvas program{ "Program (Wall)", 3480, 1080, {}, -1 };

    // A long-lived Pulse instance used only to enumerate capture devices so the
    // "Camera" dropdown has something to show before a source is started.
    Pulse * enum_pulse = nullptr;
    std::vector<std::string> camera_names;
    std::vector<std::string> microphone_names;

    // The "Prepare source" staging form. We configure a source here, then
    // "Prepare" starts it and (on success) moves it into the library.
    ActiveSource staging;
    int          staging_kind = 0;

    // Height (in px) of the audio-mixer pane that sits *under* the canvas area.
    // The user drags the splitter between them to re-balance the two; this is
    // the mixer's share and the canvas takes whatever is left.
    float mixer_height = 240.0f;
};

// ----------------------------------------------------------------------------
//  Logging hook (shared by every Pulse instance)
// ----------------------------------------------------------------------------

static void on_pulse_log(void * /*uc*/, PulseDebugLevel level, const char * category,
                         int64_t /*wall*/, int64_t /*elapsed*/, unsigned int /*pid*/,
                         const char * /*file*/, const char * /*func*/, int /*line*/,
                         const char * /*obj*/, const char * message)
{
    if (level > PULSE_LEVEL_WARNING) return;  // warnings and worse only
    std::fprintf(stderr, "[pulse:%s] %s\n", category ? category : "?", message ? message : "");
}

// ----------------------------------------------------------------------------
//  Conference callbacks (one ActiveSource* per conference tile)
// ----------------------------------------------------------------------------

static void on_conf_status(const PulseConferenceStatusInfo * info, void * uc)
{
    auto * src = static_cast<ActiveSource *>(uc);
    src->conn_status.store(static_cast<int>(info->status));
}

static void on_conf_result(const PulseError err, void * uc)
{
    auto * src = static_cast<ActiveSource *>(uc);
    if (err != PULSE_SUCCESS)
        src->last_error = std::string("conference connect failed: ") + pulse_strerror(err);
}

static void on_conf_progress(const PulseOperationProgressInfo * /*info*/, void * /*uc*/)
{
    // The wall does not surface per-step progress; the status enum is enough.
}

// ----------------------------------------------------------------------------
//  Audio-level callback (one per source, drives that strip's VU meter)
// ----------------------------------------------------------------------------
//
//  Pulse hands us a short list of recent input audio levels for the source's own
//  instance. Each `level` is a magnitude in (negated) dB - 0 is loud, ~127 is
//  silence - exactly as pexninja consumes it. We stash them and let the UI
//  thread map them to a 0..1 bar.

static void on_source_audio_level(void * uc, const PulseAudioLevelMetasList * list)
{
    auto * src = static_cast<ActiveSource *>(uc);
    if (!list) return;

    std::lock_guard<std::mutex> lock(src->audio_mutex);
    for (size_t i = 0; i < list->len; ++i)
        src->audio_levels.push_back(list->metas[i].level);
    while (src->audio_levels.size() > kAudioLevelWindow)
        src->audio_levels.pop_front();
}

// ----------------------------------------------------------------------------
//  Video rendering helpers (data session <-> RGBA). Pull side from doppler,
//  push side from gateway.
// ----------------------------------------------------------------------------

// Routing helpers driven by a source's media type: an audio source feeds the
// mixer, a video source feeds the canvases, a "both" source feeds both.
static bool source_has_video(const ActiveSource & s)
{
    return s.media_type == MediaType::Video || s.media_type == MediaType::Both;
}
static bool source_has_audio(const ActiveSource & s)
{
    return s.media_type == MediaType::Audio || s.media_type == MediaType::Both;
}

static PulseDataSessionConfig * make_video_output_config()
{
    // The OUTPUT (pull) side just wants already-decoded RGBA frames; caps are
    // enough and Pulse fills in the real dimensions on every pulled frame.
    PulseDataSessionConfig * cfg =
        pulse_data_session_config_new(PULSE_DATA_SESSION_VIDEO_FROM_CAPS);
    pulse_data_session_config_video_from_caps(cfg, "video/x-raw, format=RGBA");
    return cfg;
}

static PulseDataSessionConfig * make_rgba_input_config(int w, int h)
{
    // The INPUT (push) side is configured with VIDEO_FROM_VALUES so we can keep
    // Pulse in sync with the canvas resolution: PulseDataSessionFrameData itself
    // only carries data + size, so the dimensions have to ride on the config
    // (at connect time, and again via update_config whenever they change).
    PulseDataSessionConfig * cfg =
        pulse_data_session_config_new(PULSE_DATA_SESSION_VIDEO_FROM_VALUES);
    PulseDimensions dims{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    PulseFramerate  fps{ 30, 1 };
    pulse_data_session_config_video_from_values(cfg, PULSE_MEDIA_PIXEL_FORMAT_RGBA, dims, fps);
    return cfg;
}

// Pull the freshest RGBA frame Pulse has for `render_content` and upload it to
// the source's GL texture (for thumbnails) while keeping a CPU copy (for
// outbound compositing). Non-blocking; a no-op when nothing is ready yet.
static void pump_frame(ActiveSource & src)
{
    if (!src.pulse || !source_has_video(src)) return;

    PulseDataSessionFrameData * frame = nullptr;
    pulse_data_session_pull_frame_data(src.pulse, PULSE_MEDIA_VIDEO, &frame,
                                       src.render_content, 0);
    if (!frame) return;

    int w = 0, h = 0;
    if (pulse_frame_data_get_resolution(frame, &w, &h) && w > 0 && h > 0 && frame->data) {
        // Keep a CPU copy for the compositor.
        src.frame_cpu.w = w;
        src.frame_cpu.h = h;
        src.frame_cpu.px.assign(frame->data,
                                frame->data + static_cast<size_t>(w) * h * 4);

        // And upload to the GL texture for thumbnails / canvas previews.
        if (src.texture) {
            glBindTexture(GL_TEXTURE_2D, src.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, frame->data);
            src.tex_w = w;
            src.tex_h = h;
        }
    }
    pulse_data_session_frame_data_free(frame);
}

// ----------------------------------------------------------------------------
//  CPU compositor
// ----------------------------------------------------------------------------

static ActiveSource * find_source(AppState & app, int id)
{
    for (auto & s : app.library)
        if (s->id == id) return s.get();
    return nullptr;
}

// Nearest-neighbour blit of a source frame into a destination region, clipped to
// the destination bounds. Opaque overwrite - good enough for a demo wall.
static void blit_scaled(RgbaImage & dst, const RgbaImage & src,
                        int dx, int dy, int dw, int dh)
{
    if (src.w <= 0 || src.h <= 0 || dw <= 0 || dh <= 0) return;

    for (int yy = 0; yy < dh; ++yy) {
        const int ty = dy + yy;
        if (ty < 0 || ty >= dst.h) continue;
        int sy = static_cast<int>(static_cast<int64_t>(yy) * src.h / dh);
        if (sy >= src.h) sy = src.h - 1;

        for (int xx = 0; xx < dw; ++xx) {
            const int tx = dx + xx;
            if (tx < 0 || tx >= dst.w) continue;
            int sx = static_cast<int>(static_cast<int64_t>(xx) * src.w / dw);
            if (sx >= src.w) sx = src.w - 1;

            const uint8_t * s = &src.px[(static_cast<size_t>(sy) * src.w + sx) * 4];
            uint8_t *       d = &dst.px[(static_cast<size_t>(ty) * dst.w + tx) * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
        }
    }
}

// Paint every placement of a canvas into `out` (resized to the canvas), bottom
// of the z-order first.
static void composite_canvas(AppState & app, const Canvas & canvas, RgbaImage & out)
{
    out.w = canvas.w;
    out.h = canvas.h;
    out.px.assign(static_cast<size_t>(canvas.w) * canvas.h * 4, 0);
    // Opaque dark backdrop.
    for (size_t i = 0; i < out.px.size(); i += 4) {
        out.px[i + 0] = 16; out.px[i + 1] = 16; out.px[i + 2] = 20; out.px[i + 3] = 255;
    }

    for (const Placement & p : canvas.items) {
        ActiveSource * s = find_source(app, p.source_id);
        if (!s || s->frame_cpu.w <= 0) continue;
        blit_scaled(out, s->frame_cpu,
                    static_cast<int>(p.x), static_cast<int>(p.y),
                    static_cast<int>(p.w), static_cast<int>(p.h));
    }
}

// Push one composited buffer into a conference input slot, re-describing the
// resolution only when it changes.
static void push_canvas(ActiveSource & conf, PulseMediaContent slot, RgbaImage & img,
                        int & last_w, int & last_h)
{
    if (!conf.pulse || img.w <= 0 || img.h <= 0) return;

    PulseDataSessionFrame frame{};
    PulseDataSessionConfig * upd = nullptr;
    if (img.w != last_w || img.h != last_h) {
        upd = make_rgba_input_config(img.w, img.h);
        frame.update_config = upd;
        last_w = img.w;
        last_h = img.h;
    }
    frame.video.data      = img.px.data();
    frame.video.data_size = static_cast<int>(img.px.size());

    pulse_data_session_push_frame(conf.pulse, &frame, slot);
    if (upd) pulse_data_session_config_free(upd);
}

// ----------------------------------------------------------------------------
//  Device enumeration (for the camera / microphone dropdowns)
// ----------------------------------------------------------------------------

static void refresh_camera_list(AppState & app)
{
    app.camera_names.clear();
    if (!app.enum_pulse) return;

    PulseDeviceIterator * it = nullptr;
    pulse_device_iterator_new(app.enum_pulse, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT, &it);
    if (!it) return;
    for (const PulseDevice * d = pulse_device_iterator_first(it);
         d != nullptr;
         d = pulse_device_iterator_next(it)) {
        const char * n = pulse_device_get_name(d);
        app.camera_names.emplace_back(n ? n : "(unnamed)");
    }
    pulse_device_iterator_free(it);
}

static void refresh_microphone_list(AppState & app)
{
    app.microphone_names.clear();
    if (!app.enum_pulse) return;

    PulseDeviceIterator * it = nullptr;
    pulse_device_iterator_new(app.enum_pulse, PULSE_MEDIA_AUDIO, PULSE_MEDIA_INPUT, &it);
    if (!it) return;
    for (const PulseDevice * d = pulse_device_iterator_first(it);
         d != nullptr;
         d = pulse_device_iterator_next(it)) {
        const char * n = pulse_device_get_name(d);
        app.microphone_names.emplace_back(n ? n : "(unnamed)");
    }
    pulse_device_iterator_free(it);
}

// Bind a camera (by name, falling back to the system default / first one) to a
// source's own Pulse instance as the MAIN video input.
static PulseError connect_camera(ActiveSource & src)
{
    PulseDeviceIterator * it = nullptr;
    pulse_device_iterator_new(src.pulse, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT, &it);
    if (!it) return PULSE_ERROR_NOT_CONFIGURED;

    const PulseDevice * chosen   = nullptr;
    const PulseDevice * fallback = nullptr;
    for (const PulseDevice * d = pulse_device_iterator_first(it);
         d != nullptr;
         d = pulse_device_iterator_next(it)) {
        if (!fallback) fallback = d;
        if (pulse_device_is_system_default(d) && !chosen) chosen = d;
        const char * n = pulse_device_get_name(d);
        if (src.camera_name[0] != '\0' && n && std::strcmp(n, src.camera_name) == 0) {
            chosen = d;
            break;
        }
    }
    if (!chosen) chosen = fallback;

    PulseError err = PULSE_ERROR_NOT_CONFIGURED;
    if (chosen) {
        err = pulse_device_session_connect_device(src.pulse, chosen,
                                                  PULSE_MEDIA_CONTENT_MAIN);
    }
    pulse_device_iterator_free(it);
    return err;
}

// Bind a microphone (by name, falling back to the system default / first one) to
// a source's own Pulse instance as the MAIN audio input. With a real capture
// device attached, Pulse's input audio-level callback finally has a live signal
// to report - which is what drives this source's VU meter in the mixer.
static PulseError connect_microphone(ActiveSource & src)
{
    PulseDeviceIterator * it = nullptr;
    pulse_device_iterator_new(src.pulse, PULSE_MEDIA_AUDIO, PULSE_MEDIA_INPUT, &it);
    if (!it) return PULSE_ERROR_NOT_CONFIGURED;

    const PulseDevice * chosen   = nullptr;
    const PulseDevice * fallback = nullptr;
    for (const PulseDevice * d = pulse_device_iterator_first(it);
         d != nullptr;
         d = pulse_device_iterator_next(it)) {
        if (!fallback) fallback = d;
        if (pulse_device_is_system_default(d) && !chosen) chosen = d;
        const char * n = pulse_device_get_name(d);
        if (src.microphone_name[0] != '\0' && n && std::strcmp(n, src.microphone_name) == 0) {
            chosen = d;
            break;
        }
    }
    if (!chosen) chosen = fallback;

    PulseError err = PULSE_ERROR_NOT_CONFIGURED;
    if (chosen) {
        err = pulse_device_session_connect_device(src.pulse, chosen,
                                                  PULSE_MEDIA_CONTENT_MAIN);
    }
    pulse_device_iterator_free(it);
    return err;
}

// Acquire a still image / mp4 as a video-mix input and connect a one-input mix
// onto MAIN, so the source's self-view previews it. Returns the input id (which
// must be released on stop) via src.mix_input.
static PulseError connect_file_via_mix(ActiveSource & src, bool loop)
{
    PulseVideoMixInputID id = PULSE_VIDEO_MIX_INPUT_ID_NONE;
    PulseError err = loop
        ? pulse_video_mix_input_from_file_with_loop(src.pulse, src.file_path, true, &id)
        : pulse_video_mix_input_from_file(src.pulse, src.file_path, &id);
    if (err != PULSE_SUCCESS) return err;

    PulseVideoMixInput in{};
    in.input_id       = id;
    in.layer          = 0;
    in.width_ratio    = 0.0;   // 0 == fill the layer
    in.height_ratio   = 0.0;
    in.x_centrepoint  = 0.5;
    in.y_centrepoint  = 0.5;
    in.videoproc_mask = PULSE_VIDEO_PROCESS_TYPE_NONE;

    PulseVideoMixConfig cfg{};
    cfg.num_inputs = 1;
    cfg.inputs     = &in;

    err = pulse_video_mix_connect(src.pulse, &cfg, PULSE_MEDIA_CONTENT_MAIN);
    if (err != PULSE_SUCCESS) {
        pulse_video_mix_input_release(src.pulse, id);
        return err;
    }
    src.mix_input = id;
    return PULSE_SUCCESS;
}

// Split "name@server" into its two halves. Returns false if the '@' is missing.
static bool split_conference_id(const char * id, std::string & conference, std::string & server)
{
    const char * at = std::strchr(id, '@');
    if (!at || at == id || at[1] == '\0') return false;
    conference.assign(id, at);
    server.assign(at + 1);
    return true;
}

// ----------------------------------------------------------------------------
//  Shared file picker (Image / MP4 "Browse..." buttons)
// ----------------------------------------------------------------------------
//
//  A single ImGui-Addons file browser plus a one-slot "pending request" record
//  that remembers which char buffer the chosen path should land in. The browser
//  is a modal popup, so at most one can be open at a time - one instance + one
//  request is all we need. Lifted from pexninja's file_picker. render_pending()
//  must be called once per frame at the top level (outside any Begin/End).

namespace file_picker
{
struct Request
{
    char *      dest      = nullptr;   // caller's fixed-size buffer to overwrite
    size_t      dest_size = 0;
    std::string title;                 // unique modal id + window title
    std::string exts;                  // comma-separated extensions, e.g. ".mp4"
    bool        just_opened = false;   // defer OpenPopup to render_pending()
};

static imgui_addons::ImGuiFileBrowser g_browser;
static Request                        g_request;

// Open the picker for `dest`. Safe to call inside any window scope.
static void request_open(char * dest, size_t dest_size, const char * title, const char * exts)
{
    g_request.dest        = dest;
    g_request.dest_size   = dest_size;
    g_request.title       = title ? title : "Open file";
    g_request.exts        = exts ? exts : "*.*";
    g_request.just_opened = true;
}

// Render the open dialog (if any). Call once per frame at the top level so the
// popup's ID stack is stable across windows. Cheap when nothing is pending.
static void render_pending()
{
    if (g_request.dest == nullptr) return;
    if (g_request.just_opened) {
        ImGui::OpenPopup(g_request.title.c_str());
        g_request.just_opened = false;
    }
    if (g_browser.showFileDialog(g_request.title,
                                 imgui_addons::ImGuiFileBrowser::DialogMode::OPEN,
                                 ImVec2(700, 380), g_request.exts)) {
        std::snprintf(g_request.dest, g_request.dest_size, "%s",
                      g_browser.selected_path.c_str());
        g_request.dest      = nullptr;
        g_request.dest_size = 0;
    } else if (!ImGui::IsPopupOpen(g_request.title.c_str())) {
        // Cancelled / closed without a selection.
        g_request.dest      = nullptr;
        g_request.dest_size = 0;
    }
}

// A "Browse..." button that opens the picker for `dest` when clicked.
static void browse_button(const char * label_id, char * dest, size_t dest_size,
                          const char * title, const char * exts)
{
    if (ImGui::Button(label_id))
        request_open(dest, dest_size, title, exts);
}
} // namespace file_picker

// ----------------------------------------------------------------------------
//  Source lifecycle
// ----------------------------------------------------------------------------

static void stop_source(ActiveSource & src);  // fwd

// Spin up the Pulse instance behind a source and point its input at the chosen
// source (or, for a conference, dial the call). Idempotent.
static void start_source(ActiveSource & src)
{
    if (src.started) return;
    src.last_error.clear();

    src.pulse = pulse_new();
    if (!src.pulse) {
        src.last_error = "pulse_new() returned NULL";
        return;
    }

    // We paint the frames ourselves, so make sure Pulse never spawns its own
    // native windows. Must happen before any media flows.
    pulse_options_set_self_view_window_handle(src.pulse, nullptr);
    pulse_options_set_remote_video_window_handle(src.pulse, nullptr);
    pulse_options_set_presentation_video_window_handle(src.pulse, nullptr);
    pulse_options_set_application_user_agent_string(src.pulse, "videowall/0.2");

    PulseError err = PULSE_SUCCESS;

    switch (src.kind) {
        case SourceKind::Camera:
            src.render_content = PULSE_MEDIA_CONTENT_SELFVIEW;
            err = connect_camera(src);
            break;

        case SourceKind::Microphone:
            // An audio-only source: connect a capture device and let the audio
            // level callback (registered below) feed its mixer strip. There is
            // no video to render.
            src.render_content = PULSE_MEDIA_CONTENT_SELFVIEW;
            err = connect_microphone(src);
            break;

        case SourceKind::Rtsp: {
            src.render_content = PULSE_MEDIA_CONTENT_SELFVIEW;
            if (src.rtsp_url[0] == '\0') { err = PULSE_ERROR_INVALID_PARAMETER; break; }
            PulseRtspInputConfig cfg{};
            cfg.location   = src.rtsp_url;
            cfg.transport  = PULSE_RTSP_TRANSPORT_TCP;
            cfg.latency_ms = 200;
            PulseRtspSessionID session = 0;
            err = pulse_rtsp_session_connect_input(src.pulse, &cfg, &session);
            if (err == PULSE_SUCCESS) {
                src.rtsp_session = session;
                err = pulse_rtsp_session_bind_to_content(src.pulse, session,
                                                         PULSE_MEDIA_CONTENT_MAIN);
            }
            break;
        }

        case SourceKind::Rtmp: {
            src.render_content = PULSE_MEDIA_CONTENT_SELFVIEW;
            PulseRtmpInputConfig cfg{};
            cfg.path           = src.rtmp_path;
            cfg.listening_port = static_cast<uint16_t>(src.rtmp_port);
            cfg.use_tls        = false;
            cfg.support_audio  = true;
            cfg.support_video  = true;
            err = pulse_rtmp_session_connect_input(src.pulse, PULSE_MEDIA_CONTENT_MAIN, &cfg);
            if (err == PULSE_SUCCESS) src.rtmp_listening = true;
            break;
        }

        case SourceKind::Image:
            src.render_content = PULSE_MEDIA_CONTENT_SELFVIEW;
            if (src.file_path[0] == '\0') { err = PULSE_ERROR_INVALID_PARAMETER; break; }
            err = connect_file_via_mix(src, /*loop=*/false);
            break;

        case SourceKind::Mp4:
            src.render_content = PULSE_MEDIA_CONTENT_SELFVIEW;
            if (src.file_path[0] == '\0') { err = PULSE_ERROR_INVALID_PARAMETER; break; }
            err = connect_file_via_mix(src, /*loop=*/src.loop);
            break;

        case SourceKind::Conference: {
            // The far end is the interesting picture here, so render MAIN.
            src.render_content = PULSE_MEDIA_CONTENT_MAIN;
            std::string conference, server;
            if (!split_conference_id(src.conference_id, conference, server)) {
                err = PULSE_ERROR_INVALID_PARAMETER;
                src.last_error = "conference id must look like name@server";
                break;
            }
            PulseConferenceStatusCallbackConfig conf_cb{ on_conf_status, &src };
            pulse_options_set_conference_state_callback(src.pulse, &conf_cb);

            PulseRestConnectionConfig cfg{};
            cfg.server_address  = server.c_str();
            cfg.conference_name = conference.c_str();
            cfg.display_name    = src.display_name;
            cfg.pin_code        = src.pin_code[0] ? src.pin_code : nullptr;

            PulseAsyncOperationResultCallbackConfig result_cb{ on_conf_result, &src };
            PulseOperationProgressCallbackConfig    progress_cb{ on_conf_progress, &src };
            err = pulse_connect_with_rest_async(src.pulse, &cfg, &result_cb, &progress_cb);

            // Stand up this conference's outbound sink: the SEND bus (pushed to
            // MAIN) and a PRESENTATION bus, plus their canvases. We open the
            // MAIN input session now so the moment a frame is composited we can
            // push it; PRESENTATION is opened lazily by "Start presentation".
            if (err == PULSE_SUCCESS) {
                src.sink = std::make_unique<OutboundSink>();
                // The SEND bus is exactly what the far end receives, so it must
                // match the resolution we send. Keep it a plain 1080p frame
                // (1920x1080) - NOT the superwide wall geometry - so conference
                // endpoints get a sensible, standard picture.
                src.sink->send.name         = "Send";
                src.sink->send.w            = kSendCanvasW;
                src.sink->send.h            = kSendCanvasH;
                src.sink->presentation.name = "Presentation";
                src.sink->presentation.w    = kSendCanvasW;
                src.sink->presentation.h    = kSendCanvasH;

                PulseDataSessionConfig * icfg =
                    make_rgba_input_config(src.sink->send.w, src.sink->send.h);
                if (pulse_data_session_connect_input(src.pulse, icfg,
                                                     PULSE_MEDIA_CONTENT_MAIN) == PULSE_SUCCESS) {
                    src.sink->main_input_open = true;
                    src.sink->last_main_w = src.sink->send.w;
                    src.sink->last_main_h = src.sink->send.h;
                }
                pulse_data_session_config_free(icfg);
            }
            break;
        }
    }

    if (err != PULSE_SUCCESS) {
        if (src.last_error.empty())
            src.last_error = std::string("start failed: ") + pulse_strerror(err);
        stop_source(src);
        return;
    }

    // Open the RGBA output we will pull frames from, and a GL texture to hold
    // them. Pulse starts feeding the session as soon as media is available.
    // Audio-only sources (e.g. a microphone) have no video to pull, so we skip
    // the texture + output session entirely for them.
    if (source_has_video(src)) {
        glGenTextures(1, &src.texture);
        glBindTexture(GL_TEXTURE_2D, src.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        PulseDataSessionConfig * dcfg = make_video_output_config();
        pulse_data_session_connect_output(src.pulse, dcfg, src.render_content);
        pulse_data_session_config_free(dcfg);
    }

    // Subscribe to this instance's input audio levels so the mixer's VU meter
    // for this strip has data. Best-effort: a source with no audio simply reads
    // as silence, which is exactly what a mixer channel should show.
    if (pulse_register_device_audio_level_callback(src.pulse, PULSE_MEDIA_INPUT,
                                                   kAudioLevelWindow,
                                                   on_source_audio_level, &src) == PULSE_SUCCESS)
        src.audio_cb_registered = true;

    src.started = true;
}

// Tear a source's Pulse instance down and free every resource it acquired.
// Safe to call on a never-started or half-started source.
static void stop_source(ActiveSource & src)
{
    if (!src.pulse) {
        src.started = false;
        return;
    }

    // Stop the VU-meter feed before anything else (mirrors the register in
    // start_source).
    if (src.audio_cb_registered) {
        pulse_deregister_device_audio_level_callback(src.pulse, PULSE_MEDIA_INPUT);
        src.audio_cb_registered = false;
    }
    {
        std::lock_guard<std::mutex> lock(src.audio_mutex);
        src.audio_levels.clear();
    }
    src.vu = 0.0f;

    // Disconnect the data-session output (mirrors connect_output). Only opened
    // for video-capable sources.
    if (source_has_video(src))
        pulse_data_session_disconnect(src.pulse, PULSE_MEDIA_VIDEO, PULSE_MEDIA_OUTPUT,
                                      src.render_content);

    switch (src.kind) {
        case SourceKind::Camera:
            pulse_device_session_disconnect_main_video(src.pulse, PULSE_MEDIA_CONTENT_MAIN,
                                                       PULSE_MEDIA_INPUT);
            break;
        case SourceKind::Microphone:
            pulse_device_session_disconnect_main_audio(src.pulse);
            break;
        case SourceKind::Rtsp:
            if (src.rtsp_session != 0) {
                pulse_rtsp_session_disconnect_input(src.pulse, src.rtsp_session);
                src.rtsp_session = 0;
            }
            break;
        case SourceKind::Rtmp:
            if (src.rtmp_listening) {
                pulse_rtmp_session_disconnect_input(src.pulse, PULSE_MEDIA_CONTENT_MAIN);
                src.rtmp_listening = false;
            }
            break;
        case SourceKind::Image:
        case SourceKind::Mp4:
            pulse_video_mix_disconnect(src.pulse, PULSE_MEDIA_CONTENT_MAIN);
            if (src.mix_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
                pulse_video_mix_input_release(src.pulse, src.mix_input);
                src.mix_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
            }
            break;
        case SourceKind::Conference:
            // Tear down the outbound (send / presentation) input sessions first.
            if (src.sink) {
                if (src.sink->pres_input_open)
                    pulse_data_session_disconnect(src.pulse, PULSE_MEDIA_VIDEO,
                                                  PULSE_MEDIA_INPUT,
                                                  PULSE_MEDIA_CONTENT_PRESENTATION);
                if (src.sink->main_input_open)
                    pulse_data_session_disconnect(src.pulse, PULSE_MEDIA_VIDEO,
                                                  PULSE_MEDIA_INPUT,
                                                  PULSE_MEDIA_CONTENT_MAIN);
                src.sink.reset();
            }
            pulse_options_set_conference_state_callback(src.pulse, nullptr);
            if (pulse_is_connected(src.pulse))
                pulse_disconnect(src.pulse, nullptr);
            break;
    }

    pulse_free(src.pulse);
    src.pulse = nullptr;

    if (src.texture) {
        glDeleteTextures(1, &src.texture);
        src.texture = 0;
    }
    src.tex_w = src.tex_h = 0;
    src.frame_cpu = RgbaImage{};
    src.conn_status.store(PULSE_CONNECTION_STATUS_DISCONNECTED);
    src.started = false;
}

// Remove every placement that references `id` from a canvas.
static void prune_placements(Canvas & c, int id)
{
    c.items.erase(std::remove_if(c.items.begin(), c.items.end(),
                                 [&](const Placement & p) { return p.source_id == id; }),
                  c.items.end());
    c.selected = -1;
}

// Stop a library source and erase it, first pruning its placements from every
// canvas (program plus every conference's send / presentation buses).
static void remove_source(AppState & app, int id)
{
    for (auto & s : app.library) {
        prune_placements(app.program, id);
        if (s->sink) {
            prune_placements(s->sink->send, id);
            prune_placements(s->sink->presentation, id);
        }
    }

    auto it = std::find_if(app.library.begin(), app.library.end(),
                           [&](const std::unique_ptr<ActiveSource> & s) { return s->id == id; });
    if (it == app.library.end()) return;

    stop_source(**it);
    app.library.erase(it);
    if (app.selected_source == id) app.selected_source = -1;
}

// ----------------------------------------------------------------------------
//  Outbound push - composite each conference's buses and push them in
// ----------------------------------------------------------------------------

static void pump_outbound(AppState & app)
{
    const double now = glfwGetTime();
    const double interval = 1.0 / 30.0;   // throttle the push path to ~30 fps

    for (auto & s : app.library) {
        if (s->kind != SourceKind::Conference || !s->sink || !s->started) continue;
        OutboundSink & sink = *s->sink;

        if (now - sink.last_push_time < interval) continue;
        sink.last_push_time = now;

        // The SEND bus -> MAIN input (what the far end sees instead of a camera).
        if (sink.main_input_open) {
            composite_canvas(app, sink.send, sink.main_buf);
            push_canvas(*s, PULSE_MEDIA_CONTENT_MAIN, sink.main_buf,
                        sink.last_main_w, sink.last_main_h);
        }

        // The PRESENTATION bus -> PRESENTATION input, only while it is "started".
        if (sink.present_active && sink.pres_input_open) {
            composite_canvas(app, sink.presentation, sink.pres_buf);
            push_canvas(*s, PULSE_MEDIA_CONTENT_PRESENTATION, sink.pres_buf,
                        sink.last_pres_w, sink.last_pres_h);
        }
    }
}

// Start / stop sending the presentation bus for a conference.
static void start_presentation(ActiveSource & conf)
{
    if (!conf.sink || conf.sink->present_active) return;
    PulseDataSessionConfig * icfg =
        make_rgba_input_config(conf.sink->presentation.w, conf.sink->presentation.h);
    PulseError err = pulse_data_session_connect_input(conf.pulse, icfg,
                                                      PULSE_MEDIA_CONTENT_PRESENTATION);
    pulse_data_session_config_free(icfg);
    if (err != PULSE_SUCCESS) {
        conf.last_error = std::string("start presentation failed: ") + pulse_strerror(err);
        return;
    }
    conf.sink->pres_input_open = true;
    conf.sink->present_active  = true;
    conf.sink->last_pres_w = conf.sink->presentation.w;
    conf.sink->last_pres_h = conf.sink->presentation.h;
}

static void stop_presentation(ActiveSource & conf)
{
    if (!conf.sink || !conf.sink->present_active) return;
    if (conf.sink->pres_input_open) {
        pulse_data_session_disconnect(conf.pulse, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT,
                                      PULSE_MEDIA_CONTENT_PRESENTATION);
        conf.sink->pres_input_open = false;
    }
    conf.sink->present_active = false;
}

// ----------------------------------------------------------------------------
//  Per-kind configuration UI (shared by the staging form and the library
//  inspector). Draws only the kind-specific fields.
// ----------------------------------------------------------------------------

static void draw_kind_config(AppState & app, ActiveSource & src)
{
    switch (src.kind) {
        case SourceKind::Camera: {
            std::string current = src.camera_name[0] ? src.camera_name : "(system default)";
            if (ImGui::BeginCombo("Camera", current.c_str())) {
                if (ImGui::Selectable("(system default)", src.camera_name[0] == '\0'))
                    src.camera_name[0] = '\0';
                for (const std::string & n : app.camera_names) {
                    bool sel = (std::strcmp(n.c_str(), src.camera_name) == 0);
                    if (ImGui::Selectable(n.c_str(), sel))
                        std::snprintf(src.camera_name, sizeof(src.camera_name), "%s", n.c_str());
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Rescan cameras")) refresh_camera_list(app);
            break;
        }
        case SourceKind::Microphone: {
            std::string current = src.microphone_name[0] ? src.microphone_name : "(system default)";
            if (ImGui::BeginCombo("Microphone", current.c_str())) {
                if (ImGui::Selectable("(system default)", src.microphone_name[0] == '\0'))
                    src.microphone_name[0] = '\0';
                for (const std::string & n : app.microphone_names) {
                    bool sel = (std::strcmp(n.c_str(), src.microphone_name) == 0);
                    if (ImGui::Selectable(n.c_str(), sel))
                        std::snprintf(src.microphone_name, sizeof(src.microphone_name), "%s", n.c_str());
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Rescan microphones")) refresh_microphone_list(app);
            break;
        }
        case SourceKind::Rtsp:
            ImGui::InputText("RTSP URL", src.rtsp_url, sizeof(src.rtsp_url));
            break;
        case SourceKind::Rtmp:
            ImGui::InputInt("Listen port", &src.rtmp_port);
            ImGui::InputText("Path", src.rtmp_path, sizeof(src.rtmp_path));
            ImGui::TextWrapped("Publish to rtmp://<host>:%d/%s", src.rtmp_port, src.rtmp_path);
            break;
        case SourceKind::Image:
            ImGui::InputText("Image path", src.file_path, sizeof(src.file_path));
            ImGui::SameLine();
            file_picker::browse_button("Browse...##img", src.file_path, sizeof(src.file_path),
                                       "Open image", ".jpg,.jpeg,.png,.bmp,.gif,.*");
            break;
        case SourceKind::Mp4:
            ImGui::InputText("MP4 path", src.file_path, sizeof(src.file_path));
            ImGui::SameLine();
            file_picker::browse_button("Browse...##mp4", src.file_path, sizeof(src.file_path),
                                       "Open MP4", ".mp4,.mov,.mkv,.webm,.*");
            ImGui::Checkbox("Loop", &src.loop);
            break;
        case SourceKind::Conference:
            ImGui::InputText("Conference id", src.conference_id, sizeof(src.conference_id));
            ImGui::InputText("Display name", src.display_name, sizeof(src.display_name));
            ImGui::InputText("PIN code", src.pin_code, sizeof(src.pin_code),
                             ImGuiInputTextFlags_Password);
            ImGui::TextWrapped("e.g. havard@pexipdemo.com -> conference \"havard\" on \"pexipdemo.com\". "
                               "Leave PIN empty if the conference has none.");
            break;
    }
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
//  Library rail (prepare / list / drag / stop sources)
// ----------------------------------------------------------------------------

// The drag-drop payload type carried from a library thumbnail to a canvas.
static const char * kSourcePayload = "WALL_SRC";

static void draw_library_rail(AppState & app)
{
    // ---- Prepare a source (the staging form) -------------------------------
    ImGui::SeparatorText("Prepare a source");
    if (ImGui::Combo("Kind", &app.staging_kind, kSourceKindLabels,
                     IM_ARRAYSIZE(kSourceKindLabels))) {
        app.staging.kind = static_cast<SourceKind>(app.staging_kind);
        // Each kind has a natural default media type (a microphone is audio, an
        // image is video, a conference is both); reset to it when the kind
        // changes so the routing makes sense out of the box.
        app.staging.media_type = default_media_type(app.staging.kind);
    }
    app.staging.kind = static_cast<SourceKind>(app.staging_kind);
    ImGui::InputText("Name", app.staging.name, sizeof(app.staging.name));

    // What is this source - audio, video, or both? This decides where it shows
    // up: audio in the mixer, video on the canvases, both in both.
    int media_idx = static_cast<int>(app.staging.media_type);
    if (ImGui::Combo("Media", &media_idx, kMediaTypeLabels, IM_ARRAYSIZE(kMediaTypeLabels)))
        app.staging.media_type = static_cast<MediaType>(media_idx);

    // Scope the per-kind widgets so they never collide with the identical
    // widgets the inspector draws for the selected source (otherwise preparing
    // a second source of the same kind trips Dear ImGui's duplicate-ID check).
    ImGui::PushID("staging");
    draw_kind_config(app, app.staging);
    ImGui::PopID();

    if (ImGui::Button("Prepare (start)")) {
        // Move the staging config into a fresh library entry and start it. Only
        // sources that come up successfully join the library.
        auto src = std::make_unique<ActiveSource>();
        ActiveSource & s = *src;
        s.kind = app.staging.kind;
        s.media_type = app.staging.media_type;
        std::snprintf(s.name, sizeof(s.name), "%s", app.staging.name);
        std::memcpy(s.camera_name,     app.staging.camera_name,     sizeof(s.camera_name));
        std::memcpy(s.microphone_name, app.staging.microphone_name, sizeof(s.microphone_name));
        std::memcpy(s.file_path,     app.staging.file_path,     sizeof(s.file_path));
        std::memcpy(s.rtsp_url,      app.staging.rtsp_url,      sizeof(s.rtsp_url));
        s.rtmp_port = app.staging.rtmp_port;
        std::memcpy(s.rtmp_path,     app.staging.rtmp_path,     sizeof(s.rtmp_path));
        std::memcpy(s.conference_id, app.staging.conference_id, sizeof(s.conference_id));
        std::memcpy(s.display_name,  app.staging.display_name,  sizeof(s.display_name));
        std::memcpy(s.pin_code,      app.staging.pin_code,      sizeof(s.pin_code));
        s.loop = app.staging.loop;
        s.id   = app.next_source_id++;

        start_source(s);
        if (s.started) {
            app.selected_source = s.id;
            app.library.push_back(std::move(src));
        } else {
            app.staging.last_error = s.last_error;
        }
    }
    if (!app.staging.last_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", app.staging.last_error.c_str());

    // ---- The library of active sources (drag onto any canvas) --------------
    ImGui::SeparatorText("Library (drag onto a canvas)");
    const float thumb_w = 110.0f, thumb_h = 62.0f;
    const float avail   = ImGui::GetContentRegionAvail().x;
    int per_row = std::max(1, static_cast<int>(avail / (thumb_w + 8.0f)));
    int col = 0;

    for (auto & up : app.library) {
        ActiveSource & s = *up;
        ImGui::PushID(s.id);

        // A live thumbnail doubling as the drag handle and selection toggle.
        // Audio-only sources have no picture, so show a labelled button instead;
        // only video-capable sources can be dragged onto a canvas.
        ImVec2 size(thumb_w, thumb_h);
        bool clicked;
        if (s.texture && s.tex_w > 0) {
            clicked = ImGui::ImageButton("thumb", (ImTextureID)(uintptr_t)s.texture, size,
                                         ImVec2(0, 0), ImVec2(1, 1));
        } else if (!source_has_video(s)) {
            clicked = ImGui::Button("(audio)", size);
        } else {
            clicked = ImGui::Button("(no signal)", size);
        }
        if (clicked) app.selected_source = s.id;

        // Only video-capable sources may be dragged onto a canvas; an audio-only
        // source lives solely in the mixer below.
        if (source_has_video(s) && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload(kSourcePayload, &s.id, sizeof(int));
            ImGui::Text("%s", s.name);
            ImGui::EndDragDropSource();
        }

        // Caption + status badge underneath the thumbnail.
        ImGui::TextUnformatted(s.name);
        if (s.kind == SourceKind::Conference)
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s",
                               status_to_string(s.conn_status.load()));
        else
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%s  (%s)",
                               kSourceKindLabels[static_cast<int>(s.kind)],
                               kMediaTypeLabels[static_cast<int>(s.media_type)]);

        ImGui::PopID();
        if (++col % per_row != 0) ImGui::SameLine();
    }
    if (col % per_row != 0) ImGui::NewLine();

    // ---- Inspector for the selected library source -------------------------
    ActiveSource * sel = (app.selected_source >= 0) ? find_source(app, app.selected_source) : nullptr;
    if (sel) {
        ImGui::SeparatorText("Selected source");
        ImGui::Text("%s  -  %s  (%s)", sel->name, kSourceKindLabels[static_cast<int>(sel->kind)],
                    kMediaTypeLabels[static_cast<int>(sel->media_type)]);
        ImGui::PushID("inspector");
        ImGui::BeginDisabled(true);
        draw_kind_config(app, *sel);
        ImGui::EndDisabled();
        ImGui::PopID();
        if (!sel->last_error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", sel->last_error.c_str());
        if (ImGui::Button("Stop & remove from library"))
            remove_source(app, sel->id);
    }
}

// ----------------------------------------------------------------------------
//  Canvas editing (shared by the Program wall and the conference buses)
// ----------------------------------------------------------------------------
//
//  Draws a scaled-down view of one canvas: composites every placement's texture
//  at its placement, accepts sources dragged from the library, and lets the user
//  click to select and drag tiles around. An inline inspector underneath drives
//  the selected placement's size, z-order, and removal.
// ----------------------------------------------------------------------------

static void draw_editable_canvas(AppState & app, Canvas & canvas, const char * id_str)
{
    ImGui::PushID(id_str);

    // Reserve a strip at the bottom for the selected-placement inspector so the
    // canvas itself takes the rest of the available height. The canvas then
    // scales to fit *both* width and height, so even a tall 1920x1080 wall is
    // fully visible at once - no scrolling.
    const float inspector_h = 96.0f;
    float region_h = ImGui::GetContentRegionAvail().y;
    float canvas_child_h = region_h - inspector_h;
    if (canvas_child_h < 120.0f) canvas_child_h = 120.0f;

    ImGui::BeginChild("canvas", ImVec2(0, canvas_child_h), true);

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float avail_h = ImGui::GetContentRegionAvail().y;
    // Fit-to-box: scale by whichever axis is the tighter fit so the whole canvas
    // shows. Then centre it within the leftover space.
    const float scale_w = (canvas.w > 0) ? (avail_w / static_cast<float>(canvas.w)) : 1.0f;
    const float scale_h = (canvas.h > 0) ? (avail_h / static_cast<float>(canvas.h)) : 1.0f;
    const float scale   = std::min(scale_w, scale_h);
    const ImVec2 canvas_px(canvas.w * scale, canvas.h * scale);

    const ImVec2 child_origin = ImGui::GetCursorScreenPos();
    const ImVec2 origin(child_origin.x + std::max(0.0f, (avail_w - canvas_px.x) * 0.5f),
                        child_origin.y + std::max(0.0f, (avail_h - canvas_px.y) * 0.5f));
    ImGui::SetCursorScreenPos(origin);

    ImDrawList * dl = ImGui::GetWindowDrawList();

    // A full-canvas invisible button: reserves the area, serves as the
    // drag-drop target for library sources, and deselects on an empty click.
    // SetNextItemAllowOverlap() lets the per-tile buttons submitted *after* it
    // still receive hover/clicks — without it this background button would swallow
    // every interaction and the tiles could not be moved or resized.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("bg", canvas_px);
    const bool bg_hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) canvas.selected = -1;

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload * pl = ImGui::AcceptDragDropPayload(kSourcePayload)) {
            int sid = *static_cast<const int *>(pl->Data);
            ActiveSource * s = find_source(app, sid);
            // Only video-capable sources belong on a canvas; an audio-only
            // source is ignored here (it lives in the mixer instead).
            if (s && source_has_video(*s)) {
                const ImVec2 m = ImGui::GetMousePos();
                Placement p;
                p.source_id = sid;
                p.x = (m.x - origin.x) / scale;
                p.y = (m.y - origin.y) / scale;
                p.w = 480.0f;
                p.h = 270.0f;
                // Adopt the source's native aspect ratio (so the resize handle
                // keeps it). Fall back to 16:9 until the first frame arrives.
                if (s->frame_cpu.w > 0 && s->frame_cpu.h > 0) {
                    p.aspect = static_cast<float>(s->frame_cpu.w) / s->frame_cpu.h;
                    p.h = p.w / p.aspect;
                }
                if (p.x < 0) p.x = 0;
                if (p.y < 0) p.y = 0;
                canvas.items.push_back(p);
                canvas.selected = static_cast<int>(canvas.items.size()) - 1;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // The canvas backdrop (drawn after reserving, so it sits under the tiles).
    dl->AddRectFilled(origin, ImVec2(origin.x + canvas_px.x, origin.y + canvas_px.y),
                      IM_COL32(20, 20, 24, 255));
    dl->AddRect(origin, ImVec2(origin.x + canvas_px.x, origin.y + canvas_px.y),
                bg_hovered ? IM_COL32(120, 160, 200, 255) : IM_COL32(90, 90, 110, 255));

    auto to_screen = [&](float cx, float cy) {
        return ImVec2(origin.x + cx * scale, origin.y + cy * scale);
    };

    for (int i = 0; i < static_cast<int>(canvas.items.size()); ++i) {
        Placement &    p   = canvas.items[i];
        ActiveSource * src = find_source(app, p.source_id);

        const ImVec2 tl = to_screen(p.x, p.y);
        const ImVec2 br = to_screen(p.x + p.w, p.y + p.h);

        if (src && src->texture && src->tex_w > 0) {
            dl->AddImage((ImTextureID)(uintptr_t)src->texture, tl, br);
        } else {
            dl->AddRectFilled(tl, br, IM_COL32(40, 40, 48, 255));
        }

        const bool selected = (i == canvas.selected);
        // Tally-style border: bright cyan when selected, dim otherwise.
        dl->AddRect(tl, br, selected ? IM_COL32(90, 200, 255, 255) : IM_COL32(120, 120, 140, 255),
                    0.0f, 0, selected ? 2.5f : 1.0f);
        dl->AddText(ImVec2(tl.x + 4, tl.y + 4), IM_COL32(230, 230, 230, 255),
                    src ? src->name : "(missing)");

        ImGui::SetCursorScreenPos(tl);
        ImGui::PushID(i);
        // Allow the resize handle (and higher-z tiles) submitted afterwards to
        // win the overlap and stay interactive.
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("tile", ImVec2(std::max(br.x - tl.x, 1.0f),
                                              std::max(br.y - tl.y, 1.0f)));
        if (ImGui::IsItemActivated()) canvas.selected = i;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            p.x += d.x / scale;
            p.y += d.y / scale;
            if (p.x < 0) p.x = 0;
            if (p.y < 0) p.y = 0;
            if (p.x > canvas.w - 1) p.x = static_cast<float>(canvas.w - 1);
            if (p.y > canvas.h - 1) p.y = static_cast<float>(canvas.h - 1);
        }

        // A bottom-right resize handle for the selected tile. Dragging it scales
        // the placement while preserving its aspect ratio.
        if (selected) {
            const float hsz = 14.0f;
            const ImVec2 hmin(br.x - hsz, br.y - hsz);
            dl->AddRectFilled(hmin, br, IM_COL32(90, 200, 255, 255));
            dl->AddRect(hmin, br, IM_COL32(20, 20, 24, 255));
            ImGui::SetCursorScreenPos(hmin);
            ImGui::InvisibleButton("resize", ImVec2(hsz, hsz));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const ImVec2 d = ImGui::GetIO().MouseDelta;
                float neww = p.w + d.x / scale;
                if (neww < 16.0f) neww = 16.0f;
                if (p.x + neww > canvas.w) neww = canvas.w - p.x;
                const float aspect = (p.aspect > 0.0f) ? p.aspect
                                   : (p.h > 0.0f ? p.w / p.h : 16.0f / 9.0f);
                p.w = neww;
                p.h = neww / aspect;
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        }
        ImGui::PopID();
    }

    ImGui::EndChild();

    // ---- inline inspector for the selected placement -----------------------
    if (canvas.selected >= 0 && canvas.selected < static_cast<int>(canvas.items.size())) {
        Placement & p = canvas.items[canvas.selected];
        ImGui::DragFloat2("Position (px)", &p.x, 1.0f);
        // Width drives the size; height follows from the locked aspect ratio.
        if (ImGui::DragFloat("Width (px)", &p.w, 1.0f, 16.0f, 100000.0f)) {
            if (p.w < 16.0f) p.w = 16.0f;
            const float aspect = (p.aspect > 0.0f) ? p.aspect
                               : (p.h > 0.0f ? p.w / p.h : 16.0f / 9.0f);
            p.h = p.w / aspect;
        }
        ImGui::Text("Size: %.0f x %.0f px (aspect locked)", p.w, p.h);

        // Z-order: last in the vector is drawn on top.
        if (ImGui::Button("Bring forward") &&
            canvas.selected < static_cast<int>(canvas.items.size()) - 1) {
            std::swap(canvas.items[canvas.selected], canvas.items[canvas.selected + 1]);
            canvas.selected += 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Send backward") && canvas.selected > 0) {
            std::swap(canvas.items[canvas.selected], canvas.items[canvas.selected - 1]);
            canvas.selected -= 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove from canvas")) {
            canvas.items.erase(canvas.items.begin() + canvas.selected);
            canvas.selected = -1;
        }
    }

    ImGui::PopID();
}

// ----------------------------------------------------------------------------
//  Audio mixer - a classic channel strip per library source
// ----------------------------------------------------------------------------
//
//  Each source feeds one vertical strip: a live VU meter (driven by Pulse's
//  input audio-level callback, see on_source_audio_level) sitting next to a gain
//  fader, a 3-band EQ of rotary knobs, and noise-suppression / mute / solo
//  switches. The meter is real; the knobs and switches are UI we can wire into
//  Pulse APIs later (noise suppression, an EQ, channel gain).
// ----------------------------------------------------------------------------

// Centre the next single-line item of width `item_w` within `avail_w`.
static void center_item(float item_w, float avail_w)
{
    float off = (avail_w - item_w) * 0.5f;
    if (off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
}

// A small rotary knob. Drag up/down to change. `caption` is drawn centred below.
static void mixer_knob(const char * caption, float * value, float vmin, float vmax,
                       float avail_w)
{
    const float radius   = 16.0f;
    const float diameter = radius * 2.0f;

    center_item(diameter, avail_w);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center(pos.x + radius, pos.y + radius);

    ImGui::InvisibleButton(caption, ImVec2(diameter, diameter));
    const bool active = ImGui::IsItemActive();
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const float range = vmax - vmin;
        *value += -ImGui::GetIO().MouseDelta.y * range / 180.0f;  // up = louder
        if (*value < vmin) *value = vmin;
        if (*value > vmax) *value = vmax;
    }
    if (ImGui::IsItemHovered() || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    const float t   = (vmax > vmin) ? (*value - vmin) / (vmax - vmin) : 0.0f;
    const float a0  = kPi * 0.75f;         // sweep from lower-left...
    const float a1  = kPi * 2.25f;         // ...round to lower-right
    const float ang = a0 + t * (a1 - a0);

    ImDrawList * dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(center, radius, IM_COL32(38, 38, 46, 255), 32);
    dl->AddCircle(center, radius,
                  active ? IM_COL32(90, 200, 255, 255) : IM_COL32(110, 110, 130, 255), 32, 2.0f);
    dl->AddLine(center, ImVec2(center.x + cosf(ang) * (radius - 3.0f),
                               center.y + sinf(ang) * (radius - 3.0f)),
                IM_COL32(235, 235, 235, 255), 2.0f);

    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s %+.0f", caption, *value);
    center_item(ImGui::CalcTextSize(buf).x, avail_w);
    ImGui::TextUnformatted(buf);
}

// Sample the freshest input level for `s`, smooth it, and paint a vertical VU
// meter `w` x `h` at the current cursor. Green below ~-12 dB, amber, then red.
static void mixer_vu_meter(ActiveSource & s, float w, float h)
{
    unsigned int dbi = 0;
    {
        std::lock_guard<std::mutex> lock(s.audio_mutex);
        if (!s.audio_levels.empty()) dbi = s.audio_levels.back();
    }
    const float dbf    = (dbi != 0) ? -static_cast<float>(dbi) : kMinAudioLevelDb;
    float       target = (dbf - kMinAudioLevelDb) / (kMaxAudioLevelDb - kMinAudioLevelDb);
    if (target < 0.0f) target = 0.0f;
    if (target > 1.0f) target = 1.0f;
    s.vu += (target - s.vu) * kVuMeterSmoothing;   // simple attack/decay smoothing

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList * dl = ImGui::GetWindowDrawList();

    const ImVec2 bmin = p;
    const ImVec2 bmax(p.x + w, p.y + h);
    dl->AddRectFilled(bmin, bmax, IM_COL32(20, 20, 24, 255));

    const float fill_h = h * s.vu;
    const ImVec2 fmin(bmin.x, bmax.y - fill_h);
    ImU32 col = IM_COL32(60, 200, 90, 255);          // green
    if (s.vu > 0.85f)      col = IM_COL32(220, 60, 60, 255);   // red
    else if (s.vu > 0.70f) col = IM_COL32(230, 200, 60, 255);  // amber
    dl->AddRectFilled(fmin, bmax, col);
    dl->AddRect(bmin, bmax, IM_COL32(110, 110, 130, 255));
}

// A chunky on/off switch styled as a coloured toggle button.
static bool mixer_switch(const char * label, bool * on, ImU32 on_col, float avail_w)
{
    ImGui::PushStyleColor(ImGuiCol_Button,
                          *on ? on_col : IM_COL32(50, 50, 58, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          *on ? on_col : IM_COL32(70, 70, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on_col);
    const float bw = avail_w;
    bool clicked = ImGui::Button(label, ImVec2(bw, 0));
    if (clicked) *on = !*on;
    ImGui::PopStyleColor(3);
    return clicked;
}

static void draw_audio_mixer(AppState & app)
{
    ImGui::TextWrapped("One channel strip per audio-capable source (Audio or Both). The VU meters "
                       "are live - they read each source's input audio levels via Pulse's "
                       "pulse_register_device_audio_level_callback, so a Microphone source shows "
                       "real activity. The gain fader, EQ knobs and the noise-suppression / mute / "
                       "solo switches are wired into the UI now so the matching Pulse APIs can be "
                       "plumbed in later.");
    ImGui::Separator();

    // Count audio-capable sources so we can show a helpful hint when there are
    // none (a video-only library has nothing to mix).
    int audio_sources = 0;
    for (auto & up : app.library)
        if (source_has_audio(*up)) ++audio_sources;

    if (audio_sources == 0) {
        ImGui::TextDisabled("No audio sources yet - prepare a Microphone (or an Audio/Both source) "
                            "from the library on the left.");
        return;
    }

    const float strip_w = 116.0f;
    ImGui::BeginChild("mixer_strips", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    int idx = 0;
    for (auto & up : app.library) {
        ActiveSource & s = *up;
        if (!source_has_audio(s)) continue;   // video-only sources are not mixed
        ImGui::PushID(s.id);
        if (idx++ > 0) ImGui::SameLine();

        ImGui::BeginChild("strip", ImVec2(strip_w, 0), true);
        const float inner = ImGui::GetContentRegionAvail().x;

        // ---- header ---------------------------------------------------------
        center_item(ImGui::CalcTextSize(s.name).x, inner);
        ImGui::TextUnformatted(s.name);
        const char * kind = kSourceKindLabels[static_cast<int>(s.kind)];
        center_item(ImGui::CalcTextSize(kind).x, inner);
        ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.85f, 1.0f), "%s", kind);
        ImGui::Separator();

        // ---- 3-band EQ ------------------------------------------------------
        center_item(ImGui::CalcTextSize("EQ").x, inner);
        ImGui::TextDisabled("EQ");
        mixer_knob("Hi",  &s.eq_high, -12.0f, 12.0f, inner);
        mixer_knob("Mid", &s.eq_mid,  -12.0f, 12.0f, inner);
        mixer_knob("Lo",  &s.eq_low,  -12.0f, 12.0f, inner);
        ImGui::Separator();

        // ---- noise suppression switch --------------------------------------
        mixer_switch(s.noise_suppression ? "NS: ON##ns" : "NS: off##ns",
                     &s.noise_suppression, IM_COL32(60, 160, 220, 255), inner);
        ImGui::Separator();

        // ---- VU meter + gain fader, side by side ---------------------------
        const float meter_h = 150.0f;
        const float meter_w = 18.0f;
        const float fader_w = 22.0f;
        center_item(meter_w + 8.0f + fader_w, inner);
        mixer_vu_meter(s, meter_w, meter_h);
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::VSliderFloat("##gain", ImVec2(fader_w, meter_h), &s.gain_db,
                            -60.0f, 12.0f, "");
        char gbuf[32];
        std::snprintf(gbuf, sizeof(gbuf), "%+.0f dB", s.gain_db);
        center_item(ImGui::CalcTextSize(gbuf).x, inner);
        ImGui::TextUnformatted(gbuf);
        ImGui::Separator();

        // ---- mute / solo ----------------------------------------------------
        const float half = (inner - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        mixer_switch("M##mute", &s.mute, IM_COL32(220, 80, 80, 255), half);
        ImGui::SameLine();
        mixer_switch("S##solo", &s.solo, IM_COL32(220, 190, 60, 255), half);

        ImGui::EndChild();
        ImGui::PopID();
    }

    ImGui::EndChild();
}

// ----------------------------------------------------------------------------
//  The tabbed, switcher-style canvas area
// ----------------------------------------------------------------------------

static void draw_canvas_tabs(AppState & app)
{
    if (!ImGui::BeginTabBar("buses")) return;

    // ---- Program (Wall) tab ------------------------------------------------
    if (ImGui::BeginTabItem("Program (Wall)")) {
        ImGui::InputInt("Width",  &app.program.w);
        ImGui::InputInt("Height", &app.program.h);
        if (app.program.w < 16) app.program.w = 16;
        if (app.program.h < 16) app.program.h = 16;
        ImGui::Text("%d x %d - drag sources from the library, then move/resize them",
                    app.program.w, app.program.h);
        draw_editable_canvas(app, app.program, "program");
        ImGui::EndTabItem();
    }

    // ---- One tab per dialled-in conference: its Send / Presentation buses ---
    for (auto & up : app.library) {
        ActiveSource & s = *up;
        if (s.kind != SourceKind::Conference || !s.sink) continue;

        char tab[96];
        std::snprintf(tab, sizeof(tab), "%s (far end)###conf%d", s.name, s.id);
        if (!ImGui::BeginTabItem(tab)) continue;

        OutboundSink & sink = *s.sink;
        const bool connected = (s.conn_status.load() == PULSE_CONNECTION_STATUS_CONNECTED);

        ImGui::Text("Status: %s", status_to_string(s.conn_status.load()));
        ImGui::SameLine();
        // The "show both canvases" control: light up the presentation bus.
        ImGui::BeginDisabled(!connected);
        if (!sink.present_active) {
            if (ImGui::Button("Start presentation")) start_presentation(s);
        } else {
            if (ImGui::Button("Stop presentation")) stop_presentation(s);
        }
        ImGui::EndDisabled();
        if (!connected)
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f),
                               "Connect to the conference before sending media.");

        // The SEND bus - exactly what this far end receives.
        ImGui::SeparatorText("Send bus (what they see)");
        // Guard: warn if a conference is feeding its own far-end back to itself.
        for (const Placement & p : sink.send.items) {
            if (p.source_id == s.id) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                                   "Warning: this conference's own far-end is on its send bus "
                                   "(video feedback loop).");
                break;
            }
        }
        draw_editable_canvas(app, sink.send, "send");

        // The PRESENTATION bus - only meaningful while presentation is active.
        if (sink.present_active) {
            ImGui::SeparatorText("Presentation bus");
            draw_editable_canvas(app, sink.presentation, "pres");
        }

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}

// ----------------------------------------------------------------------------
//  The stage: canvas area on top, audio mixer underneath, a draggable splitter
//  between them. The audio mixer used to be a tab; it now lives below the
//  canvas so you can watch the wall and ride the faders at the same time, and
//  the splitter lets you trade height between the two to taste.
// ----------------------------------------------------------------------------

static void draw_stage(AppState & app)
{
    const float splitter_h = 8.0f;
    const float min_canvas  = 300.0f;   // keep the canvas + its inspector usable
    const float min_mixer   = 90.0f;    // keep at least the meters visible

    const float avail_h = ImGui::GetContentRegionAvail().y;

    // Clamp the mixer's share so neither pane collapses.
    float max_mixer = avail_h - splitter_h - min_canvas;
    if (max_mixer < min_mixer) max_mixer = min_mixer;
    if (app.mixer_height < min_mixer) app.mixer_height = min_mixer;
    if (app.mixer_height > max_mixer) app.mixer_height = max_mixer;

    const float canvas_h = avail_h - splitter_h - app.mixer_height;

    // ---- top: the canvas tabs ----------------------------------------------
    ImGui::BeginChild("canvas_pane", ImVec2(0, canvas_h), false);
    draw_canvas_tabs(app);
    ImGui::EndChild();

    // ---- the draggable splitter --------------------------------------------
    ImGui::InvisibleButton("vsplitter", ImVec2(-1.0f, splitter_h));
    const bool split_active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered() || split_active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (split_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        // Dragging the bar down shrinks the mixer; dragging up grows it.
        app.mixer_height -= ImGui::GetIO().MouseDelta.y;
        if (app.mixer_height < min_mixer) app.mixer_height = min_mixer;
        if (app.mixer_height > max_mixer) app.mixer_height = max_mixer;
    }
    {
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        const float  cy = (p0.y + p1.y) * 0.5f;
        ImU32 col = split_active ? IM_COL32(90, 200, 255, 255) : IM_COL32(90, 90, 110, 255);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p0.x, cy), ImVec2(p1.x, cy), col, 2.0f);
    }

    // ---- bottom: the audio mixer -------------------------------------------
    ImGui::BeginChild("mixer_pane", ImVec2(0, app.mixer_height), true);
    ImGui::SeparatorText("Audio Mixer");
    draw_audio_mixer(app);
    ImGui::EndChild();
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
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow * window = glfwCreateWindow(1600, 900, "videowall - Pulse production switcher",
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

    // Install the logger before the first pulse_new() so we catch early chatter.
    pulse_global_logger_callback(on_pulse_log, nullptr);

    AppState app;
    app.staging.kind = SourceKind::Camera;
    app.staging.media_type = default_media_type(SourceKind::Camera);
    std::snprintf(app.staging.name, sizeof(app.staging.name), "Source 1");
    // A throwaway Pulse instance whose only job is to enumerate capture devices
    // (cameras + microphones) for the UI. Real media always flows through the
    // per-source instances.
    app.enum_pulse = pulse_new();
    if (app.enum_pulse) {
        refresh_camera_list(app);
        refresh_microphone_list(app);
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 1. Pull the freshest frame out of every library source (updates both
        //    the GL thumbnails and the CPU buffers the compositor reads).
        for (auto & s : app.library) pump_frame(*s);

        // 2. Composite each conference's buses and push them back in.
        pump_outbound(app);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport * vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("videowall", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        // Left: the source library. Right: the switcher buses.
        ImGui::BeginChild("rail", ImVec2(380, 0), true);
        draw_library_rail(app);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("stage", ImVec2(0, 0), false);
        draw_stage(app);
        ImGui::EndChild();

        ImGui::End();

        // The file picker is a modal popup; render it at the top level (outside
        // the main window's Begin/End) so its ID stack is stable.
        file_picker::render_pending();

        ImGui::Render();
        int display_w = 0, display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Tear every source (and its Pulse instance) down cleanly.
    for (auto & src : app.library)
        stop_source(*src);
    app.library.clear();
    if (app.enum_pulse) {
        pulse_free(app.enum_pulse);
        app.enum_pulse = nullptr;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
