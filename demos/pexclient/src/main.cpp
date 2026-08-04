// ----------------------------------------------------------------------------
//  pexclient — a Pexip Infinity home-screen client, styled per the
//  "Dark Frosted" design handoff (design_handoff_pexninja_refresh/README.md).
//
//  What it does:
//    * registers to Infinity as a device (DNS _pexapp._tcp SRV via Pulse),
//    * search-or-dial bar with registrar alias autocomplete
//      (pulse_registrations_query_alias — devices and services),
//    * favourites strip + recent-calls panel, persisted to a config file,
//    * dials with pulse_connect_with_rest_async and renders the call itself
//      (remote MAIN full-window + rounded self-view, doppler's data-session
//      pattern).
//
//  Layout follows the handoff's variant 01: search row → favourites →
//  two glass panels (meetings placeholder / recents) → registration footer,
//  over the 135° gradient-mesh background. All chrome is custom-drawn with
//  the ImGui draw list; only text inputs and scroll regions are stock widgets.
// ----------------------------------------------------------------------------

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <pexpulse/pulse.h>
#include <pexpulse/pulse_conference_control.h>
#include <pexpulse/pulse_data_session.h>
#include <pexpulse/pulse_device.h>
#include <pexpulse/pulse_device_session.h>
#include <pexpulse/pulse_options.h>
#include <pexpulse/pulse_participant_control.h>
#include <pexpulse/pulse_registrations.h>
#include <pexpulse/pulse_video_mix_input.h>
#include <pexpulse/pulse_video_mix_session.h>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>   // DWMWA_CLOAKED — filter ghost UWP windows out of the share picker
#include <mmsystem.h> // PlaySound — incoming-call ring
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "Theme.h"

#ifndef PEXCLIENT_ASSET_DIR
#define PEXCLIENT_ASSET_DIR "."
#endif

// ----------------------------------------------------------------------------
//  Model
// ----------------------------------------------------------------------------

struct Contact
{
  std::string name;    // display label (usually the alias' local part)
  std::string address; // full alias, e.g. desk@erm.onpexip.com
};

struct RecentCall
{
  std::string name;
  std::string address;
  std::string timestamp; // "10:09"
  std::string duration;  // "02:45"
  bool starred = false;
};

struct AliasHit
{
  std::string alias;
  std::string description;
  bool is_device = false;
};

struct DevEntry
{
  PulseDeviceID id = 0;
  std::string name;
  bool is_default = false;
};

struct Config
{
  char display_name[128] = "pexclient";
  // Preferred device names ("" = system default). Names, not IDs — IDs are
  // transient per-run handles, names survive restarts and re-plugs.
  std::string dev_camera, dev_mic, dev_speaker;
  char reg_host[256] = "";
  char reg_alias[256] = "";
  char reg_user[256] = "";
  char reg_pass[256] = "";
  bool reg_auto = false;
  bool reg_sso = false; // authenticate registration via SSO instead of password
  float ui_scale = 1.2f;  // 1.0 = the design handoff's literal type scale

  // Video pipeline. bg_mode: 0 = off, 1 = blur, 2 = replace with bg_image.
  // content_mode: 0 = dual stream (content and camera as separate streams),
  // 1 = composition (camera composited into the content stream as a PiP).
  int bg_mode = 0;
  std::string bg_image;
  int content_mode = 0;
  float pip_size = 0.25f;  // PiP width as a fraction of the frame
  int pip_corner = 3;      // 0=TL 1=TR 2=BL 3=BR
  char default_server[256] = ""; // used for bare aliases when not registered

  std::vector<Contact> favorites;
  std::vector<RecentCall> recents;
};

struct GLVideoCtx
{
  GLuint texture = 0;
  PulseMediaContent content = PULSE_MEDIA_CONTENT_MAIN;
  int width = 0;
  int height = 0;
};

struct App
{
  Pulse * pulse = nullptr;
  GLFWwindow * window = nullptr;
  Config cfg;

  theme::Fonts fonts;
  GLuint bg_texture = 0;     // gradient-mesh background
  GLuint avatar_texture = 0; // accent→violet 135° gradient (avatar fallback)

  // Written from Pulse worker threads, read by the UI loop.
  std::atomic<int> reg_status{PULSE_CONNECTION_STATUS_DISCONNECTED};
  std::atomic<int> call_status{PULSE_CONNECTION_STATUS_DISCONNECTED};
  std::mutex text_mutex;
  std::string status_text; // transient footer message (progress / errors)
  std::atomic<bool> status_is_error{false};

  // Dial bar.
  char search[512] = "";
  std::vector<AliasHit> hits;
  std::string last_query = "\x01"; // never matches, forces first refresh

  // Live call bookkeeping.
  std::string call_address;
  std::chrono::steady_clock::time_point call_started;
  bool call_was_connected = false;
  GLVideoCtx remote_ctx, self_ctx;

  bool show_settings = false;
  bool devices_connected = false;

  // Incoming-call handshake. Pulse's incoming callback BLOCKS its worker
  // thread until we answer, so the callback parks on `pending` and polls
  // `answer` while the UI shows an accept/decline banner.
  std::atomic<bool> incoming_pending{false};
  std::atomic<int> incoming_answer{-1}; // -1 undecided, 0 decline, 1 accept
  std::mutex incoming_mutex;
  std::string incoming_remote, incoming_alias;

  // PIN-request handshake — same parked-callback pattern as incoming calls.
  std::atomic<bool> pin_pending{false};
  std::atomic<int> pin_answer{-1}; // -1 undecided, 0 cancel, 1 join
  std::atomic<bool> pin_guest_required{false};
  std::mutex pin_mutex;
  std::string pin_value;

  // Local media toggles. Valid both before a call (carried into it) and live.
  bool mic_muted = false;
  bool cam_muted = false;

  // Media device selection.
  bool show_devices = false;
  std::vector<DevEntry> cameras, mics, speakers;
  std::atomic<bool> device_list_dirty{true}; // set by hot-plug callbacks

  // Presentation / content share.
  struct ShareSource
  {
    uint64_t handle = 0;
    std::string name;
    bool is_display = false;
  };
  bool show_share = false;
  std::vector<ShareSource> share_sources;
  bool presenting = false;
  PulseVideoMixInputID share_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
  App::ShareSource share_src;      // remembered so the mix can be rebuilt
  // Compositor inputs shared between the MAIN and PRESENTATION mixes.
  PulseVideoMixInputID camera_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
  PulseVideoMixInputID bg_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
  PulseDevice * camera_device = nullptr; // owned copy for the mix input
  bool main_mix_active = false;
  std::atomic<bool> remote_presenting{false}; // far end is presenting to us
  GLVideoCtx preso_ctx;

  // Roster. The list is handed to us by the participant_list_updated
  // callback (Pulse thread) and owned by us until the next update; the UI
  // reads it under the same mutex every frame.
  std::mutex roster_mutex;
  PulseConferenceEventParticipantList * roster = nullptr;
  bool show_roster = false;
  float roster_slide = 0.0f; // 0 = hidden, 1 = fully open

  // Chat.
  struct ChatMsg
  {
    std::string origin, text, time;
    bool direct = false;
    bool self = false;
  };
  std::mutex chat_mutex;
  std::vector<ChatMsg> chat;
  bool show_chat = false;
  float chat_slide = 0.0f;
  std::atomic<int> chat_unread{0};
  bool chat_scroll_to_bottom = false;
  char chat_input[1024] = "";

  // Direct-chat recipient ("" = Everyone / broadcast). UI-thread only.
  std::string chat_target_uuid, chat_target_name;

  // Conference controls.
  bool show_conf_controls = false;
  std::atomic<bool> conf_locked{false};
  std::atomic<bool> conf_guests_muted{false};
  std::atomic<bool> conf_guests_can_unmute{true};
  std::mutex layout_mutex;
  std::string current_layout;                // from layout events
  std::vector<std::string> available_layouts; // fetched when the panel opens
  char dial_out_addr[512] = "";
  int dial_out_role = 1; // 0 = Host, 1 = Guest
  float disconnect_confirm_until = 0.0f; // two-step Disconnect-all arming

  // Live captions.
  bool captions_on = false;
  std::atomic<bool> captions_available{false};
  std::mutex caption_mutex;
  std::string caption_text;
  std::chrono::steady_clock::time_point caption_at{};

  // Floating self-view. Position is normalised (0..1) over the window's
  // usable area so it stays put proportionally across resizes; persists
  // across calls for the session.
  bool selfview_hidden = false;
  ImVec2 selfview_pos = ImVec2 (1.0f, 1.0f); // default: bottom-right
  bool selfview_drag_active = false;

  // Set when a call is starting and the capture devices must be (re)attached.
  // Always actioned on the UI thread — Pulse's blocking callbacks run on
  // worker threads, and attaching a camera from there races the media setup.
  std::atomic<bool> want_devices{false};

  // A registration is in flight. reg_status only moves once Pulse's state
  // callback fires, which is too late to stop a second Register click from
  // hitting the handle while the first is still running.
  std::atomic<bool> reg_in_flight{false};

  // Incoming-call alerting: ring tone + window attention, driven from the
  // UI thread (GLFW window calls must not come from a Pulse worker).
  bool ringing = false;
  double ring_next_at = 0.0;

  // Virtual reception (IVR): Infinity asks for the conference extension to
  // route to. Parked-callback handshake, like PIN/incoming/SSO.
  std::atomic<bool> ext_pending{false};
  std::atomic<int> ext_answer{-1}; // -1 undecided, 0 cancel, 1 submit
  std::mutex ext_mutex;
  std::string ext_value;

  // DTMF keypad.
  bool show_keypad = false;
  std::string dtmf_sent; // local echo of what we've sent this call

  // SSO provider selection — parked-callback handshake, like PIN/incoming.
  // sso_answer: -2 undecided, -1 cancel, >=0 chosen provider index.
  std::atomic<bool> sso_pending{false};
  std::atomic<int> sso_answer{-2};
  std::mutex sso_mutex;
  std::vector<std::string> sso_providers;
};

static void
set_status (App & app, std::string text, bool is_error = false)
{
  std::lock_guard<std::mutex> lock (app.text_mutex);
  app.status_text = std::move (text);
  app.status_is_error.store (is_error);
}

static std::string
get_status (App & app)
{
  std::lock_guard<std::mutex> lock (app.text_mutex);
  return app.status_text;
}

// ----------------------------------------------------------------------------
//  Config persistence — one line per key, pipe-separated lists.
// ----------------------------------------------------------------------------

static const char * kConfigFile = "pexclient-config.txt";

static void
copy_field (char * dst, size_t cap, const std::string & src)
{
  size_t n = std::min (cap - 1, src.size ());
  memcpy (dst, src.data (), n);
  dst[n] = '\0';
}

static void
load_config (Config & cfg)
{
  std::ifstream ifs (kConfigFile);
  std::string line;
  while (std::getline (ifs, line)) {
    std::size_t eq = line.find ('=');
    if (eq == std::string::npos)
      continue;
    std::string key = line.substr (0, eq), value = line.substr (eq + 1);

    if (key == "display_name")
      copy_field (cfg.display_name, sizeof (cfg.display_name), value);
    else if (key == "reg_host")
      copy_field (cfg.reg_host, sizeof (cfg.reg_host), value);
    else if (key == "reg_alias")
      copy_field (cfg.reg_alias, sizeof (cfg.reg_alias), value);
    else if (key == "reg_user")
      copy_field (cfg.reg_user, sizeof (cfg.reg_user), value);
    else if (key == "reg_pass")
      copy_field (cfg.reg_pass, sizeof (cfg.reg_pass), value);
    else if (key == "reg_auto")
      cfg.reg_auto = (value == "true");
    else if (key == "reg_sso")
      cfg.reg_sso = (value == "true");
    else if (key == "ui_scale")
      cfg.ui_scale = std::min (2.0f, std::max (0.8f, (float) atof (value.c_str ())));
    else if (key == "default_server")
      copy_field (cfg.default_server, sizeof (cfg.default_server), value);
    else if (key == "dev_camera")
      cfg.dev_camera = value;
    else if (key == "dev_mic")
      cfg.dev_mic = value;
    else if (key == "dev_speaker")
      cfg.dev_speaker = value;
    else if (key == "bg_mode")
      cfg.bg_mode = atoi (value.c_str ());
    else if (key == "bg_image")
      cfg.bg_image = value;
    else if (key == "content_mode")
      cfg.content_mode = atoi (value.c_str ());
    else if (key == "pip_size")
      cfg.pip_size = (float) atof (value.c_str ());
    else if (key == "pip_corner")
      cfg.pip_corner = atoi (value.c_str ());
    else if (key == "favorite") {
      std::size_t bar = value.find ('|');
      if (bar != std::string::npos)
        cfg.favorites.push_back ({value.substr (0, bar), value.substr (bar + 1)});
    } else if (key == "recent") {
      // name|address|timestamp|duration|starred
      std::vector<std::string> parts;
      std::stringstream ss (value);
      std::string part;
      while (std::getline (ss, part, '|'))
        parts.push_back (part);
      if (parts.size () == 5)
        cfg.recents.push_back ({parts[0], parts[1], parts[2], parts[3], parts[4] == "1"});
    }
  }
}

static void
save_config (const Config & cfg)
{
  std::ofstream ofs (kConfigFile, std::ios::trunc);
  ofs << "display_name=" << cfg.display_name << "\n";
  ofs << "reg_host=" << cfg.reg_host << "\n";
  ofs << "reg_alias=" << cfg.reg_alias << "\n";
  ofs << "reg_user=" << cfg.reg_user << "\n";
  ofs << "reg_pass=" << cfg.reg_pass << "\n";
  ofs << "reg_auto=" << (cfg.reg_auto ? "true" : "false") << "\n";
  ofs << "reg_sso=" << (cfg.reg_sso ? "true" : "false") << "\n";
  ofs << "ui_scale=" << cfg.ui_scale << "\n";
  ofs << "default_server=" << cfg.default_server << "\n";
  ofs << "dev_camera=" << cfg.dev_camera << "\n";
  ofs << "dev_mic=" << cfg.dev_mic << "\n";
  ofs << "dev_speaker=" << cfg.dev_speaker << "\n";
  ofs << "bg_mode=" << cfg.bg_mode << "\n";
  ofs << "bg_image=" << cfg.bg_image << "\n";
  ofs << "content_mode=" << cfg.content_mode << "\n";
  ofs << "pip_size=" << cfg.pip_size << "\n";
  ofs << "pip_corner=" << cfg.pip_corner << "\n";
  for (const Contact & f : cfg.favorites)
    ofs << "favorite=" << f.name << "|" << f.address << "\n";
  for (const RecentCall & r : cfg.recents)
    ofs << "recent=" << r.name << "|" << r.address << "|" << r.timestamp << "|" << r.duration << "|"
        << (r.starred ? "1" : "0") << "\n";
}

// ----------------------------------------------------------------------------
//  Small helpers
// ----------------------------------------------------------------------------

static std::string
local_part (const std::string & address)
{
  std::size_t at = address.find ('@');
  return at == std::string::npos ? address : address.substr (0, at);
}

static std::string
initials_of (const std::string & name)
{
  std::string out;
  bool take = true;
  for (char ch : name) {
    if (take && isalnum ((unsigned char) ch)) {
      out += (char) toupper ((unsigned char) ch);
      take = false;
      if (out.size () == 2)
        break;
    }
    if (ch == ' ' || ch == '.' || ch == '-' || ch == '_')
      take = true;
  }
  if (out.empty ())
    out = "?";
  return out;
}

static std::string
now_hhmm ()
{
  std::time_t t = std::time (nullptr);
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s (&tm_buf, &t); // MSVC spells the reentrant variant differently (and swaps the args)
#else
  localtime_r (&t, &tm_buf);
#endif
  char buf[8];
  std::strftime (buf, sizeof (buf), "%H:%M", &tm_buf);
  return buf;
}

static bool
is_favorite (const Config & cfg, const std::string & address)
{
  for (const Contact & f : cfg.favorites)
    if (f.address == address)
      return true;
  return false;
}

static void
toggle_favorite (Config & cfg, const std::string & name, const std::string & address)
{
  for (size_t i = 0; i < cfg.favorites.size (); i++) {
    if (cfg.favorites[i].address == address) {
      cfg.favorites.erase (cfg.favorites.begin () + i);
      for (RecentCall & r : cfg.recents)
        if (r.address == address)
          r.starred = false;
      save_config (cfg);
      return;
    }
  }
  cfg.favorites.push_back ({name, address});
  for (RecentCall & r : cfg.recents)
    if (r.address == address)
      r.starred = true;
  save_config (cfg);
}

// ----------------------------------------------------------------------------
//  Pulse callbacks — keep them short; they run on Pulse worker threads.
// ----------------------------------------------------------------------------

static void
on_reg_status (const PulseRegistrationStatusInfo * info, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  app->reg_status.store ((int) info->status);
}

static void
on_call_status (const PulseConferenceStatusInfo * info, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  app->call_status.store ((int) info->status);
}

static void
on_async_result (const PulseError err, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  if (err != PULSE_SUCCESS)
    std::fprintf (stderr, "[pexclient] async result: %s\n", pulse_strerror (err));
  set_status (*app, err == PULSE_SUCCESS ? "" : pulse_strerror (err), err != PULSE_SUCCESS);
}

// Registration results: clear the in-flight guard and translate the one error
// whose wording explains nothing to a user.
static void
on_register_result (const PulseError err, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  app->reg_in_flight.store (false);
  if (err == PULSE_SUCCESS) {
    set_status (*app, "");
    return;
  }
  std::fprintf (stderr, "[pexclient] registration failed: %s\n", pulse_strerror (err));
  if (err == PULSE_ERROR_HANDLE_IN_USE)
    set_status (*app, "Registration busy — is another pexclient already running?", true);
  else
    set_status (*app, pulse_strerror (err), true);
}

static void
on_progress (const PulseOperationProgressInfo * info, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  set_status (*app, info->desc ? info->desc : "");
}

static void connect_default_devices (App & app);
static void rebuild_video (App & app);

// --- Incoming-call alerting -------------------------------------------------
//
// Pulse has no ringtone of its own, so play a macOS system sound. It is
// fire-and-forget and short, so "ringing" is a replay on a timer rather than a
// loop we stop — which also means an in-flight ring never outlives the call.

#if defined(__APPLE__)
static SystemSoundID
ring_sound ()
{
  static SystemSoundID sound = 0;
  static bool tried = false;
  if (!tried) {
    tried = true;
    CFURLRef url = CFURLCreateWithFileSystemPath (kCFAllocatorDefault, CFSTR ("/System/Library/Sounds/Funk.aiff"),
                                                  kCFURLPOSIXPathStyle, false);
    if (url) {
      if (AudioServicesCreateSystemSoundID (url, &sound) != kAudioServicesNoError)
        sound = 0;
      CFRelease (url);
    }
  }
  return sound;
}

static void
play_ring ()
{
  SystemSoundID s = ring_sound ();
  if (s != 0)
    AudioServicesPlaySystemSound (s);
  else
    AudioServicesPlayAlertSound (kSystemSoundID_UserPreferredAlert);
}
#elif defined(_WIN32)
static void
play_ring ()
{
  // Same fire-and-forget model as the macOS path: one short system chime per
  // call, retriggered by the ring timer. Async so the UI thread never blocks.
  PlaySoundW (L"SystemExclamation", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
}
#else
static void
play_ring ()
{
}
#endif

// Ring, bounce the Dock icon and raise the window while a call is pending.
// Called every frame from the UI thread.
// Attach capture devices when a call asked for them (incoming accept), on the
// UI thread.
static void
update_devices_request (App & app)
{
  if (app.want_devices.exchange (false))
    connect_default_devices (app);
}

static void
update_ringing (App & app)
{
  bool pending = app.incoming_pending.load ();
  double now = ImGui::GetTime ();

  if (pending && !app.ringing) {
    app.ringing = true;
    app.ring_next_at = 0.0; // ring immediately

    // Bring the client forward so the banner is actually seen. Dock-bounce
    // (persistent) plus a raise; GLFW routes both to the Cocoa equivalents.
    glfwRequestWindowAttention (app.window);
    if (glfwGetWindowAttrib (app.window, GLFW_ICONIFIED))
      glfwRestoreWindow (app.window);
    glfwShowWindow (app.window);
    glfwFocusWindow (app.window);
  } else if (!pending && app.ringing) {
    app.ringing = false;
  }

  if (app.ringing && now >= app.ring_next_at) {
    play_ring ();
    app.ring_next_at = now + 2.6; // roughly a telephone cadence
  }
}

// Incoming call: park this Pulse worker thread on the UI's answer. Returning
// true accepts the call (Pulse then runs the normal connect flow using the
// result/progress callbacks we fill in).
static bool
on_incoming (const PulseRegistrationsEventIncoming * event, void * user_context,
             PulseAsyncOperationResultCallbackConfig * result_cb, PulseOperationProgressCallbackConfig * progress_cb)
{
  auto * app = static_cast<App *> (user_context);
  {
    std::lock_guard<std::mutex> lock (app->incoming_mutex);
    app->incoming_remote =
      event->remote_display_name && event->remote_display_name[0] ? event->remote_display_name : event->remote_alias;
    app->incoming_alias = event->conference_alias ? event->conference_alias : "";
  }
  app->incoming_answer.store (-1);
  app->incoming_pending.store (true);

  while (app->incoming_answer.load () == -1)
    std::this_thread::sleep_for (std::chrono::milliseconds (100));

  app->incoming_pending.store (false);
  bool accept = app->incoming_answer.load () == 1;
  if (accept) {
    app->want_devices.store (true); // UI thread attaches them; see update_devices_request
    result_cb->func = on_async_result;
    result_cb->user_context = app;
    progress_cb->func = on_progress;
    progress_cb->user_context = app;
    std::lock_guard<std::mutex> lock (app->incoming_mutex);
    app->call_address = app->incoming_alias;
    app->call_was_connected = false;
  }
  return accept;
}

static void
on_incoming_cancelled (const PulseRegistrationsEventIncomingCancelled *, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  if (app->incoming_pending.load ())
    app->incoming_answer.store (0); // releases the parked callback as a decline
}

// Virtual reception: Infinity is asking which conference to route us to. Park
// the worker until the UI collects an extension. Like the PIN callback, the
// setter MUST be invoked before returning.
static bool
on_conference_extension (const PulseSetConferenceExtension * set_ext, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  {
    std::lock_guard<std::mutex> lock (app->ext_mutex);
    app->ext_value.clear ();
  }
  app->ext_answer.store (-1);
  app->ext_pending.store (true);

  while (app->ext_answer.load () == -1)
    std::this_thread::sleep_for (std::chrono::milliseconds (100));

  app->ext_pending.store (false);
  bool submit = app->ext_answer.load () == 1;
  if (submit) {
    std::lock_guard<std::mutex> lock (app->ext_mutex);
    set_ext->func (set_ext->context, app->ext_value.c_str ());
  }
  std::fprintf (stderr, "[pexclient] conference extension: %s\n", submit ? "submitted" : "cancelled");
  return submit;
}

// PIN request: park the Pulse worker until the UI collects a PIN. The
// PulseSetPinCode function MUST be invoked before this callback returns (and
// is invalid in any other context) — hence the blocking shape.
static bool
on_pin_request (bool guest_pin_required, const PulseSetPinCode * set_pin, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  std::fprintf (stderr, "[pexclient] pin request: guest_pin_required=%d\n", guest_pin_required);
  {
    std::lock_guard<std::mutex> lock (app->pin_mutex);
    app->pin_value.clear ();
  }
  app->pin_guest_required.store (guest_pin_required);
  app->pin_answer.store (-1);
  app->pin_pending.store (true);

  while (app->pin_answer.load () == -1)
    std::this_thread::sleep_for (std::chrono::milliseconds (100));

  app->pin_pending.store (false);
  bool join = app->pin_answer.load () == 1;
  if (join) {
    std::lock_guard<std::mutex> lock (app->pin_mutex);
    // NULL means "no PIN" — valid for guests when a guest PIN isn't required.
    bool set_ok = set_pin->func (set_pin->context, app->pin_value.empty () ? nullptr : app->pin_value.c_str ());
    std::fprintf (stderr, "[pexclient] pin answered: join=1 len=%zu set_ok=%d\n", app->pin_value.size (), set_ok);
  } else {
    std::fprintf (stderr, "[pexclient] pin answered: join=0 (cancelled)\n");
  }
  return join;
}

// ----------------------------------------------------------------------------
//  Pulse actions
// ----------------------------------------------------------------------------

static void
start_register (App & app)
{
  if (app.cfg.reg_host[0] == '\0' || app.cfg.reg_alias[0] == '\0') {
    set_status (app, "Fill in registration host and alias in Settings.");
    return;
  }
  PulseRegistrationRequest req{};
  req.host = app.cfg.reg_host;
  req.alias = app.cfg.reg_alias;
  req.username = !app.cfg.reg_sso && app.cfg.reg_user[0] ? app.cfg.reg_user : nullptr;
  req.password = !app.cfg.reg_sso && app.cfg.reg_pass[0] ? app.cfg.reg_pass : nullptr;
  req.use_sso = app.cfg.reg_sso;

  // The registrations event callbacks must be installed before registering —
  // they are how Pulse delivers (and lets us answer) incoming calls.
  PulseRegistrationsEventCallbackConfig ev_cb{};
  ev_cb.registrations_event_incoming_callback = on_incoming;
  ev_cb.registrations_event_incoming_callback_user_context = &app;
  ev_cb.registrations_event_incoming_cancelled_callback = on_incoming_cancelled;
  ev_cb.registrations_event_incoming_cancelled_callback_user_context = &app;
  pulse_options_set_registrations_events_callbacks (app.pulse, &ev_cb);

  if (app.reg_in_flight.exchange (true)) {
    set_status (app, "Registration already in progress…");
    return;
  }

  PulseAsyncOperationResultCallbackConfig result_cb{on_register_result, &app};
  PulseOperationProgressCallbackConfig progress_cb{on_progress, &app};
  PulseError err = pulse_register_async (app.pulse, &req, &result_cb, &progress_cb);
  if (err != PULSE_SUCCESS) {
    app.reg_in_flight.store (false);
    on_register_result (err, &app);
  }
}

static void
start_deregister (App & app)
{
  PulseAsyncOperationResultCallbackConfig result_cb{on_async_result, &app};
  PulseError err = pulse_deregister_async (app.pulse, &result_cb, nullptr);
  if (err != PULSE_SUCCESS)
    set_status (app, std::string ("deregister: ") + pulse_strerror (err));
}

// Enumerate one device class into a vector the combos can render.
static std::vector<DevEntry>
enumerate_devices (Pulse * pulse, PulseMediaType type, PulseMediaDirection direction)
{
  std::vector<DevEntry> out;
  PulseDeviceIterator * it = nullptr;
  if (pulse_device_iterator_new (pulse, type, direction, &it) != PULSE_SUCCESS || !it)
    return out;
  for (const PulseDevice * d = pulse_device_iterator_first (it); d != nullptr; d = pulse_device_iterator_next (it)) {
    const char * name = pulse_device_get_name (d);
    out.push_back ({pulse_device_get_id (d), name ? name : "?", pulse_device_is_system_default (d)});
  }
  pulse_device_iterator_free (it);
  return out;
}

static void
refresh_device_lists (App & app)
{
  app.cameras = enumerate_devices (app.pulse, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT);
  app.mics = enumerate_devices (app.pulse, PULSE_MEDIA_AUDIO, PULSE_MEDIA_INPUT);
  app.speakers = enumerate_devices (app.pulse, PULSE_MEDIA_AUDIO, PULSE_MEDIA_OUTPUT);
  app.device_list_dirty.store (false);
  std::fprintf (stderr, "[pexclient] devices: %zu cameras, %zu mics, %zu speakers\n", app.cameras.size (),
                app.mics.size (), app.speakers.size ());
  for (const DevEntry & d : app.cameras)
    std::fprintf (stderr, "[pexclient]   camera: '%s'%s\n", d.name.c_str (), d.is_default ? " (default)" : "");
}

// Connect the preferred device for one class — by saved name if it is still
// present, the system default otherwise. Safe to call at any time: connecting
// a different device over a live session performs a hot swap.
static void
apply_device (App & app, const std::vector<DevEntry> & list, const std::string & preferred, PulseMediaType type,
              PulseMediaDirection direction)
{
  if (!preferred.empty ()) {
    for (const DevEntry & d : list) {
      if (d.name == preferred) {
        pulse_device_session_connect_device_by_id (app.pulse, d.id, type, direction, PULSE_MEDIA_CONTENT_MAIN);
        return;
      }
    }
  }
  pulse_device_session_connect_system_default (app.pulse, PULSE_MEDIA_CONTENT_MAIN, type, direction);
}

static void
connect_default_devices (App & app)
{
  if (app.device_list_dirty.load ())
    refresh_device_lists (app);

  // Releasing the devices between calls (so the camera light goes out) can
  // leave the previous session half torn down; re-attaching the *same* device
  // then yields a frozen capture. An explicit disconnect first makes the
  // attach below unconditional and idempotent.
  pulse_device_session_disconnect_main_video (app.pulse, PULSE_MEDIA_CONTENT_MAIN, PULSE_MEDIA_INPUT);
  pulse_device_session_disconnect_main_audio (app.pulse);
  apply_device (app, app.mics, app.cfg.dev_mic, PULSE_MEDIA_AUDIO, PULSE_MEDIA_INPUT);
  apply_device (app, app.speakers, app.cfg.dev_speaker, PULSE_MEDIA_AUDIO, PULSE_MEDIA_OUTPUT);
  app.devices_connected = true;
  rebuild_video (app); // attaches the camera, plainly or through the compositor
}

static void
on_device_list_changed (PulseMediaType, void * user_context)
{
  static_cast<App *> (user_context)->device_list_dirty.store (true);
}

// ----------------------------------------------------------------------------
//  Content share (presentation sending)
// ----------------------------------------------------------------------------

#if defined(__APPLE__)

static std::string
cf_to_std (CFStringRef cfstr)
{
  if (cfstr == nullptr)
    return "";
  CFIndex len = CFStringGetLength (cfstr);
  CFIndex max_size = CFStringGetMaximumSizeForEncoding (len, kCFStringEncodingUTF8) + 1;
  std::string result ((size_t) max_size, '\0');
  if (!CFStringGetCString (cfstr, &result[0], max_size, kCFStringEncodingUTF8))
    return "";
  result.resize (strlen (result.c_str ()));
  return result;
}

// Displays first, then "normal" application windows — same filtering rules as
// pexninja's macOS picker (layer 0, named, sensibly sized, not ourselves).
static std::vector<App::ShareSource>
enumerate_share_sources ()
{
  std::vector<App::ShareSource> out;

  CGDirectDisplayID displays[16];
  uint32_t display_count = 0;
  if (CGGetActiveDisplayList (16, displays, &display_count) == kCGErrorSuccess) {
    for (uint32_t i = 0; i < display_count; i++) {
      char buf[128];
      snprintf (buf, sizeof (buf), "%sDisplay %u  (%zux%zu)", CGDisplayIsMain (displays[i]) ? "Main " : "",
                displays[i], CGDisplayPixelsWide (displays[i]), CGDisplayPixelsHigh (displays[i]));
      out.push_back ({(uint64_t) displays[i], buf, true});
    }
  }

  CFArrayRef list =
    CGWindowListCopyWindowInfo (kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
  if (list != nullptr) {
    for (CFIndex i = 0; i < CFArrayGetCount (list); i++) {
      CFDictionaryRef info = (CFDictionaryRef) CFArrayGetValueAtIndex (list, i);

      int layer = -1;
      CFNumberRef layer_ref = (CFNumberRef) CFDictionaryGetValue (info, kCGWindowLayer);
      if (layer_ref != nullptr)
        CFNumberGetValue (layer_ref, kCFNumberIntType, &layer);
      if (layer != 0)
        continue;

      std::string name = cf_to_std ((CFStringRef) CFDictionaryGetValue (info, kCGWindowName));
      std::string owner = cf_to_std ((CFStringRef) CFDictionaryGetValue (info, kCGWindowOwnerName));
      if (name.empty () || owner == "pexclient")
        continue;

      CFDictionaryRef bounds_ref = (CFDictionaryRef) CFDictionaryGetValue (info, kCGWindowBounds);
      if (bounds_ref != nullptr) {
        CGRect bounds;
        CGRectMakeWithDictionaryRepresentation (bounds_ref, &bounds);
        if (bounds.size.width < 50 || bounds.size.height < 50)
          continue;
      }

      CFNumberRef id_ref = (CFNumberRef) CFDictionaryGetValue (info, kCGWindowNumber);
      if (id_ref == nullptr)
        continue;
      CGWindowID window_id = 0;
      CFNumberGetValue (id_ref, kCGWindowIDCFNumberType, &window_id);

      out.push_back ({(uint64_t) window_id, owner.empty () ? name : owner + " — " + name, false});
    }
    CFRelease (list);
  }

  return out;
}

#elif defined(_WIN32)

static std::string
wide_to_utf8 (const wchar_t * w)
{
  if (w == nullptr || w[0] == L'\0')
    return "";
  int n = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1)
    return "";
  std::string out ((size_t) n - 1, '\0');
  WideCharToMultiByte (CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
  return out;
}

// Displays first, then "normal" application windows — the same filtering
// rules as the macOS picker (visible, titled, sensibly sized, not ourselves),
// translated to Win32: EnumDisplayMonitors for displays, EnumWindows for
// windows, with tool windows and DWM-cloaked UWP ghosts skipped.
static BOOL CALLBACK
share_monitor_proc (HMONITOR monitor, HDC, LPRECT, LPARAM lparam)
{
  auto * out = reinterpret_cast<std::vector<App::ShareSource> *> (lparam);
  MONITORINFO info{};
  info.cbSize = sizeof (info);
  if (GetMonitorInfo (monitor, &info)) {
    long w = info.rcMonitor.right - info.rcMonitor.left;
    long h = info.rcMonitor.bottom - info.rcMonitor.top;
    char buf[128];
    snprintf (buf, sizeof (buf), "%sDisplay %zu  (%ldx%ld)", (info.dwFlags & MONITORINFOF_PRIMARY) ? "Main " : "",
              out->size () + 1, w, h);
    out->push_back ({(uint64_t) (uintptr_t) monitor, buf, true});
  }
  return TRUE;
}

static BOOL CALLBACK
share_window_proc (HWND hwnd, LPARAM lparam)
{
  auto * out = reinterpret_cast<std::vector<App::ShareSource> *> (lparam);

  if (!IsWindowVisible (hwnd) || IsIconic (hwnd))
    return TRUE;
  if (GetWindowLongW (hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
    return TRUE;

  // UWP apps leave invisible "cloaked" shell windows behind; DWM knows.
  BOOL cloaked = FALSE;
  if (SUCCEEDED (DwmGetWindowAttribute (hwnd, DWMWA_CLOAKED, &cloaked, sizeof (cloaked))) && cloaked)
    return TRUE;

  // Not ourselves (the macOS picker filters on owner name; process id is the
  // more robust equivalent here).
  DWORD pid = 0;
  GetWindowThreadProcessId (hwnd, &pid);
  if (pid == GetCurrentProcessId ())
    return TRUE;

  wchar_t wtitle[256];
  if (GetWindowTextW (hwnd, wtitle, 256) <= 0)
    return TRUE;
  std::string title = wide_to_utf8 (wtitle);
  if (title.empty ())
    return TRUE;

  RECT r;
  if (GetWindowRect (hwnd, &r) && (r.right - r.left < 50 || r.bottom - r.top < 50))
    return TRUE;

  out->push_back ({(uint64_t) (uintptr_t) hwnd, title, false});
  return TRUE;
}

static std::vector<App::ShareSource>
enumerate_share_sources ()
{
  std::vector<App::ShareSource> out;
  EnumDisplayMonitors (nullptr, nullptr, share_monitor_proc, (LPARAM) &out);
  EnumWindows (share_window_proc, (LPARAM) &out);
  return out;
}

#else

static std::vector<App::ShareSource>
enumerate_share_sources ()
{
  return {}; // window/display capture picker is macOS/Windows-only in this demo
}

#endif

// ----------------------------------------------------------------------------
//  Video pipeline
// ----------------------------------------------------------------------------
//
//  Two outgoing video streams, each either a plain source or a composition:
//
//    MAIN          camera. Plain device session when no background effect is
//                  wanted (cheapest); otherwise a mix carrying the camera with
//                  BLUR, or SEGMENTATION over a background image.
//    PRESENTATION  shared display/window. Alone in "dual stream" mode, or with
//                  the camera composited in as a PiP in "composition" mode —
//                  for far ends that show only one stream.
//
//  Everything is rebuilt from config by rebuild_video(), so a settings change
//  is just "update the value, rebuild". The mix inputs (camera, background,
//  desktop) are acquired once and referenced by both configurations.
// ----------------------------------------------------------------------------

// Resolve the configured camera to an owned PulseDevice, needed by
// pulse_video_mix_input_from_device().
static PulseDevice *
copy_selected_camera (App & app)
{
  PulseDeviceIterator * it = nullptr;
  if (pulse_device_iterator_new (app.pulse, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT, &it) != PULSE_SUCCESS || !it)
    return nullptr;

  PulseDevice * chosen = nullptr;
  for (const PulseDevice * d = pulse_device_iterator_first (it); d != nullptr; d = pulse_device_iterator_next (it)) {
    const char * name = pulse_device_get_name (d);
    bool match = app.cfg.dev_camera.empty () ? pulse_device_is_system_default (d)
                                             : (name && app.cfg.dev_camera == name);
    if (match) {
      chosen = pulse_device_copy (d);
      break;
    }
  }
  if (!chosen) { // configured camera absent — fall back to the first one
    const PulseDevice * first = pulse_device_iterator_first (it);
    if (first)
      chosen = pulse_device_copy (first);
  }
  pulse_device_iterator_free (it);
  return chosen;
}

static void
release_mix_inputs (App & app)
{
  if (app.camera_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    pulse_video_mix_input_release (app.pulse, app.camera_input);
    app.camera_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
  }
  if (app.bg_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    pulse_video_mix_input_release (app.pulse, app.bg_input);
    app.bg_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
  }
  if (app.camera_device) {
    pulse_device_free (app.camera_device);
    app.camera_device = nullptr;
  }
}

// Acquire the camera as a mix input (idempotent).
static bool
ensure_camera_input (App & app)
{
  if (app.camera_input != PULSE_VIDEO_MIX_INPUT_ID_NONE)
    return true;
  if (!app.camera_device)
    app.camera_device = copy_selected_camera (app);
  if (!app.camera_device)
    return false;
  PulseError err = pulse_video_mix_input_from_device (app.pulse, app.camera_device, &app.camera_input);
  if (err != PULSE_SUCCESS) {
    std::fprintf (stderr, "[pexclient] camera mix input failed: %s\n", pulse_strerror (err));
    app.camera_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
    return false;
  }
  return true;
}

// Anchor for the PiP, from the configured corner.
static void
pip_anchor (int corner, double * x, double * y)
{
  *x = (corner == 0 || corner == 2) ? 0.0 : 1.0;
  *y = (corner == 0 || corner == 1) ? 0.0 : 1.0;
}

static void
rebuild_video (App & app)
{
  if (!app.devices_connected)
    return; // nothing attached yet; connect_default_devices will call us

  const bool want_bg = app.cfg.bg_mode != 0;
  const bool want_pip = app.presenting && app.cfg.content_mode == 1;

  // --- tear down whatever is currently attached -------------------------
  if (app.main_mix_active) {
    pulse_video_mix_disconnect (app.pulse, PULSE_MEDIA_CONTENT_MAIN);
    app.main_mix_active = false;
  }
  pulse_device_session_disconnect_main_video (app.pulse, PULSE_MEDIA_CONTENT_MAIN, PULSE_MEDIA_INPUT);
  if (app.presenting)
    pulse_video_mix_disconnect (app.pulse, PULSE_MEDIA_CONTENT_PRESENTATION);
  release_mix_inputs (app);

  // --- MAIN --------------------------------------------------------------
  if (!want_bg && !want_pip) {
    // No compositing needed anywhere: plain camera device session.
    apply_device (app, app.cameras, app.cfg.dev_camera, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT);
  } else if (!ensure_camera_input (app)) {
    set_status (app, "Camera unavailable for compositing — using plain camera.", true);
    apply_device (app, app.cameras, app.cfg.dev_camera, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT);
  } else if (want_bg) {
    PulseVideoMixInput inputs[2];
    size_t n = 0;

    if (app.cfg.bg_mode == 2 && !app.cfg.bg_image.empty ()) {
      // Background image sits behind; the camera is keyed over it.
      if (pulse_video_mix_input_from_file (app.pulse, app.cfg.bg_image.c_str (), &app.bg_input) == PULSE_SUCCESS) {
        inputs[n] = {};
        inputs[n].input_id = app.bg_input;
        inputs[n].layer = 0;
        inputs[n].width_ratio = 1.0;
        inputs[n].height_ratio = 1.0;
        inputs[n].x_centrepoint = 0.5;
        inputs[n].y_centrepoint = 0.5;
        inputs[n].videoproc_mask = PULSE_VIDEO_PROCESS_TYPE_NONE;
        n++;
      } else {
        set_status (app, "Background image could not be loaded.", true);
      }
    }

    inputs[n] = {};
    inputs[n].input_id = app.camera_input;
    inputs[n].layer = (int) n; // above the background when there is one
    inputs[n].width_ratio = 1.0;
    inputs[n].height_ratio = 1.0;
    inputs[n].x_centrepoint = 0.5;
    inputs[n].y_centrepoint = 0.5;
    // SEGMENTATION keys the person out so the image shows through; BLUR
    // blurs whatever is behind them. The mask is a bitfield.
    inputs[n].videoproc_mask = (app.bg_input != PULSE_VIDEO_MIX_INPUT_ID_NONE)
                                 ? PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION
                                 : PULSE_VIDEO_PROCESS_TYPE_BLUR;
    n++;

    PulseVideoMixConfig cfg{n, inputs};
    PulseError err = pulse_video_mix_connect (app.pulse, &cfg, PULSE_MEDIA_CONTENT_MAIN);
    if (err != PULSE_SUCCESS) {
      std::fprintf (stderr, "[pexclient] main mix failed: %s\n", pulse_strerror (err));
      set_status (app, std::string ("background: ") + pulse_strerror (err), true);
      release_mix_inputs (app);
      apply_device (app, app.cameras, app.cfg.dev_camera, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT);
    } else {
      app.main_mix_active = true;
    }
  } else {
    // PiP only: camera still goes out plainly on MAIN as well.
    apply_device (app, app.cameras, app.cfg.dev_camera, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT);
  }

  // --- PRESENTATION ------------------------------------------------------
  if (app.presenting && app.share_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    PulseVideoMixInput inputs[2];
    size_t n = 0;

    inputs[n] = {};
    inputs[n].input_id = app.share_input;
    inputs[n].layer = 0;
    inputs[n].width_ratio = 1.0;
    inputs[n].height_ratio = 1.0;
    inputs[n].x_centrepoint = 0.5;
    inputs[n].y_centrepoint = 0.5;
    inputs[n].videoproc_mask = PULSE_VIDEO_PROCESS_TYPE_NONE;
    n++;

    if (want_pip && ensure_camera_input (app)) {
      double ax, ay;
      pip_anchor (app.cfg.pip_corner, &ax, &ay);
      inputs[n] = {};
      inputs[n].input_id = app.camera_input;
      inputs[n].layer = 1;
      inputs[n].width_ratio = app.cfg.pip_size;
      inputs[n].height_ratio = app.cfg.pip_size;
      inputs[n].x_centrepoint = ax;
      inputs[n].y_centrepoint = ay;
      // Keying the PiP means the presenter floats over the content instead
      // of sitting in a hard rectangle.
      inputs[n].videoproc_mask =
        app.cfg.bg_mode != 0 ? PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION : PULSE_VIDEO_PROCESS_TYPE_NONE;
      n++;
    }

    PulseVideoMixConfig cfg{n, inputs};
    PulseError err = pulse_video_mix_connect (app.pulse, &cfg, PULSE_MEDIA_CONTENT_PRESENTATION);
    if (err != PULSE_SUCCESS) {
      std::fprintf (stderr, "[pexclient] presentation mix failed: %s\n", pulse_strerror (err));
      set_status (app, std::string ("share: ") + pulse_strerror (err), true);
    }
  }
}

static void
stop_share (App & app)
{
  if (!app.presenting)
    return;
  pulse_video_mix_disconnect (app.pulse, PULSE_MEDIA_CONTENT_PRESENTATION);
  if (app.share_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    pulse_video_mix_input_release (app.pulse, app.share_input);
    app.share_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
  }
  pulse_participant_control_release_floor (app.pulse, nullptr);
  app.presenting = false;
  rebuild_video (app); // MAIN may have been carrying a PiP-shared camera input
}

// Share one display/window: acquire it as a mix input, run a single-input
// full-frame mix on the PRESENTATION content, then take the floor so the
// conference switches to our stream (pexninja's presentation recipe, minus
// the camera PiP compositing).
static void
start_share (App & app, const App::ShareSource & src)
{
  stop_share (app); // switching sources: tear the previous mix down first

  PulseError err = pulse_video_mix_input_from_desktop (app.pulse, src.handle,
                                                       src.is_display ? PULSE_DISPLAY : PULSE_WINDOW,
                                                       &app.share_input);
  if (err != PULSE_SUCCESS) {
    set_status (app, std::string ("share: ") + pulse_strerror (err), true);
    return;
  }

  app.share_src = src;
  app.presenting = true;
  rebuild_video (app); // builds PRESENTATION (and re-does MAIN if it shares the camera)

  pulse_participant_control_take_floor (app.pulse, nullptr);
  set_status (app, "Presenting " + src.name);
}

// Roster updates: take ownership of the new list, free the previous one.
// (PulseConferenceControlParticipantsResponse is a typedef of this same
// struct, so pulse_conference_control_free_participant_list is the free.)
static void
on_participant_list_updated (PulseRoomId, PulseConferenceEventParticipantList * list, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  std::lock_guard<std::mutex> lock (app->roster_mutex);
  pulse_conference_control_free_participant_list (app->roster);
  app->roster = list;
}

static void
clear_roster (App & app)
{
  std::lock_guard<std::mutex> lock (app.roster_mutex);
  pulse_conference_control_free_participant_list (app.roster);
  app.roster = nullptr;
}

// ----------------------------------------------------------------------------
//  Chat
// ----------------------------------------------------------------------------

static void
on_message_received (PulseRoomId, const PulseConferenceEventMessageReceived * event, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  App::ChatMsg m;
  m.origin = event->origin ? event->origin : "?";
  m.text = event->payload ? event->payload : "";
  m.time = now_hhmm ();
  m.direct = event->direct;
  {
    std::lock_guard<std::mutex> lock (app->chat_mutex);
    app->chat.push_back (std::move (m));
  }
  app->chat_unread.fetch_add (1); // cleared every frame while the drawer is open
  app->chat_scroll_to_bottom = true;
}

// Send a chat message — broadcast when no recipient is selected, direct
// otherwise — and echo it into our own transcript (Infinity does not reflect
// a sender's messages back).
static void
send_chat (App & app, const char * text)
{
  bool direct = !app.chat_target_uuid.empty ();

  PulseMessageRequest req{};
  req.content_type = PULSE_MESSAGE_CONTENT_TYPE_PLAIN;
  req.payload = text;
  PulseError err = pulse_send_message (app.pulse, direct ? app.chat_target_uuid.c_str () : nullptr, &req);
  if (err != PULSE_SUCCESS) {
    set_status (app, std::string ("chat: ") + pulse_strerror (err), true);
    return;
  }

  App::ChatMsg m;
  m.origin = direct ? "You → " + app.chat_target_name : "You";
  m.text = text;
  m.time = now_hhmm ();
  m.self = true;
  m.direct = direct;
  std::lock_guard<std::mutex> lock (app.chat_mutex);
  app.chat.push_back (std::move (m));
  app.chat_scroll_to_bottom = true;
}

static void
clear_chat (App & app)
{
  std::lock_guard<std::mutex> lock (app.chat_mutex);
  app.chat.clear ();
  app.chat_unread.store (0);
  app.chat_input[0] = '\0';
  app.chat_target_uuid.clear ();
  app.chat_target_name.clear ();
}

// Conference-level state (lock, guest mute, ...) for the controls panel.
static void
on_conference_update (PulseRoomId, const PulseConferenceEventConferenceUpdate * event, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  app->conf_locked.store (event->locked);
  app->conf_guests_muted.store (event->guests_muted);
  app->conf_guests_can_unmute.store (event->guests_can_unmute);
  app->captions_available.store (event->live_captions_available);
}

// Live caption text. Partial results overwrite each other; finals linger
// until the display timeout in ui_in_call ages them out.
static void
on_live_captions (PulseRoomId, const PulseConferenceEventLiveCaptions * event, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  std::lock_guard<std::mutex> lock (app->caption_mutex);
  app->caption_text = event->data ? event->data : "";
  app->caption_at = std::chrono::steady_clock::now ();
}

// Layout events tell us the currently active layout code (e.g. "1:7").
static void
on_layout (PulseRoomId, const PulseConferenceEventLayout * event, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  std::lock_guard<std::mutex> lock (app->layout_mutex);
  app->current_layout = event->layout ? event->layout : "";
}

// Far-end presentation lifecycle — drives which stream the in-call view
// promotes to the big pane.
static void
on_presentation_start (PulseRoomId, const PulseConferenceEventPresentationStart *, void * user_context)
{
  static_cast<App *> (user_context)->remote_presenting.store (true);
}

static void
on_presentation_stop (PulseRoomId, void * user_context)
{
  static_cast<App *> (user_context)->remote_presenting.store (false);
}

static void
record_recent (App & app, const std::string & address, const std::string & duration)
{
  RecentCall r;
  r.name = local_part (address);
  r.address = address;
  r.timestamp = now_hhmm ();
  r.duration = duration;
  r.starred = is_favorite (app.cfg, address);
  app.cfg.recents.insert (app.cfg.recents.begin (), r);
  if (app.cfg.recents.size () > 20)
    app.cfg.recents.resize (20);
  save_config (app.cfg);
}

static void
start_dial (App & app, std::string address)
{
  if (address.empty ())
    return;

  bool registered = app.reg_status.load () == PULSE_CONNECTION_STATUS_CONNECTED;
  std::string server;
  std::size_t at = address.find ('@');
  if (at != std::string::npos) {
    server = address.substr (at + 1);
  } else if (registered) {
    server = app.cfg.reg_host;
    address += "@";
    address += app.cfg.reg_host;
  } else if (app.cfg.default_server[0]) {
    server = app.cfg.default_server;
  } else {
    set_status (app, "Not registered — dial a full address (alias@domain) or set a default server.");
    return;
  }

  connect_default_devices (app);

  PulseRestConnectionConfig cc{};
  cc.server_address = server.c_str ();
  cc.conference_name = address.c_str ();
  cc.display_name = app.cfg.display_name;

  PulseAsyncOperationResultCallbackConfig result_cb{on_async_result, &app};
  PulseOperationProgressCallbackConfig progress_cb{on_progress, &app};
  PulseError err = pulse_connect_with_rest_async (app.pulse, &cc, &result_cb, &progress_cb);
  if (err != PULSE_SUCCESS) {
    set_status (app, std::string ("connect: ") + pulse_strerror (err));
    return;
  }
  app.call_address = address;
  app.call_was_connected = false;
  set_status (app, "Calling " + address + "…");
}

static void
start_hangup (App & app)
{
  PulseAsyncOperationResultCallbackConfig result_cb{on_async_result, &app};
  pulse_disconnect_async (app.pulse, &result_cb, nullptr);
}

// Registrar directory search for the autocomplete dropdown. Blocking, but it's
// the same pattern pexninja uses per keystroke and it stays snappy in practice.
static void
refresh_alias_hits (App & app)
{
  std::string query = app.search;
  if (query == app.last_query)
    return;
  app.last_query = query;
  app.hits.clear ();

  if (app.reg_status.load () != PULSE_CONNECTION_STATUS_CONNECTED || query.empty ())
    return;

  PulseRegistrationAliasList * result = nullptr;
  if (pulse_registrations_query_alias (app.pulse, query.c_str (), 6, 6, &result) == PULSE_SUCCESS && result) {
    for (size_t i = 0; i < result->size; i++) {
      AliasHit hit;
      hit.alias = result->list[i]->alias ? result->list[i]->alias : "";
      hit.description = result->list[i]->description ? result->list[i]->description : "";
      hit.is_device = result->list[i]->type == PULSE_REGISTRATION_ALIAS_DEVICE;
      app.hits.push_back (std::move (hit));
    }
  }
  pulse_registration_alias_list_free (result);
}

// ----------------------------------------------------------------------------
//  Textures: background mesh, avatar gradient, video frames
// ----------------------------------------------------------------------------

// The handoff's window background: 135° three-stop gradient + two mesh blobs,
// with a 4x4 ordered dither so the smooth ramps don't band at 8 bits.
static GLuint
make_background_texture ()
{
  const int W = 960, H = 660; // ~ the 820x580 design frame, GL-stretched
  static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

  struct Stop
  {
    float t;
    float r, g, b;
  };
  const Stop stops[3] = {
    {0.00f, 0x0D, 0x12, 0x20},
    {0.40f, 0x11, 0x18, 0x27},
    {1.00f, 0x0D, 0x1A, 0x2A},
  };

  // Blob geometry from the handoff, scaled from its 820x580 reference frame.
  const float sx = W / 820.0f, sy = H / 580.0f, sr = (sx + sy) * 0.5f;
  struct Blob
  {
    float cx, cy, reach, r, g, b, peak;
  };
  const Blob blobs[2] = {
    {(-60 + 160) * sx, (-80 + 160) * sy, 160 * sr / 0.7f, 0x63, 0x66, 0xF1, 0.18f},
    {(820 + 40 - 140) * sx, (580 + 60 - 140) * sy, 140 * sr / 0.7f, 0x38, 0xBD, 0xF8, 0.12f},
  };

  std::vector<unsigned char> pixels ((size_t) W * H * 4);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      float t = (float) (x + y) / (float) (W + H);
      float r, g, b;
      if (t <= stops[1].t) {
        float f = t / stops[1].t;
        r = stops[0].r + (stops[1].r - stops[0].r) * f;
        g = stops[0].g + (stops[1].g - stops[0].g) * f;
        b = stops[0].b + (stops[1].b - stops[0].b) * f;
      } else {
        float f = (t - stops[1].t) / (1.0f - stops[1].t);
        r = stops[1].r + (stops[2].r - stops[1].r) * f;
        g = stops[1].g + (stops[2].g - stops[1].g) * f;
        b = stops[1].b + (stops[2].b - stops[1].b) * f;
      }
      for (const Blob & bl : blobs) {
        float dx = x - bl.cx, dy = y - bl.cy;
        float d = std::sqrt (dx * dx + dy * dy);
        if (d < bl.reach) {
          float a = (1.0f - d / bl.reach) * bl.peak;
          r += (bl.r - r) * a;
          g += (bl.g - g) * a;
          b += (bl.b - b) * a;
        }
      }
      float dither = (bayer[y & 3][x & 3] + 0.5f) / 16.0f - 0.5f;
      unsigned char * px = &pixels[((size_t) y * W + x) * 4];
      px[0] = (unsigned char) std::min (255.0f, std::max (0.0f, r + dither + 0.5f));
      px[1] = (unsigned char) std::min (255.0f, std::max (0.0f, g + dither + 0.5f));
      px[2] = (unsigned char) std::min (255.0f, std::max (0.0f, b + dither + 0.5f));
      px[3] = 255;
    }
  }

  GLuint tex;
  glGenTextures (1, &tex);
  glBindTexture (GL_TEXTURE_2D, tex);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data ());
  return tex;
}

// 135° AccentPrimary → AccentGradientEnd, used behind avatar initials.
static GLuint
make_avatar_texture ()
{
  const int N = 128;
  std::vector<unsigned char> pixels ((size_t) N * N * 4);
  for (int y = 0; y < N; y++) {
    for (int x = 0; x < N; x++) {
      float t = (float) (x + y) / (float) (2 * N);
      unsigned char * px = &pixels[((size_t) y * N + x) * 4];
      px[0] = (unsigned char) (0x5B + (0x8B - 0x5B) * t);
      px[1] = (unsigned char) (0x6C + (0x5C - 0x6C) * t);
      px[2] = (unsigned char) (0xE8 + (0xF6 - 0xE8) * t);
      px[3] = 255;
    }
  }
  GLuint tex;
  glGenTextures (1, &tex);
  glBindTexture (GL_TEXTURE_2D, tex);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data ());
  return tex;
}

// --- Video data sessions (doppler's pattern) --------------------------------

static void
init_video_ctx (Pulse * pulse, GLVideoCtx & ctx, PulseMediaContent content)
{
  glGenTextures (1, &ctx.texture);
  glBindTexture (GL_TEXTURE_2D, ctx.texture);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  ctx.content = content;

  PulseDataSessionConfig * cfg = pulse_data_session_config_new (PULSE_DATA_SESSION_VIDEO_FROM_CAPS);
  pulse_data_session_config_video_from_caps (cfg, "video/x-raw, format=RGBA");
  pulse_data_session_connect_output (pulse, cfg, content);
  pulse_data_session_config_free (cfg);
}

static void
pump_video_ctx (Pulse * pulse, GLVideoCtx & ctx)
{
  PulseDataSessionFrameData * frame = nullptr;
  pulse_data_session_pull_frame_data (pulse, PULSE_MEDIA_VIDEO, &frame, ctx.content, 0);
  if (!frame)
    return;
  int w = 0, h = 0;
  if (pulse_frame_data_get_resolution (frame, &w, &h) && w > 0 && h > 0) {
    glBindTexture (GL_TEXTURE_2D, ctx.texture);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame->data);
    ctx.width = w;
    ctx.height = h;
  }
  pulse_data_session_frame_data_free (frame);
}

// ----------------------------------------------------------------------------
//  Draw-list glyphs — the handoff wants 1.2px-stroke line icons, so the few
//  glyphs needed are drawn as paths rather than shipped as an icon font.
// ----------------------------------------------------------------------------

static void
glyph_search (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  dl->AddCircle (ImVec2 (center.x - r * 0.18f, center.y - r * 0.18f), r * 0.62f, col, 0, 1.2f);
  ImVec2 tip (center.x + r * 0.75f, center.y + r * 0.75f);
  ImVec2 from (center.x + r * 0.28f, center.y + r * 0.28f);
  dl->AddLine (from, tip, col, 1.2f);
}

static void
glyph_gear (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  dl->AddCircle (center, r * 0.45f, col, 0, 1.2f);
  for (int i = 0; i < 8; i++) {
    float a = (float) i * 3.14159265f / 4.0f;
    ImVec2 from (center.x + std::cos (a) * r * 0.68f, center.y + std::sin (a) * r * 0.68f);
    ImVec2 to (center.x + std::cos (a) * r, center.y + std::sin (a) * r);
    dl->AddLine (from, to, col, 1.4f);
  }
}

static void
glyph_star (ImDrawList * dl, ImVec2 center, float r, ImU32 col, bool filled)
{
  ImVec2 pts[10];
  for (int i = 0; i < 10; i++) {
    float a = -3.14159265f / 2.0f + (float) i * 3.14159265f / 5.0f;
    float rr = (i % 2 == 0) ? r : r * 0.45f;
    pts[i] = ImVec2 (center.x + std::cos (a) * rr, center.y + std::sin (a) * rr);
  }
  if (filled)
    dl->AddConcavePolyFilled (pts, 10, col);
  else
    dl->AddPolyline (pts, 10, col, ImDrawFlags_Closed, 1.1f);
}

static void
glyph_mic (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  // Capsule body + pickup arc + stem.
  ImVec2 b0 (center.x - r * 0.28f, center.y - r * 0.95f), b1 (center.x + r * 0.28f, center.y + r * 0.15f);
  dl->AddRect (b0, b1, col, r * 0.28f, 0, 1.3f);
  dl->PathArcTo (ImVec2 (center.x, center.y - r * 0.05f), r * 0.62f, 0.35f, 3.14159265f - 0.35f, 10);
  dl->PathStroke (col, 0, 1.3f);
  dl->AddLine (ImVec2 (center.x, center.y + r * 0.57f), ImVec2 (center.x, center.y + r * 0.95f), col, 1.3f);
}

static void
glyph_camera (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  ImVec2 b0 (center.x - r, center.y - r * 0.6f), b1 (center.x + r * 0.35f, center.y + r * 0.6f);
  dl->AddRect (b0, b1, col, 2.5f, 0, 1.3f);
  ImVec2 tri[3] = {ImVec2 (b1.x + r * 0.12f, center.y - r * 0.18f), ImVec2 (center.x + r, center.y - r * 0.5f),
                   ImVec2 (center.x + r, center.y + r * 0.5f)};
  dl->AddLine (tri[0], tri[1], col, 1.3f);
  dl->AddLine (tri[1], tri[2], col, 1.3f);
  dl->AddLine (tri[2], ImVec2 (b1.x + r * 0.12f, center.y + r * 0.18f), col, 1.3f);
}

// Mic / camera toggle per the handoff's variant-03 spec: glass button; muted
// state gets a red-tinted fill, red glyph and a diagonal strike.
static bool
mute_toggle_button (App & app, ImDrawList * dl, const char * id, ImVec2 p0, float size, bool muted, bool is_camera)
{
  ImVec2 p1 (p0.x + size, p0.y + size);
  ImGui::SetCursorScreenPos (p0);
  bool clicked = ImGui::InvisibleButton (id, ImVec2 (size, size));
  bool hovered = ImGui::IsItemHovered ();

  ImU32 fill = muted ? theme::HexU32 (theme::StatusError, 0.18f)
                     : theme::WhiteU32 (hovered ? theme::PanelFillRaised + 0.03f : theme::PanelFill);
  dl->AddRectFilled (p0, p1, fill, theme::RadiusRow);
  dl->AddRect (p0, p1, muted ? theme::HexU32 (theme::StatusError, 0.45f) : theme::WhiteU32 (theme::PanelStroke),
               theme::RadiusRow, 0, 1.0f);

  ImU32 glyph_col = muted ? theme::HexU32 (theme::StatusError) : theme::WhiteU32 (theme::TextSecondary);
  ImVec2 c ((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
  if (is_camera)
    glyph_camera (dl, c, size * 0.26f, glyph_col);
  else
    glyph_mic (dl, c, size * 0.28f, glyph_col);

  if (muted)
    dl->AddLine (ImVec2 (p0.x + size * 0.24f, p0.y + size * 0.24f), ImVec2 (p1.x - size * 0.24f, p1.y - size * 0.24f),
                 theme::HexU32 (theme::StatusError), 1.6f);

  if (hovered)
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
  return clicked;
}

static void
glyph_arrow_out (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  // Outgoing ↗ per the handoff's recents rows.
  dl->AddLine (ImVec2 (center.x - r * 0.7f, center.y + r * 0.7f), ImVec2 (center.x + r * 0.6f, center.y - r * 0.6f),
               col, 1.2f);
  dl->AddLine (ImVec2 (center.x - r * 0.2f, center.y - r * 0.6f), ImVec2 (center.x + r * 0.6f, center.y - r * 0.6f),
               col, 1.2f);
  dl->AddLine (ImVec2 (center.x + r * 0.6f, center.y - r * 0.6f), ImVec2 (center.x + r * 0.6f, center.y + r * 0.2f),
               col, 1.2f);
}

static void
glyph_screen (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  // Monitor with a stand — the share-content affordance.
  ImVec2 p0 (center.x - r, center.y - r * 0.75f), p1 (center.x + r, center.y + r * 0.35f);
  dl->AddRect (p0, p1, col, 2.0f, 0, 1.3f);
  dl->AddLine (ImVec2 (center.x, p1.y), ImVec2 (center.x, p1.y + r * 0.35f), col, 1.3f);
  dl->AddLine (ImVec2 (center.x - r * 0.45f, p1.y + r * 0.55f), ImVec2 (center.x + r * 0.45f, p1.y + r * 0.55f), col,
               1.3f);
  // Upward "share" arrow inside the screen.
  dl->AddLine (ImVec2 (center.x, center.y + r * 0.12f), ImVec2 (center.x, center.y - r * 0.45f), col, 1.2f);
  dl->AddLine (ImVec2 (center.x - r * 0.22f, center.y - r * 0.2f), ImVec2 (center.x, center.y - r * 0.45f), col, 1.2f);
  dl->AddLine (ImVec2 (center.x + r * 0.22f, center.y - r * 0.2f), ImVec2 (center.x, center.y - r * 0.45f), col, 1.2f);
}

// Note: unlike the other glyphs this needs a font, so it reads it from the
// App via a global set once at startup (the glyph_button signature is shared).
static ImFont * g_cc_font = nullptr;

static void
glyph_cc (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  ImVec2 p0 (center.x - r * 1.15f, center.y - r * 0.8f), p1 (center.x + r * 1.15f, center.y + r * 0.8f);
  dl->AddRect (p0, p1, col, 3.0f, 0, 1.3f);
  if (g_cc_font) {
    const char * txt = "CC";
    float size = r * 1.05f;
    ImVec2 ts = g_cc_font->CalcTextSizeA (size, FLT_MAX, 0, txt);
    dl->AddText (g_cc_font, size, ImVec2 (center.x - ts.x / 2, center.y - ts.y / 2), col, txt);
  }
}

static void
glyph_keypad (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  // 3x3 dot grid — the dial-pad affordance.
  for (int gy = -1; gy <= 1; gy++)
    for (int gx = -1; gx <= 1; gx++)
      dl->AddCircleFilled (ImVec2 (center.x + gx * r * 0.62f, center.y + gy * r * 0.62f), 1.7f, col);
}

static void
glyph_dots (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  for (int i = -1; i <= 1; i++)
    dl->AddCircleFilled (ImVec2 (center.x + i * r * 0.75f, center.y), 1.9f, col);
}

static void
glyph_chat (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  // Rounded speech bubble with a tail, and two dots inside.
  ImVec2 p0 (center.x - r, center.y - r * 0.75f), p1 (center.x + r, center.y + r * 0.45f);
  dl->AddRect (p0, p1, col, 3.5f, 0, 1.3f);
  dl->AddTriangleFilled (ImVec2 (center.x - r * 0.45f, p1.y), ImVec2 (center.x - r * 0.05f, p1.y),
                         ImVec2 (center.x - r * 0.45f, p1.y + r * 0.45f), col);
  dl->AddCircleFilled (ImVec2 (center.x - r * 0.35f, center.y - r * 0.15f), 1.4f, col);
  dl->AddCircleFilled (ImVec2 (center.x + r * 0.15f, center.y - r * 0.15f), 1.4f, col);
}

static void
glyph_people (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  // One clear head-and-shoulders bust, with a hint of a second behind-right.
  const float pi = 3.14159265f;
  // Secondary (drawn first, dimmer): partial head + shoulder line.
  ImU32 dim = (col & 0x00FFFFFF) | ((ImU32) (((col >> 24) & 0xFF) * 0.55f) << 24);
  dl->AddCircle (ImVec2 (center.x + r * 0.55f, center.y - r * 0.35f), r * 0.26f, dim, 0, 1.2f);
  dl->PathArcTo (ImVec2 (center.x + r * 0.55f, center.y + r * 0.85f), r * 0.55f, -pi * 0.45f, -pi * 0.08f, 8);
  dl->PathStroke (dim, 0, 1.2f);
  // Primary bust: head circle over a shoulders arc (dome opening downward).
  dl->AddCircle (ImVec2 (center.x - r * 0.25f, center.y - r * 0.38f), r * 0.34f, col, 0, 1.4f);
  dl->PathArcTo (ImVec2 (center.x - r * 0.25f, center.y + r * 1.05f), r * 0.72f, -pi * 0.85f, -pi * 0.15f, 12);
  dl->PathStroke (col, 0, 1.4f);
}

static void
glyph_sliders (ImDrawList * dl, ImVec2 center, float r, ImU32 col)
{
  // Three rails with offset knobs — the "tune devices" affordance.
  const float knob_x[3] = {-0.35f, 0.30f, -0.10f};
  for (int i = 0; i < 3; i++) {
    float y = center.y + (i - 1) * r * 0.62f;
    dl->AddLine (ImVec2 (center.x - r, y), ImVec2 (center.x + r, y), col, 1.2f);
    dl->AddCircleFilled (ImVec2 (center.x + knob_x[i] * r, y), 2.1f, col);
  }
}

// Small square glass button hosting a glyph; used for the devices trigger.
static bool
glyph_button (App & app, ImDrawList * dl, const char * id, ImVec2 p0, float size, bool active,
              void (*glyph) (ImDrawList *, ImVec2, float, ImU32))
{
  ImVec2 p1 (p0.x + size, p0.y + size);
  ImGui::SetCursorScreenPos (p0);
  bool clicked = ImGui::InvisibleButton (id, ImVec2 (size, size));
  bool hovered = ImGui::IsItemHovered ();

  dl->AddRectFilled (p0, p1,
                     active ? theme::HexU32 (theme::AccentPrimary, 0.20f)
                            : theme::WhiteU32 (hovered ? theme::PanelFillRaised + 0.03f : theme::PanelFill),
                     theme::RadiusRow);
  dl->AddRect (p0, p1, active ? theme::HexU32 (theme::AccentPrimary, 0.45f) : theme::WhiteU32 (theme::PanelStroke),
               theme::RadiusRow, 0, 1.0f);
  glyph (dl, ImVec2 ((p0.x + p1.x) / 2, (p0.y + p1.y) / 2), size * 0.24f,
         active ? theme::HexU32 (theme::AccentPrimary) : theme::WhiteU32 (theme::TextSecondary));

  if (hovered)
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
  return clicked;
}

// Rounded-gradient avatar with centered initials.
static void
draw_avatar (App & app, ImDrawList * dl, ImVec2 center, float diameter, const std::string & name)
{
  float r = diameter * 0.5f;
  ImVec2 p0 (center.x - r, center.y - r), p1 (center.x + r, center.y + r);
  dl->AddImageRounded ((ImTextureID) (intptr_t) app.avatar_texture, p0, p1, ImVec2 (0, 0), ImVec2 (1, 1),
                       IM_COL32_WHITE, r);
  // 2px accent ring @45%, per the favourites spec.
  dl->AddCircle (center, r + 1.0f, theme::HexU32 (theme::AccentPrimary, 0.45f), 0, 2.0f);

  std::string txt = initials_of (name);
  float fsize = diameter * 0.40f;
  ImVec2 ts = app.fonts.bodyBold->CalcTextSizeA (fsize, FLT_MAX, 0, txt.c_str ());
  dl->AddText (app.fonts.bodyBold, fsize, ImVec2 (center.x - ts.x / 2, center.y - ts.y / 2),
               theme::WhiteU32 (0.95f), txt.c_str ());
}

// ----------------------------------------------------------------------------
//  UI sections
// ----------------------------------------------------------------------------

static void
ui_search_row (App & app, float width)
{
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 origin = ImGui::GetCursorScreenPos ();

  const float h = 40.0f * theme::scale;
  const float call_w = 86.0f * theme::scale;
  const float toggles_w = (h + 8.0f) * 3; // mic + camera + devices, gap 8 each
  const float field_w = width - toggles_w - call_w - 10.0f;

  // --- Search field ------------------------------------------------------
  ImVec2 f0 = origin, f1 = ImVec2 (origin.x + field_w, origin.y + h);
  bool field_active;
  {
    dl->AddRectFilled (f0, f1, theme::WhiteU32 (theme::PanelFillRaised), theme::RadiusControl);

    glyph_search (dl, ImVec2 (f0.x + 22, (f0.y + f1.y) * 0.5f), 7.0f, theme::WhiteU32 (theme::IconMuted));

    ImGui::SetCursorScreenPos (ImVec2 (f0.x + 40, f0.y + (h - ImGui::GetFrameHeight ()) * 0.5f));
    ImGui::PushStyleColor (ImGuiCol_FrameBg, ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_FrameBgHovered, ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_FrameBgActive, ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetNextItemWidth (field_w - 52);
    bool entered = ImGui::InputTextWithHint ("##dial", "Search or enter address", app.search, sizeof (app.search),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    field_active = ImGui::IsItemActive ();
    ImGui::PopStyleVar ();
    ImGui::PopStyleColor (3);

    // Focus ring: accent stroke @50% + 3px accent @15% outer ring.
    if (field_active) {
      dl->AddRect (f0, f1, theme::HexU32 (theme::AccentPrimary, 0.50f), theme::RadiusControl, 0, 1.0f);
      dl->AddRect (ImVec2 (f0.x - 2, f0.y - 2), ImVec2 (f1.x + 2, f1.y + 2),
                   theme::HexU32 (theme::AccentPrimary, 0.15f), theme::RadiusControl + 2, 0, 3.0f);
    } else {
      dl->AddRect (f0, f1, theme::WhiteU32 (theme::PanelStroke), theme::RadiusControl, 0, 1.0f);
    }

    if (entered && app.search[0])
      start_dial (app, app.search);
  }

  // --- Mic / camera toggles ---------------------------------------------
  // Set the starting state before a call; flip live during one. Pulse keeps
  // pulse_mute_*_input state across connects, so one code path serves both.
  {
    if (mute_toggle_button (app, dl, "##premic", ImVec2 (f1.x + 8, origin.y), h, app.mic_muted, false)) {
      app.mic_muted = !app.mic_muted;
      pulse_mute_audio_input (app.pulse, app.mic_muted);
    }
    if (mute_toggle_button (app, dl, "##precam", ImVec2 (f1.x + 8 + h + 8, origin.y), h, app.cam_muted, true)) {
      app.cam_muted = !app.cam_muted;
      pulse_mute_video_input (app.pulse, app.cam_muted);
    }
    if (glyph_button (app, dl, "##predev", ImVec2 (f1.x + 8 + (h + 8) * 2, origin.y), h, app.show_devices,
                      glyph_sliders))
      app.show_devices = !app.show_devices;
  }

  // --- Call button -------------------------------------------------------
  {
    ImVec2 b0 (f1.x + toggles_w + 10, origin.y), b1 (b0.x + call_w, origin.y + h);
    bool enabled = app.search[0] != '\0';

    ImGui::SetCursorScreenPos (b0);
    bool clicked = ImGui::InvisibleButton ("##call", ImVec2 (call_w, h)) && enabled;
    bool hovered = ImGui::IsItemHovered ();
    bool held = ImGui::IsItemActive ();

    unsigned int fill = held ? theme::AccentPressed : (hovered ? theme::AccentHover : theme::AccentPrimary);
    float alpha = enabled ? 1.0f : 0.35f;
    if (enabled)
      dl->AddRectFilled (ImVec2 (b0.x, b0.y + 4), ImVec2 (b1.x, b1.y + 6), theme::HexU32 (theme::AccentPrimary, 0.28f),
                         theme::RadiusControl + 4); // ShadowAccentLarge approximation
    dl->AddRectFilled (b0, b1, theme::HexU32 (fill, alpha), theme::RadiusControl);

    const char * lbl = "Call";
    ImVec2 ts = app.fonts.bodyBold->CalcTextSizeA (13.5f, FLT_MAX, 0, lbl);
    dl->AddText (app.fonts.bodyBold, theme::fs (13.5f), ImVec2 ((b0.x + b1.x - ts.x) / 2, (b0.y + b1.y - ts.y) / 2),
                 theme::WhiteU32 (enabled ? 1.0f : theme::TextTertiary), lbl);

    if (hovered && enabled)
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
    if (clicked)
      start_dial (app, app.search);
  }

  // --- Autocomplete dropdown --------------------------------------------
  refresh_alias_hits (app);
  if (field_active && !app.hits.empty ()) {
    ImGui::SetNextWindowPos (ImVec2 (f0.x, f1.y + 6));
    ImGui::SetNextWindowSize (ImVec2 (field_w, 0));
    ImGui::Begin ("##hits", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_Tooltip);
    for (const AliasHit & hit : app.hits) {
      std::string row = hit.alias;
      if (!hit.description.empty ())
        row += "  ·  " + hit.description;
      row += hit.is_device ? "   [device]" : "   [conference]";
      if (ImGui::Selectable (row.c_str ())) {
        snprintf (app.search, sizeof (app.search), "%s", hit.alias.c_str ());
        app.last_query = "\x01";
      }
    }
    ImGui::End ();
  }

  ImGui::SetCursorScreenPos (ImVec2 (origin.x, origin.y + h));
}

static void
ui_favorites (App & app, float width)
{
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 origin = ImGui::GetCursorScreenPos ();

  dl->AddText (app.fonts.label, theme::fs (10.5f), origin, theme::WhiteU32 (theme::TextLabel), "FAVORITES");

  float y = origin.y + 22;
  float x = origin.x;
  const float av = 44.0f * theme::scale, item_w = 64.0f * theme::scale;

  if (app.cfg.favorites.empty ()) {
    // Empty state: dashed circle + "Add favorite" hint (star a recent call).
    ImVec2 c (x + av / 2, y + av / 2);
    for (int i = 0; i < 12; i++) {
      float a0 = (float) i * 3.14159265f / 6.0f, a1 = a0 + 3.14159265f / 10.0f;
      dl->PathArcTo (c, av / 2, a0, a1, 4);
      dl->PathStroke (theme::WhiteU32 (theme::PanelStroke), 0, 1.2f);
    }
    ImVec2 ts = app.fonts.body->CalcTextSizeA (13.5f, FLT_MAX, 0, "+");
    dl->AddText (app.fonts.body, theme::fs (13.5f), ImVec2 (c.x - ts.x / 2, c.y - ts.y / 2), theme::WhiteU32 (theme::IconMuted),
                 "+");
    dl->AddText (app.fonts.smallMed, theme::fs (11.0f), ImVec2 (x, y + av + 6), theme::WhiteU32 (theme::TextTertiary),
                 "Star a call");
  }

  for (size_t i = 0; i < app.cfg.favorites.size (); i++) {
    const Contact & f = app.cfg.favorites[i];
    ImVec2 c (x + av / 2, y + av / 2);

    ImGui::SetCursorScreenPos (ImVec2 (x, y));
    ImGui::PushID ((int) i);
    bool clicked = ImGui::InvisibleButton ("##fav", ImVec2 (av, av + 20));
    bool hovered = ImGui::IsItemHovered ();
    ImGui::PopID ();

    draw_avatar (app, dl, c, hovered ? av * 1.06f : av, f.name);

    ImVec2 ts = app.fonts.smallMed->CalcTextSizeA (11.0f, FLT_MAX, 0, f.name.c_str ());
    float tx = c.x - std::min (ts.x, item_w) / 2;
    dl->AddText (app.fonts.smallMed, theme::fs (11.0f), ImVec2 (tx, y + av + 6), theme::WhiteU32 (theme::TextSecondary),
                 f.name.c_str ());

    if (hovered)
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
    if (clicked)
      start_dial (app, f.address);

    x += item_w + 16;
    if (x + item_w > origin.x + width)
      break;
  }

  ImGui::SetCursorScreenPos (ImVec2 (origin.x, y + av + 24));
}

static void
ui_recents_panel (App & app, ImVec2 size)
{
  ImGui::BeginChild ("##recents", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 p = ImGui::GetWindowPos ();
  ImVec2 sz = ImGui::GetWindowSize ();

  dl->AddText (app.fonts.bodyBold, theme::fs (13.5f), ImVec2 (p.x + 18, p.y + 16), theme::WhiteU32 (theme::TextPrimary),
               "Recent calls");

  ImGui::SetCursorPos (ImVec2 (8, 46));

  const float row_h = 54.0f * theme::scale;
  for (size_t i = 0; i < app.cfg.recents.size (); i++) {
    RecentCall & r = app.cfg.recents[i];

    ImGui::PushID ((int) i);
    ImVec2 row0 = ImGui::GetCursorScreenPos ();
    float row_w = sz.x - 16;

    ImGui::SetNextItemAllowOverlap (); // let the star button win clicks inside the row
    bool clicked = ImGui::InvisibleButton ("##row", ImVec2 (row_w, row_h));
    bool hovered = ImGui::IsWindowHovered (ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                   ImGui::IsMouseHoveringRect (row0, ImVec2 (row0.x + row_w, row0.y + row_h));
    bool dbl = hovered && ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left);

    if (hovered)
      dl->AddRectFilled (row0, ImVec2 (row0.x + row_w, row0.y + row_h), theme::WhiteU32 (theme::RowHover),
                         theme::RadiusRow);

    // Avatar 34.
    draw_avatar (app, dl, ImVec2 (row0.x + (10 + 17) * theme::scale, row0.y + row_h / 2), 34.0f * theme::scale,
                 r.name);

    // Name + address.
    dl->AddText (app.fonts.bodyBold, theme::fs (13.0f), ImVec2 (row0.x + 54 * theme::scale, row0.y + 9 * theme::scale), theme::WhiteU32 (0.88f),
                 r.name.c_str ());
    dl->AddText (app.fonts.small_, theme::fs (11.0f), ImVec2 (row0.x + 54 * theme::scale, row0.y + 28 * theme::scale),
                 theme::WhiteU32 (theme::TextTertiary),
                 r.address.c_str ());

    // Right cluster: ↗ + timestamp, then duration + star beneath.
    float rx = row0.x + row_w - 12;
    glyph_arrow_out (dl, ImVec2 (rx - 78 * theme::scale, row0.y + 15 * theme::scale), 4.5f * theme::scale, theme::HexU32 (theme::StatusOnline, 0.75f));
    dl->AddText (app.fonts.small_, theme::fs (10.5f), ImVec2 (rx - 66 * theme::scale, row0.y + 9 * theme::scale), theme::WhiteU32 (0.30f), r.timestamp.c_str ());
    dl->AddText (app.fonts.small_, theme::fs (11.0f), ImVec2 (rx - 66 * theme::scale, row0.y + 27 * theme::scale), theme::WhiteU32 (0.28f), r.duration.c_str ());

    ImVec2 star_c (rx - 10 * theme::scale, row0.y + 32 * theme::scale);
    ImGui::SetCursorScreenPos (ImVec2 (star_c.x - 9, star_c.y - 9));
    bool star_clicked = ImGui::InvisibleButton ("##star", ImVec2 (18, 18));
    bool star_hovered = ImGui::IsItemHovered ();
    glyph_star (dl, star_c, 6.0f,
                r.starred ? theme::HexU32 (theme::StarActive)
                          : theme::WhiteU32 (star_hovered ? 0.45f : 0.22f),
                r.starred);

    if (star_clicked) {
      toggle_favorite (app.cfg, r.name, r.address); // flips r.starred via recents sweep
    } else if (dbl) {
      start_dial (app, r.address);
    } else if (clicked) {
      snprintf (app.search, sizeof (app.search), "%s", r.address.c_str ());
    }
    if (hovered)
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);

    ImGui::SetCursorScreenPos (ImVec2 (row0.x, row0.y + row_h + 2));
    ImGui::PopID ();
  }

  if (app.cfg.recents.empty ()) {
    const char * msg = "No calls yet";
    ImVec2 ts = app.fonts.small_->CalcTextSizeA (12.5f, FLT_MAX, 0, msg);
    dl->AddText (app.fonts.small_, theme::fs (12.5f), ImVec2 (p.x + (sz.x - ts.x) / 2, p.y + sz.y / 2),
                 theme::WhiteU32 (theme::TextSecondary), msg);
  }

  ImGui::EndChild ();
}

static void
ui_footer (App & app, float width, float height)
{
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 origin = ImGui::GetCursorScreenPos ();
  float y = origin.y + height / 2;

  bool registered = app.reg_status.load () == PULSE_CONNECTION_STATUS_CONNECTED;

  // Build the right-aligned cluster: dot · text · gear.
  std::string text;
  std::string status = get_status (app);
  if (!status.empty ()) {
    text = status; // transient progress / error wins the slot
  } else if (registered) {
    text = std::string ("Registered as ") + app.cfg.reg_alias + " (" + app.cfg.display_name + ")  ·  Server: " +
           app.cfg.reg_host;
  } else {
    text = "Not registered";
  }

  ImVec2 ts = app.fonts.small_->CalcTextSizeA (11.5f, FLT_MAX, 0, text.c_str ());
  float gear_r = 8.0f;
  float x_gear = origin.x + width - gear_r - 4;
  float x_text = x_gear - gear_r - 10 - ts.x;
  float x_dot = x_text - 15;

  // Status dot with glow.
  unsigned int dot = registered ? theme::StatusOnline : theme::StatusError;
  if (registered)
    dl->AddCircleFilled (ImVec2 (x_dot, y), 6.0f, theme::HexU32 (dot, 0.25f));
  dl->AddCircleFilled (ImVec2 (x_dot, y), 3.5f, theme::HexU32 (dot));

  dl->AddText (app.fonts.small_, theme::fs (11.5f), ImVec2 (x_text, y - ts.y / 2),
               app.status_is_error.load () && !status.empty () ? theme::HexU32 (theme::StatusError, 0.9f)
                                                               : theme::WhiteU32 (theme::TextTertiary),
               text.c_str ());

  ImGui::SetCursorScreenPos (ImVec2 (x_gear - gear_r, y - gear_r));
  bool gear_clicked = ImGui::InvisibleButton ("##gear", ImVec2 (gear_r * 2, gear_r * 2));
  bool gear_hovered = ImGui::IsItemHovered ();
  glyph_gear (dl, ImVec2 (x_gear, y), 7.0f, theme::WhiteU32 (gear_hovered ? 0.6f : theme::IconMuted));
  if (gear_clicked)
    app.show_settings = true;
}

static void
ui_settings (App & app)
{
  if (!app.show_settings)
    return;

  ImGui::SetNextWindowSize (ImVec2 (420, 0));
  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.98f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (18, 16));
  ImGui::Begin ("Settings", &app.show_settings,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking);

  ImGui::TextDisabled ("IDENTITY");
  ImGui::InputText ("Display name", app.cfg.display_name, sizeof (app.cfg.display_name));

  ImGui::Dummy (ImVec2 (0, 6));
  ImGui::TextDisabled ("REGISTRATION");
  ImGui::InputText ("Host / domain", app.cfg.reg_host, sizeof (app.cfg.reg_host));
  ImGui::InputText ("Device alias", app.cfg.reg_alias, sizeof (app.cfg.reg_alias));
  ImGui::Checkbox ("Authenticate with SSO", &app.cfg.reg_sso);
  if (!app.cfg.reg_sso) {
    ImGui::InputText ("Username", app.cfg.reg_user, sizeof (app.cfg.reg_user));
    ImGui::InputText ("Password", app.cfg.reg_pass, sizeof (app.cfg.reg_pass), ImGuiInputTextFlags_Password);
  }
  ImGui::Checkbox ("Register automatically on startup", &app.cfg.reg_auto);

  int reg = app.reg_status.load ();
  bool registered = reg == PULSE_CONNECTION_STATUS_CONNECTED;
  bool busy = reg == PULSE_CONNECTION_STATUS_CONNECTING || reg == PULSE_CONNECTION_STATUS_DISCONNECTING ||
              app.reg_in_flight.load ();

  ImGui::BeginDisabled (busy);
  if (registered) {
    if (ImGui::Button ("Deregister", ImVec2 (110, 0)))
      start_deregister (app);
  } else {
    if (ImGui::Button ("Register", ImVec2 (110, 0))) {
      save_config (app.cfg);
      start_register (app);
    }
  }
  ImGui::EndDisabled ();
  ImGui::SameLine ();
  ImGui::TextDisabled ("%s", registered           ? "registered"
                             : busy               ? "working…"
                                                  : "not registered");

  ImGui::Dummy (ImVec2 (0, 6));
  ImGui::TextDisabled ("DIALLING");
  ImGui::InputTextWithHint ("Default server", "used for bare aliases when not registered", app.cfg.default_server,
                            sizeof (app.cfg.default_server));

  ImGui::Dummy (ImVec2 (0, 4));
  if (ImGui::Button ("Close", ImVec2 (90, 0))) {
    save_config (app.cfg);
    app.show_settings = false;
  }

  ImGui::End ();
  ImGui::PopStyleVar ();
  ImGui::PopStyleColor ();
}

// A solid-fill button in a given colour. Plain ImGui::Button so it lives in
// whatever window it is submitted to — window-based cards need that for input
// routing (draw-list pills over a child window never receive clicks).
static bool
accent_button (App & app, const char * label, unsigned int color, ImVec2 size)
{
  ImGui::PushStyleColor (ImGuiCol_Button, theme::Hex (color, 0.92f));
  ImGui::PushStyleColor (ImGuiCol_ButtonHovered, theme::Hex (color, 1.0f));
  ImGui::PushStyleColor (ImGuiCol_ButtonActive, theme::Hex (color, 0.80f));
  ImGui::PushStyleColor (ImGuiCol_Text, theme::Hex (0xFFFFFF, 1.0f));
  ImGui::PushStyleVar (ImGuiStyleVar_FrameRounding, size.y / 2);
  ImGui::PushFont (app.fonts.bodyBold);
  bool clicked = ImGui::Button (label, size);
  ImGui::PopFont ();
  ImGui::PopStyleVar ();
  ImGui::PopStyleColor (4);
  if (ImGui::IsItemHovered ())
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
  return clicked;
}

// Media-device picker: camera / microphone / speaker combos. A normal window
// (movable, correctly input-routed) available both on the home screen and
// mid-call — selecting a device applies immediately, which live-swaps the
// session, and persists as the preference for future calls.
static void
ui_devices_window (App & app)
{
  if (!app.show_devices)
    return;

  if (app.device_list_dirty.load ())
    refresh_device_lists (app);

  ImGui::SetNextWindowSize (ImVec2 (380, 0));
  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.98f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (18, 16));
  ImGui::Begin ("Devices", &app.show_devices,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                  ImGuiWindowFlags_NoSavedSettings);

  bool changed = false;
  auto combo = [&] (const char * label, const std::vector<DevEntry> & list, std::string & preferred,
                    PulseMediaType type, PulseMediaDirection direction) {
    // Show the saved name even if the device is currently unplugged, so the
    // preference is visible (and clearable) rather than silently ignored.
    std::string current = preferred.empty () ? "System default" : preferred;
    bool present = preferred.empty ();
    for (const DevEntry & d : list)
      if (d.name == preferred)
        present = true;
    if (!present)
      current += "  (not connected)";

    if (ImGui::BeginCombo (label, current.c_str ())) {
      // Before the first call no device session exists yet — just record the
      // preference (connect_default_devices applies it at dial time) so the
      // camera doesn't light up while the app sits idle on the home screen.
      bool apply_now =
        app.devices_connected || app.call_status.load () != PULSE_CONNECTION_STATUS_DISCONNECTED;

      if (ImGui::Selectable ("System default", preferred.empty ())) {
        preferred.clear ();
        if (apply_now)
          apply_device (app, list, preferred, type, direction);
        changed = true;
      }
      for (const DevEntry & d : list) {
        std::string item = d.name + (d.is_default ? "  (default)" : "");
        if (ImGui::Selectable (item.c_str (), d.name == preferred)) {
          preferred = d.name;
          if (apply_now)
            apply_device (app, list, preferred, type, direction);
          changed = true;
        }
      }
      ImGui::EndCombo ();
    }
  };

  combo ("Camera", app.cameras, app.cfg.dev_camera, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT);
  combo ("Microphone", app.mics, app.cfg.dev_mic, PULSE_MEDIA_AUDIO, PULSE_MEDIA_INPUT);
  combo ("Speaker", app.speakers, app.cfg.dev_speaker, PULSE_MEDIA_AUDIO, PULSE_MEDIA_OUTPUT);

  // --- Background --------------------------------------------------------
  ImGui::Dummy (ImVec2 (0, 8));
  ImGui::TextDisabled ("BACKGROUND");
  {
    static const char * modes = "None\0Blur\0Replace with image\0";
    ImGui::SetNextItemWidth (-FLT_MIN);
    if (ImGui::Combo ("##bgmode", &app.cfg.bg_mode, modes)) {
      changed = true;
      rebuild_video (app);
    }
    if (app.cfg.bg_mode == 2) {
      static char path[1024] = "";
      if (ImGui::IsWindowAppearing () || (path[0] == '\0' && !app.cfg.bg_image.empty ()))
        snprintf (path, sizeof (path), "%s", app.cfg.bg_image.c_str ());
      ImGui::SetNextItemWidth (-FLT_MIN);
      if (ImGui::InputTextWithHint ("##bgimage", "path to a .png / .jpg", path, sizeof (path),
                                    ImGuiInputTextFlags_EnterReturnsTrue)) {
        app.cfg.bg_image = path;
        changed = true;
        rebuild_video (app);
      }
      ImGui::TextDisabled ("Press Enter to apply.");
    }
  }

  // --- Content sending ---------------------------------------------------
  ImGui::Dummy (ImVec2 (0, 8));
  ImGui::TextDisabled ("SENDING CONTENT");
  {
    static const char * modes = "Dual stream (content + camera)\0Composition (camera in content)\0";
    ImGui::SetNextItemWidth (-FLT_MIN);
    if (ImGui::Combo ("##contentmode", &app.cfg.content_mode, modes)) {
      changed = true;
      rebuild_video (app);
    }
    if (app.cfg.content_mode == 1) {
      static const char * corners = "Top left\0Top right\0Bottom left\0Bottom right\0";
      ImGui::SetNextItemWidth (-FLT_MIN);
      if (ImGui::Combo ("##pipcorner", &app.cfg.pip_corner, corners)) {
        changed = true;
        rebuild_video (app);
      }
      ImGui::SetNextItemWidth (-FLT_MIN);
      if (ImGui::SliderFloat ("##pipsize", &app.cfg.pip_size, 0.12f, 0.45f, "PiP size %.2f"))
        changed = true;
      if (ImGui::IsItemDeactivatedAfterEdit ())
        rebuild_video (app); // rebuild on release, not every frame of the drag
    }
    ImGui::TextDisabled ("%s", app.cfg.content_mode == 0
                                 ? "Camera and content sent as separate streams."
                                 : "Camera composited into the content stream, for far ends\nthat show only one stream.");
  }

  if (changed)
    save_config (app.cfg);

  ImGui::Dummy (ImVec2 (0, 4));
  if (ImGui::Button ("Close", ImVec2 (90, 0)))
    app.show_devices = false;

  ImGui::End ();
  ImGui::PopStyleVar ();
  ImGui::PopStyleColor ();
}

// Roster drawer: slides in from the left edge over the in-call view. A real
// window (input-routing rule), animated by moving its x position each frame.
static void
ui_roster_drawer (App & app, ImVec2 win_size)
{
  // Ease the slide toward its target; the handoff's motion budget is 250ms.
  float target = app.show_roster ? 1.0f : 0.0f;
  float dt = ImGui::GetIO ().DeltaTime;
  float step = dt / 0.20f;
  app.roster_slide += (target > app.roster_slide ? 1.0f : -1.0f) * std::min (step, std::fabs (target - app.roster_slide));
  app.roster_slide = std::min (1.0f, std::max (0.0f, app.roster_slide));
  if (app.roster_slide <= 0.001f)
    return;

  const float w = 300.0f * theme::scale;
  ImVec2 vp = ImGui::GetMainViewport ()->Pos;
  float x = vp.x + (app.roster_slide - 1.0f) * w;

  ImGui::SetNextWindowPos (ImVec2 (x, vp.y), ImGuiCond_Always);
  // Stop above the control bar so the people button stays clickable while
  // the drawer is open (at 300px it would otherwise cover the bar's left end).
  ImGui::SetNextWindowSize (ImVec2 (w, win_size.y - 76));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgStart, 0.94f));
  ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (0xFFFFFF, theme::PanelStroke));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (14, 14));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 1.0f);
  // NB: no NoBringToFrontOnFocus here — a window *created* with that flag is
  // born at the back of the z-order, i.e. permanently hidden behind the
  // full-viewport shell window.
  ImGui::Begin ("##roster", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

  ImDrawList * dl = ImGui::GetWindowDrawList ();

  std::lock_guard<std::mutex> lock (app.roster_mutex);

  // Header: "Participants (N)".
  size_t active = 0;
  if (app.roster)
    for (size_t i = 0; i < app.roster->participant_list_size; i++)
      if (app.roster->participant_list[i]->is_active_participant)
        active++;
  {
    char hdr[64];
    snprintf (hdr, sizeof (hdr), "Participants (%zu)", active);
    ImGui::PushFont (app.fonts.bodyBold);
    ImGui::TextUnformatted (hdr);
    ImGui::PopFont ();
    ImGui::Dummy (ImVec2 (0, 6));
  }

  ImGui::BeginChild ("##rosterlist", ImVec2 (0, 0), ImGuiChildFlags_None);

  const float row_h = 52.0f * theme::scale;
  for (size_t i = 0; app.roster && i < app.roster->participant_list_size; i++) {
    PulseConferenceControlParticipantEntry * e = app.roster->participant_list[i];
    if (!e->is_active_participant)
      continue;

    ImGui::PushID ((int) i);
    ImVec2 row0 = ImGui::GetCursorScreenPos ();
    float row_w = ImGui::GetContentRegionAvail ().x;

    // The row itself is deliberately NOT a button — rows have no click action,
    // and any full-row interactive item fights the control buttons drawn over
    // it for the mouse-down (which is exactly the bug this replaced). A Dummy
    // reserves the space; hover for the highlight is a plain rect test.
    ImGui::Dummy (ImVec2 (row_w, row_h));
    // AllowWhenBlockedByActiveItem: with default flags IsWindowHovered()
    // reports false whenever ANY ActiveId is held — including the window-move
    // id ImGui arms on background mouse-down — which un-submits these rows'
    // buttons on exactly the click frame.
    bool hovered =
      ImGui::IsWindowHovered (ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
      ImGui::IsMouseHoveringRect (row0, ImVec2 (row0.x + row_w, row0.y + row_h));
    if (hovered)
      dl->AddRectFilled (row0, ImVec2 (row0.x + row_w, row0.y + row_h), theme::WhiteU32 (theme::RowHover),
                         theme::RadiusRow);

    const char * name = e->active_display_name    ? e->active_display_name
                        : e->display_name         ? e->display_name
                                                  : "?";
    draw_avatar (app, dl, ImVec2 (row0.x + (6 + 16) * theme::scale, row0.y + row_h / 2), 32.0f * theme::scale, name);

    // Speaking ring around the avatar.
    if (e->is_speaking)
      dl->AddCircle (ImVec2 (row0.x + (6 + 16) * theme::scale, row0.y + row_h / 2), 18.5f * theme::scale, theme::HexU32 (theme::StatusOnline, 0.9f),
                     0, 2.0f);

    // Name (+you), role/state line.
    std::string label = name;
    if (e->is_local_participant)
      label += "  (you)";
    dl->AddText (app.fonts.bodyBold, theme::fs (13.0f), ImVec2 (row0.x + 46 * theme::scale, row0.y + 8 * theme::scale), theme::WhiteU32 (0.88f),
                 label.c_str ());

    std::string sub = e->role == PULSE_CONFERENCE_ROLE_HOST ? "Host" : "Guest";
    if (e->is_presenting)
      sub += "  ·  presenting";
    if (e->is_muted)
      sub += "  ·  muted";
    if (e->is_video_muted)
      sub += "  ·  camera off";
    dl->AddText (app.fonts.small_, theme::fs (10.5f), ImVec2 (row0.x + 46 * theme::scale, row0.y + 27 * theme::scale),
                 e->is_muted ? theme::HexU32 (theme::StatusError, 0.75f) : theme::WhiteU32 (theme::TextTertiary),
                 sub.c_str ());

    // Hover controls (rendered right-aligned): audio, video, disconnect.
    // Infinity only permits these for hosts; the entry's *_supported flags
    // reflect that, so the buttons simply don't appear for guests.
    if (hovered && !e->is_local_participant) {
      float bx = row0.x + row_w - 26;
      float by = row0.y + (row_h - 22) / 2;

      // Message this participant: select them as the direct-chat recipient
      // and open the chat drawer.
      {
        ImGui::SetCursorScreenPos (ImVec2 (bx, by));
        if (ImGui::InvisibleButton ("##pmsg", ImVec2 (22, 22))) {
          app.chat_target_uuid = e->uuid ? e->uuid : "";
          app.chat_target_name = name;
          app.show_chat = true;
        }
        glyph_chat (dl, ImVec2 (bx + 11, by + 11), 7.0f,
                    ImGui::IsItemHovered () ? theme::HexU32 (theme::AccentPrimary) : theme::WhiteU32 (0.55f));
        bx -= 28;
      }

      if (e->disconnect_supported) {
        ImGui::SetCursorScreenPos (ImVec2 (bx, by));
        if (ImGui::InvisibleButton ("##kick", ImVec2 (22, 22))) {
          PulseError err = pulse_participant_control_disconnect (app.pulse, e->uuid);
          std::fprintf (stderr, "[pexclient] kick %s -> %s\n", e->uuid, pulse_strerror (err));
        }
        ImU32 col = theme::HexU32 (theme::StatusError, ImGui::IsItemHovered () ? 1.0f : 0.7f);
        ImVec2 c (bx + 11, by + 11);
        dl->AddLine (ImVec2 (c.x - 5, c.y - 5), ImVec2 (c.x + 5, c.y + 5), col, 1.6f);
        dl->AddLine (ImVec2 (c.x - 5, c.y + 5), ImVec2 (c.x + 5, c.y - 5), col, 1.6f);
        bx -= 28;
      }

      if (e->mute_supported) {
        ImGui::SetCursorScreenPos (ImVec2 (bx, by));
        if (ImGui::InvisibleButton ("##pvid", ImVec2 (22, 22))) {
          PulseError err = pulse_participant_control_remote_video_mute (app.pulse, e->uuid, !e->is_video_muted);
          std::fprintf (stderr, "[pexclient] video-mute(%d) %s -> %s\n", !e->is_video_muted, e->uuid,
                        pulse_strerror (err));
        }
        glyph_camera (dl, ImVec2 (bx + 11, by + 11), 6.5f,
                      e->is_video_muted ? theme::HexU32 (theme::StatusError)
                                        : theme::WhiteU32 (ImGui::IsItemHovered () ? 0.9f : 0.55f));
        if (e->is_video_muted)
          dl->AddLine (ImVec2 (bx + 4, by + 4), ImVec2 (bx + 18, by + 18), theme::HexU32 (theme::StatusError), 1.4f);
        bx -= 28;

        ImGui::SetCursorScreenPos (ImVec2 (bx, by));
        if (ImGui::InvisibleButton ("##paud", ImVec2 (22, 22))) {
          PulseError err = pulse_participant_control_remote_audio_mute (app.pulse, e->uuid, !e->is_muted);
          std::fprintf (stderr, "[pexclient] audio-mute(%d) %s -> %s\n", !e->is_muted, e->uuid,
                        pulse_strerror (err));
        }
        glyph_mic (dl, ImVec2 (bx + 11, by + 11), 7.0f,
                   e->is_muted ? theme::HexU32 (theme::StatusError)
                               : theme::WhiteU32 (ImGui::IsItemHovered () ? 0.9f : 0.55f));
        if (e->is_muted)
          dl->AddLine (ImVec2 (bx + 4, by + 4), ImVec2 (bx + 18, by + 18), theme::HexU32 (theme::StatusError), 1.4f);
      }
    }

    ImGui::SetCursorScreenPos (ImVec2 (row0.x, row0.y + row_h + 2));
    ImGui::PopID ();
  }

  ImGui::EndChild ();
  ImGui::End ();
  ImGui::PopStyleVar (3);
  ImGui::PopStyleColor (2);
}

// Virtual-reception extension entry, shown while a connect is parked on
// on_conference_extension. Same modal shape as the PIN card.
static void
ui_extension_card (App & app)
{
  bool pending = app.ext_pending.load ();

  if (pending && !ImGui::IsPopupOpen ("##extmodal"))
    ImGui::OpenPopup ("##extmodal");
  if (!ImGui::IsPopupOpen ("##extmodal"))
    return;

  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::SetNextWindowSize (ImVec2 (340, 0));
  ImGui::PushStyleColor (ImGuiCol_PopupBg, theme::Hex (theme::WindowBgMid, 0.98f));
  ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (theme::AccentPrimary, 0.45f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (16, 14));
  ImGui::PushStyleVar (ImGuiStyleVar_PopupRounding, theme::RadiusPanel);

  if (ImGui::BeginPopupModal ("##extmodal", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    static char ext_buf[64] = "";

    ImGui::PushFont (app.fonts.bodyBold);
    ImGui::TextUnformatted ("Virtual reception");
    ImGui::PopFont ();
    ImGui::TextDisabled ("Enter the extension or alias to be connected to.");
    ImGui::Dummy (ImVec2 (0, 2));

    if (ImGui::IsWindowAppearing ())
      ImGui::SetKeyboardFocusHere ();
    ImGui::SetNextItemWidth (-FLT_MIN);
    bool entered = ImGui::InputTextWithHint ("##ext", "extension", ext_buf, sizeof (ext_buf),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy (ImVec2 (0, 4));

    bool go = accent_button (app, "Connect", theme::AccentPrimary, ImVec2 (92, 27)) || entered;
    ImGui::SameLine (ImGui::GetWindowWidth () - 92 - 16);
    bool cancel = accent_button (app, "Cancel", theme::StatusError, ImVec2 (92, 27));

    if (pending && go && ext_buf[0]) {
      {
        std::lock_guard<std::mutex> lock (app.ext_mutex);
        app.ext_value = ext_buf;
      }
      ext_buf[0] = '\0';
      app.ext_answer.store (1);
      ImGui::CloseCurrentPopup ();
    } else if (pending && cancel) {
      ext_buf[0] = '\0';
      app.ext_answer.store (0);
      ImGui::CloseCurrentPopup ();
    } else if (!pending) {
      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup ();
  }

  ImGui::PopStyleVar (2);
  ImGui::PopStyleColor (2);
}

// DTMF keypad. Digits go to the far end via the conference (target NULL —
// "ignored for gateway calls", which is the IVR/PSTN case this is for).
static void
ui_keypad_window (App & app)
{
  if (!app.show_keypad)
    return;

  ImGui::SetNextWindowSize (ImVec2 (250, 0));
  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.98f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (16, 14));
  ImGui::Begin ("Keypad", &app.show_keypad,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                  ImGuiWindowFlags_NoSavedSettings);

  // Echo of what has been sent, so multi-digit entry is visible.
  ImGui::PushFont (app.fonts.bodyBold);
  ImGui::TextUnformatted (app.dtmf_sent.empty () ? " " : app.dtmf_sent.c_str ());
  ImGui::PopFont ();
  ImGui::Separator ();
  ImGui::Dummy (ImVec2 (0, 4));

  static const char * rows[] = {"123", "456", "789", "*0#"};
  const float bw = 66.0f * theme::scale, bh = 40.0f * theme::scale;

  auto send_digit = [&] (char d) {
    char digit[2] = {d, '\0'};
    PulseError err = pulse_participant_control_dtmf (app.pulse, nullptr, digit);
    std::fprintf (stderr, "[pexclient] dtmf '%c' -> %s\n", d, pulse_strerror (err));
    if (err == PULSE_SUCCESS) {
      app.dtmf_sent += d;
      if (app.dtmf_sent.size () > 24)
        app.dtmf_sent.erase (0, app.dtmf_sent.size () - 24);
    } else {
      set_status (app, std::string ("DTMF: ") + pulse_strerror (err), true);
    }
  };

  for (const char * row : rows) {
    for (int i = 0; row[i]; i++) {
      ImGui::PushID (row[i]);
      char label[2] = {row[i], '\0'};
      if (accent_button (app, label, theme::AccentPrimary, ImVec2 (bw, bh)))
        send_digit (row[i]);
      ImGui::PopID ();
      if (i < 2)
        ImGui::SameLine ();
    }
  }

  // Physical keyboard entry while the window has focus.
  if (ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) {
    for (ImGuiKey k = ImGuiKey_0; k <= ImGuiKey_9; k = (ImGuiKey) (k + 1))
      if (ImGui::IsKeyPressed (k))
        send_digit ((char) ('0' + (k - ImGuiKey_0)));
    if (ImGui::IsKeyPressed (ImGuiKey_KeypadMultiply))
      send_digit ('*');
  }

  ImGui::Dummy (ImVec2 (0, 4));
  if (ImGui::Button ("Clear", ImVec2 (bw, 0)))
    app.dtmf_sent.clear ();
  ImGui::SameLine ();
  if (ImGui::Button ("Close", ImVec2 (bw, 0)))
    app.show_keypad = false;

  ImGui::End ();
  ImGui::PopStyleVar ();
  ImGui::PopStyleColor ();
}

// SSO provider chooser, shown while a registration/join is parked on
// on_sso_select. Picking a provider hands control back to Pulse, which opens
// the system browser for the IdP flow.
static void
ui_sso_card (App & app)
{
  bool pending = app.sso_pending.load ();

  if (pending && !ImGui::IsPopupOpen ("##ssomodal"))
    ImGui::OpenPopup ("##ssomodal");
  if (!ImGui::IsPopupOpen ("##ssomodal"))
    return;

  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::SetNextWindowSize (ImVec2 (340, 0));
  ImGui::PushStyleColor (ImGuiCol_PopupBg, theme::Hex (theme::WindowBgMid, 0.98f));
  ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (theme::AccentPrimary, 0.45f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (16, 14));
  ImGui::PushStyleVar (ImGuiStyleVar_PopupRounding, theme::RadiusPanel);

  if (ImGui::BeginPopupModal ("##ssomodal", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::PushFont (app.fonts.bodyBold);
    ImGui::TextUnformatted ("Sign in required");
    ImGui::PopFont ();
    ImGui::TextDisabled ("Choose an identity provider — your browser will open to sign in.");
    ImGui::Dummy (ImVec2 (0, 4));

    std::vector<std::string> providers;
    {
      std::lock_guard<std::mutex> lock (app.sso_mutex);
      providers = app.sso_providers;
    }
    for (size_t i = 0; i < providers.size (); i++) {
      ImGui::PushID ((int) i);
      if (pending && accent_button (app, providers[i].c_str (), theme::AccentPrimary,
                                    ImVec2 (ImGui::GetContentRegionAvail ().x, 28))) {
        app.sso_answer.store ((int) i);
        ImGui::CloseCurrentPopup ();
      }
      ImGui::PopID ();
      ImGui::Dummy (ImVec2 (0, 2));
    }

    ImGui::Dummy (ImVec2 (0, 2));
    if (pending && accent_button (app, "Cancel", theme::StatusError, ImVec2 (92, 26))) {
      app.sso_answer.store (-1);
      ImGui::CloseCurrentPopup ();
    }
    if (!pending)
      ImGui::CloseCurrentPopup (); // aborted elsewhere

    ImGui::EndPopup ();
  }

  ImGui::PopStyleVar (2);
  ImGui::PopStyleColor (2);
}

// Conference controls: lock, guest mute, layout, dial-out, disconnect-all.
// Host-only actions — Infinity refuses them for guests, so the panel shows a
// hint instead of controls when we joined as a guest.
static void
ui_conference_controls (App & app)
{
  if (!app.show_conf_controls)
    return;

  ImGui::SetNextWindowSize (ImVec2 (400, 0));
  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.98f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (18, 16));
  ImGui::Begin ("Conference controls", &app.show_conf_controls,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                  ImGuiWindowFlags_NoSavedSettings);

  // Fetch role + layouts once each time the panel opens.
  static PulseConferenceRole role = PULSE_CONFERENCE_ROLE_GUEST;
  if (ImGui::IsWindowAppearing ()) {
    pulse_session_get_role (app.pulse, &role);

    app.available_layouts.clear ();
    PulseConferenceControlAvailableLayoutsResponse * layouts = nullptr;
    if (pulse_conference_control_available_layouts (app.pulse, &layouts) == PULSE_SUCCESS && layouts) {
      for (size_t i = 0; i < layouts->list_size; i++)
        if (layouts->list[i])
          app.available_layouts.push_back (layouts->list[i]);
      pulse_conference_control_free_available_layouts_response (layouts);
    }
  }

  if (role != PULSE_CONFERENCE_ROLE_HOST) {
    ImGui::TextDisabled ("Joined as guest — conference controls require the host role.");
    ImGui::Dummy (ImVec2 (0, 4));
    if (ImGui::Button ("Close", ImVec2 (90, 0)))
      app.show_conf_controls = false;
    ImGui::End ();
    ImGui::PopStyleVar ();
    ImGui::PopStyleColor ();
    return;
  }

  auto report = [&] (const char * what, PulseError err) {
    if (err != PULSE_SUCCESS)
      set_status (app, std::string (what) + ": " + pulse_strerror (err), true);
  };

  // --- Conference state toggles ---------------------------------------
  ImGui::TextDisabled ("CONFERENCE");
  bool locked = app.conf_locked.load ();
  if (accent_button (app, locked ? "Unlock conference" : "Lock conference",
                     locked ? theme::StatusError : theme::AccentPrimary, ImVec2 (170, 27)))
    report ("lock", locked ? pulse_conference_control_unlock (app.pulse) : pulse_conference_control_lock (app.pulse));
  ImGui::SameLine ();
  ImGui::TextDisabled ("%s", locked ? "locked" : "unlocked");

  bool gmuted = app.conf_guests_muted.load ();
  if (accent_button (app, gmuted ? "Unmute guests" : "Mute all guests",
                     gmuted ? theme::StatusError : theme::AccentPrimary, ImVec2 (170, 27)))
    report ("guest mute", gmuted ? pulse_conference_control_unmute_guests (app.pulse)
                                 : pulse_conference_control_mute_guests (app.pulse));
  ImGui::SameLine ();
  ImGui::TextDisabled ("%s", gmuted ? "guests muted" : "guests unmuted");

  bool can_unmute = app.conf_guests_can_unmute.load ();
  if (ImGui::Checkbox ("Guests may unmute themselves", &can_unmute))
    report ("guests-can-unmute", pulse_conference_control_set_guests_can_unmute (app.pulse, can_unmute));

  // --- Layout -----------------------------------------------------------
  ImGui::Dummy (ImVec2 (0, 6));
  ImGui::TextDisabled ("LAYOUT");
  {
    std::string current;
    {
      std::lock_guard<std::mutex> lock (app.layout_mutex);
      current = app.current_layout.empty () ? "(server default)" : app.current_layout;
    }
    ImGui::SetNextItemWidth (-FLT_MIN);
    if (ImGui::BeginCombo ("##layout", current.c_str ())) {
      for (const std::string & l : app.available_layouts) {
        if (ImGui::Selectable (l.c_str (), l == current)) {
          PulseConferenceControlTransformLayoutRequest req{};
          req.layout = l.c_str (); // VMR-style; every other field left unset
          report ("layout", pulse_conference_control_transform_layout (app.pulse, &req));
        }
      }
      if (app.available_layouts.empty ())
        ImGui::TextDisabled ("no layouts reported");
      ImGui::EndCombo ();
    }
  }

  // --- Add participant --------------------------------------------------
  ImGui::Dummy (ImVec2 (0, 6));
  ImGui::TextDisabled ("ADD PARTICIPANT");
  ImGui::SetNextItemWidth (ImGui::GetContentRegionAvail ().x - 150);
  bool entered = ImGui::InputTextWithHint ("##dialout", "address to call, e.g. alice@example.com", app.dial_out_addr,
                                           sizeof (app.dial_out_addr), ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine ();
  ImGui::SetNextItemWidth (68);
  ImGui::Combo ("##dialrole", &app.dial_out_role, "Host\0Guest\0");
  ImGui::SameLine ();
  if ((accent_button (app, "Call", theme::AccentPrimary, ImVec2 (54, ImGui::GetFrameHeight ())) || entered) &&
      app.dial_out_addr[0]) {
    PulseConferenceControlDialRequest req{};
    req.role = app.dial_out_role == 0 ? PULSE_CONFERENCE_ROLE_HOST : PULSE_CONFERENCE_ROLE_GUEST;
    req.destination = app.dial_out_addr;
    req.protocol = PULSE_CONFERENCE_PROTOCOL_AUTO; // let Call Routing Rules decide
    PulseConferenceControlDialResponse * resp = nullptr;
    PulseError err = pulse_conference_control_dial (app.pulse, &req, &resp);
    report ("dial-out", err);
    pulse_conference_control_free_dial_participant_response (resp);
    if (err == PULSE_SUCCESS) {
      set_status (app, std::string ("Calling ") + app.dial_out_addr + "…");
      app.dial_out_addr[0] = '\0';
    }
  }

  // --- Danger zone ------------------------------------------------------
  ImGui::Dummy (ImVec2 (0, 6));
  ImGui::TextDisabled ("DANGER ZONE");
  // Two-step: first click arms for 3 seconds, second click fires.
  float now = (float) ImGui::GetTime ();
  bool armed = now < app.disconnect_confirm_until;
  if (accent_button (app, armed ? "Confirm: disconnect everyone" : "Disconnect all participants", theme::StatusError,
                     ImVec2 (230, 27))) {
    if (armed) {
      app.disconnect_confirm_until = 0;
      report ("disconnect-all", pulse_conference_control_disconnect (app.pulse));
    } else {
      app.disconnect_confirm_until = now + 3.0f;
    }
  }
  if (armed) {
    ImGui::SameLine ();
    ImGui::TextDisabled ("click again to confirm");
  }

  ImGui::Dummy (ImVec2 (0, 4));
  if (ImGui::Button ("Close", ImVec2 (90, 0)))
    app.show_conf_controls = false;

  ImGui::End ();
  ImGui::PopStyleVar ();
  ImGui::PopStyleColor ();
}

// Chat drawer: slides in from the right edge, mirroring the roster drawer.
static void
ui_chat_drawer (App & app, ImVec2 win_size)
{
  float target = app.show_chat ? 1.0f : 0.0f;
  float dt = ImGui::GetIO ().DeltaTime;
  float step = dt / 0.20f;
  app.chat_slide += (target > app.chat_slide ? 1.0f : -1.0f) * std::min (step, std::fabs (target - app.chat_slide));
  app.chat_slide = std::min (1.0f, std::max (0.0f, app.chat_slide));
  if (app.chat_slide <= 0.001f)
    return;

  if (app.show_chat)
    app.chat_unread.store (0);

  const float w = 300.0f * theme::scale;
  ImVec2 vp = ImGui::GetMainViewport ()->Pos;
  float x = vp.x + win_size.x - app.chat_slide * w;

  ImGui::SetNextWindowPos (ImVec2 (x, vp.y), ImGuiCond_Always);
  ImGui::SetNextWindowSize (ImVec2 (w, win_size.y - 76)); // stops above the control bar
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgStart, 0.94f));
  ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (0xFFFFFF, theme::PanelStroke));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (14, 14));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::Begin ("##chat", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

  ImGui::PushFont (app.fonts.bodyBold);
  ImGui::TextUnformatted ("Chat");
  ImGui::PopFont ();
  ImGui::Dummy (ImVec2 (0, 4));

  // Recipient selector: Everyone (broadcast) or one participant (direct).
  {
    std::lock_guard<std::mutex> roster_lock (app.roster_mutex);

    // Drop the selection if the participant has left.
    if (!app.chat_target_uuid.empty ()) {
      bool present = false;
      for (size_t i = 0; app.roster && i < app.roster->participant_list_size; i++) {
        PulseConferenceControlParticipantEntry * e = app.roster->participant_list[i];
        if (e->is_active_participant && e->uuid && app.chat_target_uuid == e->uuid)
          present = true;
      }
      if (!present) {
        app.chat_target_uuid.clear ();
        app.chat_target_name.clear ();
      }
    }

    ImGui::SetNextItemWidth (-FLT_MIN);
    std::string current = app.chat_target_uuid.empty () ? "To: Everyone" : "To: " + app.chat_target_name;
    if (ImGui::BeginCombo ("##chattarget", current.c_str ())) {
      if (ImGui::Selectable ("Everyone", app.chat_target_uuid.empty ())) {
        app.chat_target_uuid.clear ();
        app.chat_target_name.clear ();
      }
      for (size_t i = 0; app.roster && i < app.roster->participant_list_size; i++) {
        PulseConferenceControlParticipantEntry * e = app.roster->participant_list[i];
        if (!e->is_active_participant || e->is_local_participant || !e->uuid)
          continue;
        const char * name = e->active_display_name ? e->active_display_name
                            : e->display_name      ? e->display_name
                                                   : "?";
        ImGui::PushID ((int) i);
        if (ImGui::Selectable (name, app.chat_target_uuid == e->uuid)) {
          app.chat_target_uuid = e->uuid;
          app.chat_target_name = name;
        }
        ImGui::PopID ();
      }
      ImGui::EndCombo ();
    }
    ImGui::Dummy (ImVec2 (0, 4));
  }

  // Transcript: fills everything above the input row.
  const float input_h = ImGui::GetFrameHeight () + 10;
  ImGui::BeginChild ("##chatlog", ImVec2 (0, -input_h), ImGuiChildFlags_None);
  {
    std::lock_guard<std::mutex> lock (app.chat_mutex);
    for (const App::ChatMsg & m : app.chat) {
      // Header line: sender (accent for self) · time · direct tag.
      ImGui::PushFont (app.fonts.smallMed);
      ImGui::TextColored (m.self ? theme::Hex (theme::AccentPrimary) : theme::Hex (0xFFFFFF, theme::TextSecondary),
                          "%s", m.origin.c_str ());
      ImGui::PopFont ();
      ImGui::SameLine ();
      ImGui::PushFont (app.fonts.small_);
      ImGui::TextColored (theme::Hex (0xFFFFFF, theme::TextTertiary), "%s%s", m.time.c_str (),
                          m.direct ? "  ·  direct" : "");
      ImGui::PopFont ();
      ImGui::PushTextWrapPos (0.0f);
      ImGui::TextUnformatted (m.text.c_str ());
      ImGui::PopTextWrapPos ();
      ImGui::Dummy (ImVec2 (0, 6));
    }
  }
  if (app.chat_scroll_to_bottom) {
    ImGui::SetScrollHereY (1.0f);
    app.chat_scroll_to_bottom = false;
  }
  ImGui::EndChild ();

  // Input row: text field + Send.
  std::string hint =
    app.chat_target_uuid.empty () ? "Message everyone" : "Message " + app.chat_target_name + " (direct)";
  ImGui::SetNextItemWidth (ImGui::GetContentRegionAvail ().x - 62);
  bool entered = ImGui::InputTextWithHint ("##chatinput", hint.c_str (), app.chat_input, sizeof (app.chat_input),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine ();
  bool send = accent_button (app, "Send", theme::AccentPrimary, ImVec2 (54, ImGui::GetFrameHeight ())) || entered;

  if (send && app.chat_input[0]) {
    send_chat (app, app.chat_input);
    app.chat_input[0] = '\0';
    ImGui::SetKeyboardFocusHere (-1); // keep typing without re-clicking the field
  }

  ImGui::End ();
  ImGui::PopStyleVar (3);
  ImGui::PopStyleColor (2);
}

// Share picker: displays and windows, refreshed each time it opens. Clicking
// a source starts (or switches) the presentation; a Stop row ends it.
static void
ui_share_window (App & app)
{
  if (!app.show_share)
    return;

  ImGui::SetNextWindowSize (ImVec2 (420, 0));
  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.98f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (18, 16));
  ImGui::Begin ("Share content", &app.show_share,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                  ImGuiWindowFlags_NoSavedSettings);

  if (ImGui::IsWindowAppearing ())
    app.share_sources = enumerate_share_sources ();

  if (app.presenting) {
    if (accent_button (app, "Stop presenting", theme::StatusError, ImVec2 (140, 27))) {
      stop_share (app);
      app.show_share = false;
    }
    ImGui::Dummy (ImVec2 (0, 4));
    ImGui::Separator ();
    ImGui::Dummy (ImVec2 (0, 4));
  }

  bool any_display = false, any_window = false;
  for (const App::ShareSource & s : app.share_sources)
    (s.is_display ? any_display : any_window) = true;

  if (!any_display && !any_window)
    ImGui::TextDisabled ("No shareable displays or windows found.");

  if (any_display) {
    ImGui::TextDisabled ("DISPLAYS");
    for (size_t i = 0; i < app.share_sources.size (); i++) {
      const App::ShareSource & s = app.share_sources[i];
      if (!s.is_display)
        continue;
      ImGui::PushID ((int) i);
      if (ImGui::Selectable (s.name.c_str ())) {
        start_share (app, s);
        app.show_share = false;
      }
      ImGui::PopID ();
    }
    ImGui::Dummy (ImVec2 (0, 4));
  }

  if (any_window) {
    ImGui::TextDisabled ("WINDOWS");
    ImGui::BeginChild ("##sharewins", ImVec2 (0, 220), ImGuiChildFlags_None);
    for (size_t i = 0; i < app.share_sources.size (); i++) {
      const App::ShareSource & s = app.share_sources[i];
      if (s.is_display)
        continue;
      ImGui::PushID ((int) i);
      if (ImGui::Selectable (s.name.c_str ())) {
        start_share (app, s);
        app.show_share = false;
      }
      ImGui::PopID ();
    }
    ImGui::EndChild ();
  }

  ImGui::End ();
  ImGui::PopStyleVar ();
  ImGui::PopStyleColor ();
}

// Incoming-call banner: its own top-centred window so its buttons are
// hoverable above every other window (shell, child panels, in-call view).
static void
ui_incoming_banner (App & app, ImVec2 win_size)
{
  if (!app.incoming_pending.load ())
    return;

  std::string remote, alias;
  {
    std::lock_guard<std::mutex> lock (app.incoming_mutex);
    remote = app.incoming_remote;
    alias = app.incoming_alias;
  }

  ImVec2 vp = ImGui::GetMainViewport ()->Pos;
  ImGui::SetNextWindowPos (ImVec2 (vp.x + win_size.x / 2, vp.y + 52), ImGuiCond_Always, ImVec2 (0.5f, 0.0f));
  ImGui::SetNextWindowSize (ImVec2 (340, 0));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.97f));
  ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (theme::AccentPrimary, 0.45f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (16, 14));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowRounding, theme::RadiusPanel);
  ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::Begin ("##incoming", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

  ImGui::PushFont (app.fonts.bodyBold);
  ImGui::TextUnformatted (remote.c_str ());
  ImGui::PopFont ();
  ImGui::TextDisabled ("Incoming call · %s", alias.c_str ());
  ImGui::Dummy (ImVec2 (0, 4));

  if (accent_button (app, "Accept", theme::StatusOnline, ImVec2 (92, 28)))
    app.incoming_answer.store (1);
  ImGui::SameLine (ImGui::GetWindowWidth () - 92 - 16);
  if (accent_button (app, "Decline", theme::StatusError, ImVec2 (92, 28)))
    app.incoming_answer.store (0);

  ImGui::End ();
  ImGui::PopStyleVar (3);
  ImGui::PopStyleColor (2);
}

// ----------------------------------------------------------------------------
//  In-call view — remote video letterboxed full-window, rounded self-view,
//  duration + End call pill.
// ----------------------------------------------------------------------------

static void
ui_in_call (App & app, ImVec2 win_size)
{
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 p = ImGui::GetWindowPos ();

  pump_video_ctx (app.pulse, app.remote_ctx);
  pump_video_ctx (app.pulse, app.self_ctx);
  if (app.remote_presenting.load ())
    pump_video_ctx (app.pulse, app.preso_ctx);

  // When the far end presents, the presentation takes the big pane and the
  // remote video drops to a thumbnail beside the self-view.
  bool show_preso = app.remote_presenting.load () && app.preso_ctx.width > 0;
  GLVideoCtx & big = show_preso ? app.preso_ctx : app.remote_ctx;

  // Big pane, letterboxed onto the shell background.
  if (big.width > 0) {
    float scale = std::min (win_size.x / big.width, win_size.y / big.height);
    ImVec2 vs (big.width * scale, big.height * scale);
    ImVec2 v0 (p.x + (win_size.x - vs.x) / 2, p.y + (win_size.y - vs.y) / 2);
    dl->AddImage ((ImTextureID) (intptr_t) big.texture, v0, ImVec2 (v0.x + vs.x, v0.y + vs.y));
  }

  const float sw = 176, sh = 99;

  // Remote-video thumbnail (only while a presentation holds the big pane):
  // fixed at bottom-right, above the control bar.
  if (show_preso && app.remote_ctx.width > 0) {
    ImVec2 s1 (p.x + win_size.x - 20, p.y + win_size.y - 84);
    ImVec2 s0 (s1.x - sw, s1.y - sh);
    dl->AddImageRounded ((ImTextureID) (intptr_t) app.remote_ctx.texture, s0, s1, ImVec2 (0, 0), ImVec2 (1, 1),
                         IM_COL32_WHITE, theme::RadiusControl);
    dl->AddRect (s0, s1, theme::WhiteU32 (0.20f), theme::RadiusControl, 0, 1.0f);
  }

  // Floating self-view: drag anywhere (kept clear of the control bar);
  // hover reveals a hide button, and a small corner chip restores it.
  if (!app.selfview_hidden && app.self_ctx.width > 0) {
    // Usable area for the thumb's top-left corner.
    const float margin = 12.0f;
    float min_x = p.x + margin, max_x = p.x + win_size.x - sw - margin;
    float min_y = p.y + margin, max_y = p.y + win_size.y - 76 - sh; // never over the bar
    ImVec2 s0 (min_x + app.selfview_pos.x * (max_x - min_x), min_y + app.selfview_pos.y * (max_y - min_y));
    ImVec2 s1 (s0.x + sw, s0.y + sh);

    // Drag handle covering the whole thumb. AllowOverlap so the hide button
    // in the corner (submitted after) can win its clicks — see the ImGui
    // overlay pitfalls note.
    ImGui::SetCursorScreenPos (s0);
    ImGui::SetNextItemAllowOverlap ();
    ImGui::InvisibleButton ("##selfview_drag", ImVec2 (sw, sh));
    // Rect-based hover, NOT IsItemHovered: when the pointer reaches the hide
    // button it steals item-hover from the drag handle, and an item-hover gate
    // would un-submit the hide button on alternating frames (flicker).
    bool thumb_hovered = ImGui::IsItemActive () ||
                         (ImGui::IsWindowHovered (ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                          ImGui::IsMouseHoveringRect (s0, s1));
    if (ImGui::IsItemActive ()) {
      app.selfview_drag_active = true;
      ImVec2 d = ImGui::GetIO ().MouseDelta;
      float nx = app.selfview_pos.x + d.x / std::max (1.0f, max_x - min_x);
      float ny = app.selfview_pos.y + d.y / std::max (1.0f, max_y - min_y);
      app.selfview_pos.x = std::min (1.0f, std::max (0.0f, nx));
      app.selfview_pos.y = std::min (1.0f, std::max (0.0f, ny));
    } else {
      app.selfview_drag_active = false;
    }

    dl->AddImageRounded ((ImTextureID) (intptr_t) app.self_ctx.texture, s0, s1, ImVec2 (0, 0), ImVec2 (1, 1),
                         IM_COL32_WHITE, theme::RadiusControl);
    dl->AddRect (s0, s1,
                 app.selfview_drag_active ? theme::HexU32 (theme::AccentPrimary, 0.8f) : theme::WhiteU32 (0.20f),
                 theme::RadiusControl, 0, app.selfview_drag_active ? 1.5f : 1.0f);

    if (thumb_hovered) {
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
      // Hide button, top-right corner of the thumb (submitted after the drag
      // handle so it wins the overlap).
      ImVec2 h0 (s1.x - 24, s0.y + 4);
      ImGui::SetCursorScreenPos (h0);
      if (ImGui::InvisibleButton ("##selfview_hide", ImVec2 (20, 20)))
        app.selfview_hidden = true;
      ImU32 col = theme::WhiteU32 (ImGui::IsItemHovered () ? 0.95f : 0.6f);
      dl->AddCircleFilled (ImVec2 (h0.x + 10, h0.y + 10), 9.0f, theme::HexU32 (theme::WindowBgStart, 0.75f));
      dl->AddLine (ImVec2 (h0.x + 6, h0.y + 10), ImVec2 (h0.x + 14, h0.y + 10), col, 1.6f); // minimise dash
    }
  } else if (app.selfview_hidden) {
    // Restore chip: small camera pill, bottom-right above the control bar.
    ImVec2 c1 (p.x + win_size.x - 20, p.y + win_size.y - 84);
    ImVec2 c0 (c1.x - 44, c1.y - 28);
    ImGui::SetCursorScreenPos (c0);
    bool clicked = ImGui::InvisibleButton ("##selfview_show", ImVec2 (44, 28));
    bool hovered = ImGui::IsItemHovered ();
    dl->AddRectFilled (c0, c1, theme::WhiteU32 (hovered ? 0.14f : 0.08f), 14.0f);
    dl->AddRect (c0, c1, theme::WhiteU32 (theme::PanelStroke), 14.0f, 0, 1.0f);
    glyph_camera (dl, ImVec2 ((c0.x + c1.x) / 2, (c0.y + c1.y) / 2), 7.0f,
                  theme::WhiteU32 (hovered ? 0.9f : 0.55f));
    if (hovered)
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
    if (clicked)
      app.selfview_hidden = false;
  }

  // Live caption overlay: dark pill above the control bar while fresh text
  // exists. Finals age out after 6 seconds of caption silence.
  if (app.captions_on) {
    std::string text;
    {
      std::lock_guard<std::mutex> lock (app.caption_mutex);
      auto age = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - app.caption_at);
      if (!app.caption_text.empty () && age.count () < 6)
        text = app.caption_text;
    }
    if (!text.empty ()) {
      float wrap_w = std::min (620.0f, win_size.x - 160.0f);
      ImVec2 ts = app.fonts.body->CalcTextSizeA (14.5f, wrap_w, wrap_w, text.c_str ());
      ImVec2 c0 (p.x + (win_size.x - ts.x) / 2 - 14, p.y + win_size.y - 80 - ts.y - 18);
      ImVec2 c1 (c0.x + ts.x + 28, c0.y + ts.y + 16);
      dl->AddRectFilled (c0, c1, theme::HexU32 (theme::WindowBgStart, 0.80f), 10.0f);
      dl->AddText (app.fonts.body, theme::fs (14.5f), ImVec2 (c0.x + 14, c0.y + 8), theme::WhiteU32 (0.95f), text.c_str (),
                   nullptr, wrap_w);
    }
  }

  // Header: callee + elapsed time.
  {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () -
                                                                     app.call_started)
                     .count ();
    char hud[512];
    snprintf (hud, sizeof (hud), "%s   ·   %02lld:%02lld", app.call_address.c_str (), elapsed / 60, elapsed % 60);
    ImVec2 ts = app.fonts.small_->CalcTextSizeA (11.5f, FLT_MAX, 0, hud);
    ImVec2 h0 (p.x + (win_size.x - ts.x) / 2 - 12, p.y + 16);
    ImVec2 h1 (p.x + (win_size.x + ts.x) / 2 + 12, p.y + 16 + ts.y + 10);
    dl->AddRectFilled (h0, h1, theme::HexU32 (theme::WindowBgStart, 0.55f), (h1.y - h0.y) / 2);
    dl->AddText (app.fonts.small_, theme::fs (11.5f), ImVec2 (h0.x + 12, h0.y + 5), theme::WhiteU32 (0.85f), hud);
  }

  // Control bar: share + mic on the left, End call centre, camera + devices
  // on the right.
  {
    const float bw = 110, bh = 36, gap = 10;
    float bar_w = bh + gap + bh + gap + bh + gap + bh + gap + bw + gap + (bh + gap) * 5 - gap;
    float x = p.x + (win_size.x - bar_w) / 2;
    float y = p.y + win_size.y - bh - 22;

    // Scrim behind the bar. The buttons are translucent glass, which vanishes
    // over bright video; a dark rounded plate plus a soft fade underneath
    // keeps them legible over any content.
    {
      const float pad_x = 14.0f, pad_y = 10.0f;
      ImVec2 s0 (x - pad_x, y - pad_y), s1 (x + bar_w + pad_x, y + bh + pad_y);

      // Vertical fade from transparent to the shell colour, anchored to the
      // window bottom, so the plate doesn't read as a floating slab.
      ImU32 top = theme::HexU32 (theme::WindowBgStart, 0.0f);
      ImU32 bottom = theme::HexU32 (theme::WindowBgStart, 0.55f);
      dl->AddRectFilledMultiColor (ImVec2 (p.x, s0.y - 26), ImVec2 (p.x + win_size.x, p.y + win_size.y), top, top,
                                   bottom, bottom);

      dl->AddRectFilled (s0, s1, theme::HexU32 (theme::WindowBgStart, 0.72f), (s1.y - s0.y) / 2);
      dl->AddRect (s0, s1, theme::WhiteU32 (0.10f), (s1.y - s0.y) / 2, 0, 1.0f);
    }

    if (glyph_button (app, dl, "##callroster", ImVec2 (x, y), bh, app.show_roster, glyph_people))
      app.show_roster = !app.show_roster;
    x += bh + gap;

    if (glyph_button (app, dl, "##callchat", ImVec2 (x, y), bh, app.show_chat, glyph_chat))
      app.show_chat = !app.show_chat;
    // Unread badge, top-right of the chat button.
    int unread = app.chat_unread.load ();
    if (unread > 0 && !app.show_chat) {
      char n[8];
      snprintf (n, sizeof (n), "%d", std::min (unread, 99));
      ImVec2 bc (x + bh - 4, y + 4);
      dl->AddCircleFilled (bc, 8.0f, theme::HexU32 (theme::StatusError));
      ImVec2 ts = app.fonts.small_->CalcTextSizeA (10.0f, FLT_MAX, 0, n);
      dl->AddText (app.fonts.small_, theme::fs (10.0f), ImVec2 (bc.x - ts.x / 2, bc.y - ts.y / 2), theme::WhiteU32 (1.0f), n);
    }
    x += bh + gap;

    if (glyph_button (app, dl, "##callshare", ImVec2 (x, y), bh, app.presenting, glyph_screen))
      app.show_share = !app.show_share;
    x += bh + gap;

    if (mute_toggle_button (app, dl, "##callmic", ImVec2 (x, y), bh, app.mic_muted, false)) {
      app.mic_muted = !app.mic_muted;
      pulse_mute_audio_input (app.pulse, app.mic_muted);
    }

    ImVec2 b0 (x + bh + gap, y), b1 (b0.x + bw, y + bh);
    ImGui::SetCursorScreenPos (b0);
    bool clicked = ImGui::InvisibleButton ("##hangup", ImVec2 (bw, bh));
    bool hovered = ImGui::IsItemHovered ();
    dl->AddRectFilled (b0, b1, theme::HexU32 (theme::StatusError, hovered ? 1.0f : 0.92f), bh / 2);
    const char * lbl = "End call";
    ImVec2 ts = app.fonts.bodyBold->CalcTextSizeA (13.0f, FLT_MAX, 0, lbl);
    dl->AddText (app.fonts.bodyBold, theme::fs (13.0f), ImVec2 ((b0.x + b1.x - ts.x) / 2, (b0.y + b1.y - ts.y) / 2),
                 theme::WhiteU32 (1.0f), lbl);
    if (hovered)
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
    if (clicked)
      start_hangup (app);

    if (mute_toggle_button (app, dl, "##callcam", ImVec2 (b1.x + gap, y), bh, app.cam_muted, true)) {
      app.cam_muted = !app.cam_muted;
      pulse_mute_video_input (app.pulse, app.cam_muted);
    }
    if (glyph_button (app, dl, "##calldev", ImVec2 (b1.x + gap + bh + gap, y), bh, app.show_devices, glyph_sliders))
      app.show_devices = !app.show_devices;

    if (glyph_button (app, dl, "##callcc", ImVec2 (b1.x + gap + (bh + gap) * 2, y), bh, app.captions_on, glyph_cc)) {
      if (!app.captions_available.load ()) {
        set_status (app, "Live captions are not available in this conference.", true);
      } else {
        app.captions_on = !app.captions_on;
        PulseError err = app.captions_on ? pulse_participant_control_show_live_captions (app.pulse, nullptr)
                                         : pulse_participant_control_hide_live_captions (app.pulse, nullptr);
        if (err != PULSE_SUCCESS) {
          set_status (app, std::string ("captions: ") + pulse_strerror (err), true);
          app.captions_on = false;
        }
      }
    }

    if (glyph_button (app, dl, "##callconf", ImVec2 (b1.x + gap + (bh + gap) * 3, y), bh, app.show_conf_controls,
                      glyph_dots))
      app.show_conf_controls = !app.show_conf_controls;

    if (glyph_button (app, dl, "##callkeypad", ImVec2 (b1.x + gap + (bh + gap) * 4, y), bh, app.show_keypad,
                      glyph_keypad))
      app.show_keypad = !app.show_keypad;
  }
}

// PIN entry, shown while a connect is parked on on_pin_request. A real modal
// popup: gets the dimmed backdrop, top-of-stack input routing and keyboard
// capture from ImGui rather than hand-rolled draw-list widgets.
static void
ui_pin_card (App & app, ImVec2)
{
  bool pending = app.pin_pending.load ();

  if (pending && !ImGui::IsPopupOpen ("##pinmodal"))
    ImGui::OpenPopup ("##pinmodal");

  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::SetNextWindowSize (ImVec2 (340, 0));
  ImGui::PushStyleColor (ImGuiCol_PopupBg, theme::Hex (theme::WindowBgMid, 0.98f));
  ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (theme::AccentPrimary, 0.45f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (16, 14));
  ImGui::PushStyleVar (ImGuiStyleVar_PopupRounding, theme::RadiusPanel);

  if (ImGui::BeginPopupModal ("##pinmodal", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    static char pin_buf[32] = "";

    ImGui::PushFont (app.fonts.bodyBold);
    ImGui::TextUnformatted ("PIN required");
    ImGui::PopFont ();
    ImGui::TextDisabled ("%s", app.pin_guest_required.load ()
                                 ? "This conference requires a PIN to join."
                                 : "Enter the host PIN, or join without one as a guest.");
    ImGui::Dummy (ImVec2 (0, 2));

    if (ImGui::IsWindowAppearing ())
      ImGui::SetKeyboardFocusHere ();
    ImGui::SetNextItemWidth (-FLT_MIN);
    bool entered = ImGui::InputTextWithHint ("##pin", "PIN", pin_buf, sizeof (pin_buf),
                                             ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CharsDecimal |
                                               ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy (ImVec2 (0, 4));

    bool join = accent_button (app, "Join", theme::AccentPrimary, ImVec2 (92, 27)) || entered;
    ImGui::SameLine (ImGui::GetWindowWidth () - 92 - 16);
    bool cancel = accent_button (app, "Cancel", theme::StatusError, ImVec2 (92, 27));

    if (pending && join) {
      {
        std::lock_guard<std::mutex> lock (app.pin_mutex);
        app.pin_value = pin_buf;
      }
      pin_buf[0] = '\0';
      app.pin_answer.store (1); // releases on_pin_request
      ImGui::CloseCurrentPopup ();
    } else if (pending && cancel) {
      pin_buf[0] = '\0';
      app.pin_answer.store (0);
      ImGui::CloseCurrentPopup ();
    } else if (!pending) {
      // Answered or aborted elsewhere (e.g. remote cancel) — close.
      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup ();
  }

  ImGui::PopStyleVar (2);
  ImGui::PopStyleColor (2);
}

// ----------------------------------------------------------------------------
//  main
// ----------------------------------------------------------------------------

// Forward Pulse's warnings/errors to stderr so failed joins are diagnosable.
static void
on_pulse_log (void *, PulseDebugLevel level, const char * category, int64_t, int64_t, unsigned int, const char *,
              const char *, int, const char *, const char * message)
{
  if (level > PULSE_LEVEL_WARNING)
    return;
  std::fprintf (stderr, "[pulse:%s] %s\n", category ? category : "?", message ? message : "");
}

// SSO provider selection. Pulse hands us the deployment's IdP list and parks
// this worker thread until the UI answers with an index (or -1 to abort);
// Pulse then runs the browser authentication flow itself. On reconnects with
// a single provider there is nothing to choose — answer immediately.
// (This callback existing is also why registration works at all on macOS:
// pulse_register requires a pulse_new_with_internal_sso_handling() handle.)
static int
on_sso_select (PulseSSOProviderList * list, void * user_context)
{
  auto * app = static_cast<App *> (user_context);
  if (list == nullptr || list->num <= 0)
    return -1;
  if (list->num == 1 && list->is_reconnect)
    return 0;

  {
    std::lock_guard<std::mutex> lock (app->sso_mutex);
    app->sso_providers.clear ();
    for (int i = 0; i < list->num; i++)
      app->sso_providers.push_back (list->providers[i].name ? list->providers[i].name : "?");
  }
  app->sso_answer.store (-2);
  app->sso_pending.store (true);

  while (app->sso_answer.load () == -2)
    std::this_thread::sleep_for (std::chrono::milliseconds (100));

  app->sso_pending.store (false);
  int choice = app->sso_answer.load ();
  std::fprintf (stderr, "[pexclient] sso provider selection: %d of %d\n", choice, list->num);
  return choice;
}

int
main (int argc, char ** argv)
{
#if defined(PEXCLIENT_DEFAULT_CWD) && defined(__APPLE__)
  // When launched from the .app bundle, LaunchServices sets the working
  // directory to Contents/Resources *inside the bundle* — where a written
  // config would be destroyed by the next make-bundle.sh run (it rebuilds the
  // bundle from scratch). Hop to the source tree so the bundle and the
  // terminal launcher share one pexclient-config.txt.
  // GLFW's Cocoa backend chdirs into Contents/Resources during glfwInit()
  // unless this hint is cleared — it would undo the chdir below.
  glfwInitHint (GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);

  if (argc > 0 && strstr (argv[0], ".app/Contents/MacOS/") != nullptr)
    (void) chdir (PEXCLIENT_DEFAULT_CWD);
#endif

  if (!glfwInit ()) {
    std::fprintf (stderr, "glfwInit failed\n");
    return 1;
  }
  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  GLFWwindow * window = glfwCreateWindow (820, 580, "Pexip", nullptr, nullptr);
  if (!window) {
    std::fprintf (stderr, "glfwCreateWindow failed\n");
    return 1;
  }
  glfwSetWindowSizeLimits (window, 680, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);
  glfwMakeContextCurrent (window);
  glfwSwapInterval (1);

  IMGUI_CHECKVERSION ();
  ImGui::CreateContext ();
  ImGuiIO & io = ImGui::GetIO ();
  io.IniFilename = nullptr;

  App app;
  app.window = window;
  load_config (app.cfg);

  float xscale = 1.0f, yscale = 1.0f;
  glfwGetWindowContentScale (window, &xscale, &yscale);
  theme::scale = app.cfg.ui_scale; // must be set before fonts/metrics are sized
  app.fonts = theme::LoadFonts (io, PEXCLIENT_ASSET_DIR "/fonts", xscale);
  theme::Apply ();

  ImGui_ImplGlfw_InitForOpenGL (window, true);
  ImGui_ImplOpenGL3_Init ("#version 150");

  app.bg_texture = make_background_texture ();
  app.avatar_texture = make_avatar_texture ();

  // --- Pulse -------------------------------------------------------------
  // The logger must be installed before the first pulse_new* call.
  pulse_global_logger_callback (on_pulse_log, nullptr);
#if defined(_WIN32)
  // The Windows Pulse build does not export the internal-SSO constructor (no
  // pexip-auth:// deep-link plumbing there yet), so SSO joins/registration
  // are unavailable on this platform — everything else works identically.
  (void) &on_sso_select;
  app.pulse = pulse_new ();
#else
  app.pulse = pulse_new_with_internal_sso_handling (argc, (const char **) argv, on_sso_select, &app);
#endif
  if (!app.pulse) {
    std::fprintf (stderr, "pulse_new() returned NULL\n");
    return 1;
  }

  // We render all video ourselves.
  pulse_options_set_self_view_window_handle (app.pulse, nullptr);
  pulse_options_set_remote_video_window_handle (app.pulse, nullptr);
  pulse_options_set_presentation_video_window_handle (app.pulse, nullptr);
  pulse_options_set_application_user_agent_string (app.pulse, "pexclient/0.1");
  pulse_options_set_background_image (app.pulse, PEXCLIENT_ASSET_DIR "/pexclient-background.png");

  PulseRegistrationStatusCallbackConfig reg_cb{on_reg_status, &app};
  pulse_options_set_registration_state_callback (app.pulse, &reg_cb);
  PulseConferenceStatusCallbackConfig conf_cb{on_call_status, &app};
  pulse_options_set_conference_state_callback (app.pulse, &conf_cb);
  PulsePinCodeRequestCallbackConfig pin_cb{};
  pin_cb.func = on_pin_request;
  pin_cb.user_context = &app;
  pulse_options_set_pin_code_request_callbacks (app.pulse, &pin_cb);
  PulseConferenceExtensionRequestCallbackConfig ext_cb{};
  ext_cb.func = on_conference_extension;
  ext_cb.user_context = &app;
  pulse_options_set_conference_extension_request_callback (app.pulse, &ext_cb);

  // Re-enumerate the device combos when hardware comes and goes.
  pulse_register_device_list_changed_callback (app.pulse, PULSE_MEDIA_AUDIO, on_device_list_changed, &app);
  pulse_register_device_list_changed_callback (app.pulse, PULSE_MEDIA_VIDEO, on_device_list_changed, &app);

  // Far-end presentation lifecycle (drives the in-call big-pane switch).
  pulse_options_set_conference_event_presentation_start_callback (app.pulse, on_presentation_start, &app);
  pulse_options_set_conference_event_presentation_stop_callback (app.pulse, on_presentation_stop, &app);

  // Roster updates for the participants drawer.
  pulse_options_set_conference_event_participant_list_updated_callback (app.pulse, on_participant_list_updated, &app);

  // Conference chat. Direct chat is a negotiated capability, off by default —
  // without this, targeted pulse_send_message calls are refused.
  pulse_options_set_conference_event_message_received_callback (app.pulse, on_message_received, &app);
  pulse_options_set_direct_chat_supported (app.pulse, true);

  // Conference-level state + active layout, for the controls panel.
  pulse_options_set_conference_event_conference_update_callback (app.pulse, on_conference_update, &app);
  pulse_options_set_conference_event_layout_callback (app.pulse, on_layout, &app);

  // Live captions.
  pulse_options_set_conference_event_live_captions_callback (app.pulse, on_live_captions, &app);
  g_cc_font = app.fonts.bodyBold;

  init_video_ctx (app.pulse, app.remote_ctx, PULSE_MEDIA_CONTENT_MAIN);
  init_video_ctx (app.pulse, app.self_ctx, PULSE_MEDIA_CONTENT_SELFVIEW);
  init_video_ctx (app.pulse, app.preso_ctx, PULSE_MEDIA_CONTENT_PRESENTATION);

  if (app.cfg.reg_auto)
    start_register (app);

  // --- Main loop ---------------------------------------------------------
  while (!glfwWindowShouldClose (window)) {
    glfwPollEvents ();

    ImGui_ImplOpenGL3_NewFrame ();
    ImGui_ImplGlfw_NewFrame ();
    ImGui::NewFrame ();

    ImGuiViewport * vp = ImGui::GetMainViewport ();
    ImGui::SetNextWindowPos (vp->Pos);
    ImGui::SetNextWindowSize (vp->Size);
    ImGui::Begin ("##shell", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoScrollWithMouse);

    // Shell background: the gradient-mesh texture, always.
    ImDrawList * dl = ImGui::GetWindowDrawList ();
    dl->AddImage ((ImTextureID) (intptr_t) app.bg_texture, vp->Pos,
                  ImVec2 (vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y));

    int call = app.call_status.load ();
    bool in_call = call == PULSE_CONNECTION_STATUS_CONNECTED || call == PULSE_CONNECTION_STATUS_RECONNECTING;

    // Track call lifecycle for the recents log.
    if (in_call && !app.call_was_connected) {
      app.call_was_connected = true;
      app.call_started = std::chrono::steady_clock::now ();
      set_status (app, "");
    } else if (!in_call && app.call_was_connected && call == PULSE_CONNECTION_STATUS_DISCONNECTED) {
      app.call_was_connected = false;
      // Release the capture devices so the camera light goes off between
      // calls; connect_default_devices() re-attaches them on the next dial
      // (or incoming accept).
      if (app.main_mix_active) {
        pulse_video_mix_disconnect (app.pulse, PULSE_MEDIA_CONTENT_MAIN);
        app.main_mix_active = false;
      }
      release_mix_inputs (app);
      pulse_device_session_disconnect_main_video (app.pulse, PULSE_MEDIA_CONTENT_MAIN, PULSE_MEDIA_INPUT);
      pulse_device_session_disconnect_main_audio (app.pulse);
      app.devices_connected = false;

      stop_share (app); // release the mix + floor if we hung up mid-present
      app.remote_presenting.store (false);
      app.show_share = false;
      app.show_roster = false;
      clear_roster (app);
      app.show_chat = false;
      clear_chat (app);
      app.show_conf_controls = false;
      app.show_keypad = false;
      app.dtmf_sent.clear ();
      app.captions_on = false;
      {
        std::lock_guard<std::mutex> lock (app.caption_mutex);
        app.caption_text.clear ();
      }
      app.available_layouts.clear ();
      {
        std::lock_guard<std::mutex> lock (app.layout_mutex);
        app.current_layout.clear ();
      }
      auto secs =
        std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - app.call_started)
          .count ();
      char dur[16];
      snprintf (dur, sizeof (dur), "%02lld:%02lld", secs / 60, secs % 60);
      record_recent (app, app.call_address, dur);
      app.search[0] = '\0';
      app.last_query = "\x01";
    }

    if (in_call) {
      ui_in_call (app, vp->Size);
      ui_roster_drawer (app, vp->Size);
      ui_chat_drawer (app, vp->Size);
      ui_conference_controls (app);
    } else {
      const float pad = theme::WindowPad;
      const float content_w = vp->Size.x - pad * 2;
      const float footer_h = 34.0f * theme::scale;

      ImGui::SetCursorScreenPos (ImVec2 (vp->Pos.x + pad, vp->Pos.y + 16));
      ui_search_row (app, content_w);

      ImGui::SetCursorScreenPos (ImVec2 (vp->Pos.x + pad, ImGui::GetCursorScreenPos ().y + 14));
      ui_favorites (app, content_w);

      // Recents panel fills the remaining height above the footer.
      ImVec2 cur = ImGui::GetCursorScreenPos ();
      float panels_h = (vp->Pos.y + vp->Size.y) - cur.y - footer_h - 10;
      if (panels_h > 80) {
        ImGui::SetCursorScreenPos (cur);
        ui_recents_panel (app, ImVec2 (content_w, panels_h));
      }

      ImGui::SetCursorScreenPos (ImVec2 (vp->Pos.x + pad, vp->Pos.y + vp->Size.y - footer_h));
      ui_footer (app, content_w, footer_h - 8);

      ui_settings (app);
    }

    update_devices_request (app);
    update_ringing (app);
    ui_incoming_banner (app, vp->Size);
    ui_pin_card (app, vp->Size);
    ui_extension_card (app);
    ui_keypad_window (app);
    ui_sso_card (app);
    ui_devices_window (app);
    ui_share_window (app);

    ImGui::End ();
    ImGui::Render ();

    int dw, dh;
    glfwGetFramebufferSize (window, &dw, &dh);
    glViewport (0, 0, dw, dh);
    ImVec4 shell = theme::Hex (theme::WindowBgStart);
    glClearColor (shell.x, shell.y, shell.z, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData (ImGui::GetDrawData ());
    glfwSwapBuffers (window);
  }

  // --- Shutdown ----------------------------------------------------------
  // If the window was closed mid-call, leave the conference properly —
  // otherwise the server keeps a phantom participant until its timeout.
  // Blocking variant on purpose: the disconnect must complete before
  // pulse_free tears the stack down.
  {
    int call = app.call_status.load ();
    if (call != PULSE_CONNECTION_STATUS_DISCONNECTED) {
      stop_share (app);
      pulse_disconnect (app.pulse, nullptr);
    }
  }
  // Likewise deregister so the registrar drops us immediately instead of
  // waiting for the registration to expire.
  if (app.reg_status.load () == PULSE_CONNECTION_STATUS_CONNECTED)
    pulse_deregister (app.pulse, nullptr);

  pulse_options_set_registration_state_callback (app.pulse, nullptr);
  pulse_options_set_conference_state_callback (app.pulse, nullptr);
  pulse_free (app.pulse);
  save_config (app.cfg);

  ImGui_ImplOpenGL3_Shutdown ();
  ImGui_ImplGlfw_Shutdown ();
  ImGui::DestroyContext ();
  glfwDestroyWindow (window);
  glfwTerminate ();
  return 0;
}
