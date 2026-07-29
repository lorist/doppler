// ----------------------------------------------------------------------------
//  uavwall — bring UAV RTSP downlinks into a Pexip VMR.
//
//  An operator picks from a rail of live RTSP feeds and arranges them on a
//  send canvas; that canvas is pushed into a Pexip conference as this
//  participant's video, so everyone in the VMR sees the composed wall.
//
//      ┌─────────┬──────────────────────────────────┐
//      │ FEED 01 │  [1-up] [2x2] [3x3] [PiP]        │  presets
//      │ FEED 02 │ ┌───────────────┬───────────────┐│
//      │ FEED 03 │ │               │               ││  send canvas
//      │ FEED 04 │ │   drag me     │               ││  (1920x1080, exactly
//      │   ...   │ ├───────────────┼───────────────┤│   what the VMR sees)
//      │         │ │               │               ││
//      └─────────┴─┴───────────────┴───────────────┘┘
//
//  Structure, borrowed from videowall (which proves the pattern):
//    * one Pulse instance per feed — RTSP in, self-view frames pulled back out
//    * one Pulse instance for the conference — dialled over REST, with the
//      composited canvas pushed in on MAIN via a data session
//  and styled with pexclient's Dark Frosted theme.
//
//  Feeds are declared in uavwall-feeds.txt (one "name|rtsp://..." per line) so
//  a demo starts with a single click on "Connect all".
// ----------------------------------------------------------------------------

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <pexpulse/pulse.h>
#include <pexpulse/pulse_data_session.h>
#include <pexpulse/pulse_options.h>
#include <pexpulse/pulse_participant_control.h>
#include <pexpulse/pulse_registrations.h>
#include <pexpulse/pulse_registrations_event.h>
#include <pexpulse/pulse_media_stats.h>
#include <pexpulse/pulse_rtsp_session.h>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <mach/mach.h>
#endif
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <cctype>
#include <cerrno>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <sstream>
#include <string>
#include <vector>

#include "Theme.h"

#ifndef UAVWALL_ASSET_DIR
#define UAVWALL_ASSET_DIR "."
#endif

// Everything an operator might reasonably want to change, with defaults chosen
// so a fresh checkout demonstrates itself against scripts/uav-streams.sh.
// Persisted to uavwall.conf next to the working directory.
struct Config
{
  // Conference
  std::string vmr;                       // name@server
  std::string pin;
  std::string display_name = "UAV Wall";

  // The canvas the VMR receives. 1080p by default so conference endpoints get
  // a standard picture; 720p roughly halves the compositing cost.
  int canvas_w = 1920;
  int canvas_h = 1080;
  int send_fps = 30;

  // Send the canvas as this participant's main video (as if it were our
  // camera), or as content/presentation — the second stream, which most
  // endpoints show alongside the people rather than instead of one.
  bool send_as_content = false;

  // Registration. The wall registers as a device so a conference can dial it,
  // and so the VMR field can search the directory. Password only — see the
  // Planned section of the README for why SSO is out of scope.
  std::string reg_host;      // domain; Pulse resolves _pexapp._tcp SRV
  std::string reg_alias;     // this device's alias on Infinity
  std::string reg_user;
  std::string reg_pass;
  bool reg_auto = false;
  // An unattended wall should answer without anyone present. Off by default so
  // a demo machine does not surprise its operator.
  bool auto_accept = false;

  // Feeds
  bool rtsp_tcp = true;                  // TCP suits most IP cameras
  int rtsp_latency_ms = 200;
  bool autoconnect = false;              // connect every feed at startup

  // Where recordings are written. Relative paths resolve against the working
  // directory, same as uavwall.conf itself.
  std::string record_dir = "recordings";

  // Interface
  float ui_scale = 1.2f;
  bool show_stats = true;

  // Saved layouts, one per slot, each "feed,x,y,w,h;feed,x,y,w,h;…". Kept as
  // text so the config parser stays a flat key=value reader.
  std::string preset_a, preset_b, preset_c;

  // Session-only: silences the feed-loss cue without hiding the visual state.
  // Deliberately not persisted — an operator muting alerts during one demo
  // should not find them still muted at the next.
  bool alerts_muted = false;
};

// ----------------------------------------------------------------------------
//  Model
// ----------------------------------------------------------------------------

struct Recorder
{
  pid_t pid = -1;
  int in_fd = -1; // ffmpeg's stdin: "q" for a feed, raw frames for the canvas
  std::string path;
  double started_at = 0.0;
  bool stopping = false;
  double kill_after = 0.0; // last resort if it will not exit
  uint64_t bytes = 0;
  uint64_t dropped = 0; // canvas only: frames the encoder could not keep up with

  bool active () const { return pid > 0 && !stopping; }
  bool busy () const { return pid > 0; }
};

struct RgbaImage
{
  int w = 0, h = 0;
  std::vector<unsigned char> px;
};

struct Feed
{
  std::string name;
  std::string url;

  Pulse * pulse = nullptr;
  PulseRtspSessionID session = 0;
  bool connected = false;      // RTSP session established
  bool output_open = false;    // data-session output opened (see stop_feed)
  std::string error;

  GLuint texture = 0;
  int tex_w = 0, tex_h = 0;
  RgbaImage frame; // CPU copy for the compositor
  double last_frame_at = 0.0;
  double connected_at = 0.0;   // for the inspector's UPTIME
  Recorder rec;                // per-feed capture, stream-copied

  // Measured locally: Pulse exposes no RTSP-level counters, so decoded-frame
  // rate and payload throughput are what we can honestly report per feed.
  uint64_t frames_total = 0;
  double fps = 0.0;
  double mbps = 0.0;      // decoded RGBA throughput, not wire bitrate
  uint64_t win_frames = 0;
  uint64_t win_bytes = 0;
  double win_started = 0.0;
};

// Where a feed appears on the canvas, in canvas pixels.
struct Tile
{
  int feed = -1;
  float x = 0, y = 0, w = 0, h = 0;
};

struct App
{
  Config cfg;
  std::vector<Feed> feeds;
  std::vector<Tile> tiles;

  // The conference instance. Created once at startup and freed at exit, rather
  // than per call, for two reasons: registration and answering an incoming call
  // both belong to a single instance that must outlive any one conference; and
  // a process that drops to *zero* Pulse instances crashes inside the next
  // pulse_new() (global GStreamer state is torn down with the last instance).
  Pulse * conf = nullptr;
  std::atomic<int> conf_status{PULSE_CONNECTION_STATUS_DISCONNECTED};
  std::mutex status_mutex;
  std::string status;
  bool input_open = false;
  PulseMediaContent input_content = PULSE_MEDIA_CONTENT_MAIN;
  bool floor_taken = false;
  int last_push_w = 0, last_push_h = 0;

  RgbaImage canvas; // composited each frame
  Recorder canvas_rec; // capture of the composed programme
  // A 1920x1080 RGBA frame is 8.3 MB and a pipe buffer is 64 KB, so writing
  // whole frames means blocking — which the UI thread must never do. A writer
  // thread takes one frame at a time and blocks on its behalf; frames offered
  // while it is still busy are dropped whole, never torn.
  // A few frames deep rather than one: the encoder's pace is not perfectly
  // even, and a single slot turned every small stall into a dropped frame —
  // which shortens the recording and plays it back fast.
  std::thread canvas_writer;
  std::mutex canvas_wm;
  std::condition_variable canvas_wcv;
  std::deque<std::vector<unsigned char>> canvas_wq;   // frames awaiting write
  std::vector<std::vector<unsigned char>> canvas_wfree; // recycled buffers
  bool canvas_wquit = false;
  // The recording's own clock. The render loop cannot always sustain send_fps
  // composites a second, and a short file plays back fast — so the cadence is
  // held here and the last frame repeated when we are late. Wall-clock
  // duration then matches what the operator watched.
  double rec_next_frame = 0.0;

  theme::Fonts fonts;
  int drag_tile = -1;      // tile being moved
  int resize_tile = -1;    // tile being resized
  int rail_drag_feed = -1; // feed being dragged out of the rail

  // Double-click full-screen: remember what to go back to.
  std::vector<Tile> saved_tiles;
  int fullscreen_feed = -1;

  // True once a call has been started (dialled out, or later, answered) and
  // until it is torn down. app.conf outlives any single call, so it can no
  // longer serve as the "are we in a call" test.
  bool call_started = false;
  // When the conference actually came up, for the footer's call duration.
  double call_connected_at = 0.0;

  // Registration lives on app.conf, the one instance that outlives any call.
  std::atomic<int> reg_status{PULSE_CONNECTION_STATUS_DISCONNECTED};
  // reg_status only moves when Pulse's state callback fires, which is too late
  // to stop a second Register press from hitting the handle (pexclient's bug).
  std::atomic<bool> reg_in_flight{false};

  // Directory search results for the VMR field. Only populated while
  // registered — pulse_registrations_query_alias fails otherwise.
  struct AliasHit
  {
    std::string alias, description;
    bool is_device = false;
  };
  std::vector<AliasHit> vmr_hits;
  std::string vmr_last_query = "\x01"; // never matches, so the first edit queries
  // Clicking a row deactivates the input field, so the list cannot be gated on
  // the field alone or it vanishes before the click lands. Remember whether the
  // list itself was hovered last frame and keep it up in that case.
  bool vmr_popup_hovered = false;

  // Incoming calls. Pulse's callback blocks one of its worker threads until we
  // answer, so it parks on `pending` and polls `answer` while the UI decides.
  std::atomic<bool> incoming_pending{false};
  std::atomic<int> incoming_answer{-1}; // -1 undecided, 0 decline, 1 accept
  std::mutex incoming_mutex;
  std::string incoming_from, incoming_alias;
  bool ringing = false;
  double ring_next_at = 0.0;
  // The incoming call arrived while we were already in a conference, so the
  // operator is being asked whether to drop it.
  std::atomic<bool> incoming_busy{false};
  // Set by the parked callback, actioned by the UI thread: leave the current
  // conference so the incoming call can be answered. The callback cannot do
  // this itself — it is running on a Pulse worker thread.
  std::atomic<bool> hangup_for_incoming{false};

  // PIN request during a dial-out. Parked the same way as an incoming call.
  std::atomic<bool> pin_pending{false};
  std::atomic<int> pin_answer{-1}; // -1 undecided, 0 cancel, 1 submit
  std::atomic<bool> pin_guest_required{false};
  // The configured PIN is offered once per dial. If Infinity asks again it was
  // wrong, and asking the operator beats resubmitting it forever.
  bool pin_auto_used = false;
  std::atomic<bool> call_failed{false};
  std::mutex pin_mutex;
  std::string pin_value;

  // Benchmark mode (--bench): connect everything, lay it out, force the
  // compositor to run as if sending, sample for N seconds and print a row.
  // Exists so capacity numbers are reproducible without anyone clicking.
  bool bench = false;
  double bench_seconds = 20.0;
  double bench_started = 0.0;
  bool bench_force_composite = false;
  int bench_grid_c = 0, bench_grid_r = 0;
  double bench_cpu_sum = 0.0, bench_cpu_peak = 0.0;
  double bench_comp_sum = 0.0;
  int bench_samples = 0;

  bool show_settings = false;
  int settings_tab = 0; // 0..4, left-nav selection

  // Rail selection, driving the inspector card at the foot of the rail.
  int inspect_feed = -1;

  // Feed-loss alert. alert_feed is the worst current stall, recomputed each
  // frame; dismissal is remembered against it so a *new* stall re-raises the
  // bar but the same one does not.
  int alert_feed = -1;
  bool alert_dismissed = false;
  int alert_dismissed_feed = -1;
  double alert_rang_at = 0.0;

  // Long-press to store a layout preset: which slot, and since when.
  int preset_held = -1;
  double preset_held_since = 0.0;

  // Resource / network readout.
  double proc_cpu_pct = 0.0;   // this process, % of one core
  double proc_rss_mb = 0.0;
  double stats_sampled_at = 0.0;
  double last_cpu_seconds = 0.0;
  double composite_ms = 0.0;   // cost of building the outgoing frame
  // Outbound conference stats, from Pulse.
  bool tx_valid = false;
  uint32_t tx_bitrate = 0;
  float tx_loss_pct = 0.0f;
  float tx_rtt_ms = 0.0f;
  int tx_w = 0, tx_h = 0;
};

static void
set_status (App & app, std::string s)
{
  std::lock_guard<std::mutex> lock (app.status_mutex);
  app.status = std::move (s);
}

static std::string
get_status (App & app)
{
  std::lock_guard<std::mutex> lock (app.status_mutex);
  return app.status;
}

// ----------------------------------------------------------------------------
//  Feed config — "name|rtsp://host/path" per line, # comments allowed
// ----------------------------------------------------------------------------

static const char * kConfigFile = "uavwall.conf";
static const char * kLegacyFeedsFile = "uavwall-feeds.txt";

static std::string
trim (const std::string & v)
{
  std::size_t a = v.find_first_not_of (" \t\r\n");
  if (a == std::string::npos)
    return "";
  std::size_t b = v.find_last_not_of (" \t\r\n");
  return v.substr (a, b - a + 1);
}

// "NAME|rtsp://..." — the URL may contain no '|', so split on the first one.
static void
add_feed_line (App & app, const std::string & value)
{
  std::size_t bar = value.find ('|');
  Feed f;
  if (bar == std::string::npos) {
    f.name = "FEED " + std::to_string (app.feeds.size () + 1);
    f.url = trim (value);
  } else {
    f.name = trim (value.substr (0, bar));
    f.url = trim (value.substr (bar + 1));
  }
  if (!f.url.empty ())
    app.feeds.push_back (std::move (f));
}

static void
load_config (App & app)
{
  std::ifstream ifs (kConfigFile);
  std::string line;
  while (std::getline (ifs, line)) {
    line = trim (line);
    if (line.empty () || line[0] == '#')
      continue;
    std::size_t eq = line.find ('=');
    if (eq == std::string::npos)
      continue;
    std::string k = trim (line.substr (0, eq));
    std::string v = trim (line.substr (eq + 1));

    if (k == "vmr")
      app.cfg.vmr = v;
    else if (k == "pin")
      app.cfg.pin = v;
    else if (k == "display_name")
      app.cfg.display_name = v;
    else if (k == "reg_host")
      app.cfg.reg_host = v;
    else if (k == "reg_alias")
      app.cfg.reg_alias = v;
    else if (k == "reg_user")
      app.cfg.reg_user = v;
    else if (k == "reg_pass")
      app.cfg.reg_pass = v;
    else if (k == "reg_auto")
      app.cfg.reg_auto = (v == "true");
    else if (k == "auto_accept")
      app.cfg.auto_accept = (v == "true");
    else if (k == "canvas") {
      int w = 0, h = 0;
      if (sscanf (v.c_str (), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
        app.cfg.canvas_w = w;
        app.cfg.canvas_h = h;
      }
    } else if (k == "send_fps")
      app.cfg.send_fps = std::max (1, std::min (60, atoi (v.c_str ())));
    // send_as was written by save_config from the start but never read back,
    // so the choice silently reverted to main video on every restart.
    else if (k == "send_as")
      app.cfg.send_as_content = (v == "content");
    else if (k == "record_dir")
      app.cfg.record_dir = v;
    else if (k == "preset_a")
      app.cfg.preset_a = v;
    else if (k == "preset_b")
      app.cfg.preset_b = v;
    else if (k == "preset_c")
      app.cfg.preset_c = v;
    else if (k == "rtsp_transport")
      app.cfg.rtsp_tcp = (v != "udp");
    else if (k == "rtsp_latency_ms")
      app.cfg.rtsp_latency_ms = std::max (0, std::min (5000, atoi (v.c_str ())));
    else if (k == "autoconnect")
      app.cfg.autoconnect = (v == "true");
    else if (k == "ui_scale")
      app.cfg.ui_scale = std::max (0.8f, std::min (2.0f, (float) atof (v.c_str ())));
    else if (k == "show_stats")
      app.cfg.show_stats = (v == "true");
    else if (k == "feed")
      add_feed_line (app, v);
  }

  // Migrate the old feeds-only file, if that is all there is.
  if (app.feeds.empty ()) {
    std::ifstream legacy (kLegacyFeedsFile);
    while (std::getline (legacy, line)) {
      line = trim (line);
      if (!line.empty () && line[0] != '#')
        add_feed_line (app, line);
    }
  }

  // Still nothing: default to what scripts/uav-streams.sh publishes, so a
  // fresh checkout demonstrates itself.
  if (app.feeds.empty ()) {
    // Matches the callsigns scripts/uav-streams.sh burns into its overlays.
    static const char * kCallsigns[] = {"HAWKEYE 21", "KESTREL 33", "NOMAD 14", "OSPREY 12"};
    for (int i = 0; i < 4; i++) {
      Feed f;
      f.name = kCallsigns[i];
      f.url = "rtsp://127.0.0.1:8554/uav" + std::to_string (i + 1);
      app.feeds.push_back (std::move (f));
    }
  }
}

static void
save_config (App & app)
{
  std::ofstream ofs (kConfigFile, std::ios::trunc);
  ofs << "# uavwall configuration. Edit here or via Settings in the app.\n\n";
  ofs << "# Conference to send the composed canvas into.\n";
  ofs << "vmr=" << app.cfg.vmr << "\n";
  ofs << "pin=" << app.cfg.pin << "\n";
  ofs << "display_name=" << app.cfg.display_name << "\n\n";
  ofs << "# Register as a device so a conference can dial the wall, and so the\n";
  ofs << "# VMR field can search the directory. Password is stored in clear.\n";
  ofs << "reg_host=" << app.cfg.reg_host << "\n";
  ofs << "reg_alias=" << app.cfg.reg_alias << "\n";
  ofs << "reg_user=" << app.cfg.reg_user << "\n";
  ofs << "reg_pass=" << app.cfg.reg_pass << "\n";
  ofs << "reg_auto=" << (app.cfg.reg_auto ? "true" : "false") << "\n";
  ofs << "auto_accept=" << (app.cfg.auto_accept ? "true" : "false") << "\n\n";
  ofs << "# What the far end receives. 1280x720 roughly halves compositing cost.\n";
  ofs << "canvas=" << app.cfg.canvas_w << "x" << app.cfg.canvas_h << "\n";
  ofs << "send_fps=" << app.cfg.send_fps << "\n";
  ofs << "# main = this participant's video, content = the presentation stream.\n";
  ofs << "send_as=" << (app.cfg.send_as_content ? "content" : "main") << "\n\n";
  ofs << "# Feed transport. TCP suits most IP cameras; some only offer UDP.\n";
  ofs << "rtsp_transport=" << (app.cfg.rtsp_tcp ? "tcp" : "udp") << "\n";
  ofs << "rtsp_latency_ms=" << app.cfg.rtsp_latency_ms << "\n";
  ofs << "autoconnect=" << (app.cfg.autoconnect ? "true" : "false") << "\n\n";
  ofs << "# Interface. ui_scale applies on next start.\n";
  ofs << "ui_scale=" << app.cfg.ui_scale << "\n";
  ofs << "show_stats=" << (app.cfg.show_stats ? "true" : "false") << "\n\n";
  ofs << "# Where recordings are written, relative to the working directory.\n";
  ofs << "record_dir=" << app.cfg.record_dir << "\n\n";
  ofs << "# Saved layouts, recalled from the A/B/C slots. feed,x,y,w,h per tile.\n";
  ofs << "preset_a=" << app.cfg.preset_a << "\n";
  ofs << "preset_b=" << app.cfg.preset_b << "\n";
  ofs << "preset_c=" << app.cfg.preset_c << "\n\n";
  ofs << "# One per feed: feed=NAME|URL\n";
  for (const Feed & f : app.feeds)
    ofs << "feed=" << f.name << "|" << f.url << "\n";
}

// ----------------------------------------------------------------------------
//  Pulse: one instance per feed
// ----------------------------------------------------------------------------

static PulseDataSessionConfig *
make_rgba_input_config (int w, int h)
{
  PulseDataSessionConfig * cfg = pulse_data_session_config_new (PULSE_DATA_SESSION_VIDEO_FROM_VALUES);
  PulseDimensions dims{(uint32_t) w, (uint32_t) h};
  PulseFramerate fps{30, 1};
  pulse_data_session_config_video_from_values (cfg, PULSE_MEDIA_PIXEL_FORMAT_RGBA, dims, fps);
  return cfg;
}

static PulseDataSessionConfig *
make_video_output_config ()
{
  PulseDataSessionConfig * cfg = pulse_data_session_config_new (PULSE_DATA_SESSION_VIDEO_FROM_CAPS);
  pulse_data_session_config_video_from_caps (cfg, "video/x-raw, format=RGBA");
  return cfg;
}

static void stop_feed (Feed & f);

// start_feed operates on a Feed alone; mirror the two transport settings here
// rather than thread the whole Config through it.
static bool g_cfg_rtsp_tcp = true;
static int g_cfg_rtsp_latency_ms = 200;

// RTSP in, self-view out — the uniform local-source recipe from videowall.
static void
start_feed (Feed & f)
{
  if (f.connected)
    return;
  f.error.clear ();

  f.pulse = pulse_new ();
  if (!f.pulse) {
    f.error = "pulse_new() failed";
    return;
  }
  pulse_options_set_self_view_window_handle (f.pulse, nullptr);
  pulse_options_set_remote_video_window_handle (f.pulse, nullptr);
  pulse_options_set_presentation_video_window_handle (f.pulse, nullptr);
  pulse_options_set_application_user_agent_string (f.pulse, "uavwall/0.1");

  PulseRtspInputConfig cfg{};
  cfg.location = f.url.c_str ();
  cfg.transport = g_cfg_rtsp_tcp ? PULSE_RTSP_TRANSPORT_TCP : PULSE_RTSP_TRANSPORT_UDP;
  cfg.latency_ms = (uint32_t) g_cfg_rtsp_latency_ms;

  PulseRtspSessionID session = 0;
  PulseError err = pulse_rtsp_session_connect_input (f.pulse, &cfg, &session);
  if (err != PULSE_SUCCESS) {
    f.error = std::string ("connect: ") + pulse_strerror (err);
    stop_feed (f);
    return;
  }
  f.session = session;

  err = pulse_rtsp_session_bind_to_content (f.pulse, session, PULSE_MEDIA_CONTENT_MAIN);
  if (err != PULSE_SUCCESS) {
    f.error = std::string ("bind: ") + pulse_strerror (err);
    stop_feed (f);
    return;
  }

  glGenTextures (1, &f.texture);
  glBindTexture (GL_TEXTURE_2D, f.texture);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  PulseDataSessionConfig * dcfg = make_video_output_config ();
  if (pulse_data_session_connect_output (f.pulse, dcfg, PULSE_MEDIA_CONTENT_SELFVIEW) == PULSE_SUCCESS)
    f.output_open = true;
  pulse_data_session_config_free (dcfg);

  f.connected = true;
}

// Safe on a never-started or half-started feed — the output session in
// particular is opened last, so a failed start must not try to close it.
static void
stop_feed (Feed & f)
{
  if (!f.pulse) {
    f.connected = false;
    return;
  }
  if (f.output_open) {
    pulse_data_session_disconnect (f.pulse, PULSE_MEDIA_VIDEO, PULSE_MEDIA_OUTPUT, PULSE_MEDIA_CONTENT_SELFVIEW);
    f.output_open = false;
  }
  if (f.session != 0) {
    pulse_rtsp_session_disconnect_input (f.pulse, f.session);
    f.session = 0;
  }
  pulse_free (f.pulse);
  f.pulse = nullptr;

  if (f.texture) {
    glDeleteTextures (1, &f.texture);
    f.texture = 0;
  }
  f.frame = RgbaImage{};
  f.tex_w = f.tex_h = 0;
  f.connected = false;
}

static void
pump_feed (Feed & f, bool need_cpu_copy)
{
  if (!f.pulse || !f.output_open)
    return;

  PulseDataSessionFrameData * frame = nullptr;
  pulse_data_session_pull_frame_data (f.pulse, PULSE_MEDIA_VIDEO, &frame, PULSE_MEDIA_CONTENT_SELFVIEW, 0);
  if (!frame)
    return;

  int w = 0, h = 0;
  if (pulse_frame_data_get_resolution (frame, &w, &h) && w > 0 && h > 0 && frame->data) {
    // The CPU copy exists only for the compositor, so skip it (a ~3.7 MB
    // memcpy per 720p frame) for feeds that are not on the canvas — the rail
    // thumbnail is drawn from the GL texture.
    if (need_cpu_copy) {
      f.frame.w = w;
      f.frame.h = h;
      f.frame.px.assign (frame->data, frame->data + (size_t) w * h * 4);
    } else if (f.frame.w != 0) {
      f.frame = RgbaImage{};
    }
    // Measured: skipping this upload for non-previewed feeds saved nothing
    // (unified memory makes it near-free), so every connected feed keeps a
    // live thumbnail. Decode and compositing are the real costs.
    if (f.texture) {
      glBindTexture (GL_TEXTURE_2D, f.texture);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame->data);
      f.tex_w = w;
      f.tex_h = h;
    }
    f.last_frame_at = ImGui::GetTime ();
    f.frames_total++;
    f.win_frames++;
    f.win_bytes += (uint64_t) w * h * 4;
  }
  pulse_data_session_frame_data_free (frame);

  // Roll the measurement window once a second.
  double now = ImGui::GetTime ();
  if (f.win_started == 0.0)
    f.win_started = now;
  if (now - f.win_started >= 1.0) {
    double dt = now - f.win_started;
    f.fps = f.win_frames / dt;
    f.mbps = (f.win_bytes * 8.0) / dt / 1e6;
    f.win_frames = 0;
    f.win_bytes = 0;
    f.win_started = now;
  }
}

// ----------------------------------------------------------------------------
//  Compositor — canvas pixels, then pushed to the conference
// ----------------------------------------------------------------------------

// Nearest-neighbour scaled blit. The obvious implementation costs a 64-bit
// division and two bounds checks *per pixel*, which at 1920x1080x30fps is ~62
// million divisions a second and dominated this app's CPU. Clipping once and
// precomputing the horizontal source mapping removes both from the inner loop,
// which becomes a table lookup and a 32-bit store.
static void
blit_scaled (RgbaImage & dst, const RgbaImage & src, int dx, int dy, int dw, int dh)
{
  if (src.w <= 0 || src.h <= 0 || dw <= 0 || dh <= 0)
    return;

  const int x0 = std::max (0, dx), x1 = std::min (dst.w, dx + dw);
  const int y0 = std::max (0, dy), y1 = std::min (dst.h, dy + dh);
  if (x0 >= x1 || y0 >= y1)
    return;

  // One division per output column rather than per pixel. Single-threaded, so
  // a function-local scratch buffer is fine and avoids reallocating per tile.
  static std::vector<int> xmap;
  xmap.resize ((size_t) (x1 - x0));
  for (int x = x0; x < x1; ++x)
    xmap[(size_t) (x - x0)] = std::min ((int) ((int64_t) (x - dx) * src.w / dw), src.w - 1);

  const int n = x1 - x0;
  for (int y = y0; y < y1; ++y) {
    int sy = (int) ((int64_t) (y - dy) * src.h / dh);
    if (sy >= src.h)
      sy = src.h - 1;

    const uint32_t * srow = reinterpret_cast<const uint32_t *> (&src.px[(size_t) sy * src.w * 4]);
    uint32_t * drow = reinterpret_cast<uint32_t *> (&dst.px[((size_t) y * dst.w + x0) * 4]);
    // Little-endian RGBA: alpha is the high byte, forced opaque.
    for (int i = 0; i < n; ++i)
      drow[i] = srow[xmap[(size_t) i]] | 0xFF000000u;
  }
}

static void
composite (App & app)
{
  app.canvas.w = app.cfg.canvas_w;
  app.canvas.h = app.cfg.canvas_h;
  app.canvas.px.assign ((size_t) app.cfg.canvas_w * app.cfg.canvas_h * 4, 0);
  for (size_t i = 0; i < app.canvas.px.size (); i += 4) {
    app.canvas.px[i + 0] = 13;
    app.canvas.px[i + 1] = 18;
    app.canvas.px[i + 2] = 32; // the theme's shell colour
    app.canvas.px[i + 3] = 255;
  }
  for (const Tile & t : app.tiles) {
    if (t.feed < 0 || t.feed >= (int) app.feeds.size ())
      continue;
    Feed & f = app.feeds[t.feed];
    if (f.frame.w <= 0)
      continue;
    blit_scaled (app.canvas, f.frame, (int) t.x, (int) t.y, (int) t.w, (int) t.h);
  }
}

static void
push_canvas (App & app)
{
  if (!app.conf || !app.input_open || app.canvas.w <= 0)
    return;

  PulseDataSessionFrame frame{};
  PulseDataSessionConfig * upd = nullptr;
  if (app.canvas.w != app.last_push_w || app.canvas.h != app.last_push_h) {
    upd = make_rgba_input_config (app.canvas.w, app.canvas.h);
    frame.update_config = upd;
    app.last_push_w = app.canvas.w;
    app.last_push_h = app.canvas.h;
  }
  frame.video.data = app.canvas.px.data ();
  frame.video.data_size = (int) app.canvas.px.size ();
  pulse_data_session_push_frame (app.conf, &frame, app.input_content);
  if (upd)
    pulse_data_session_config_free (upd);
}

// ----------------------------------------------------------------------------
//  Recording
//
//  Pulse has no recording API — pulse_file_session.h is playback only — so each
//  recording is an ffmpeg subprocess. Two shapes:
//
//    * a feed  — ffmpeg pulls the RTSP stream itself and stream-copies it, so
//      nothing is decoded or re-encoded (measured at 0.3% CPU) and the file
//      holds the original picture rather than the downscaled canvas tile.
//    * the canvas — no source stream to copy, so composited RGBA frames are
//      piped to ffmpeg's stdin and hardware-encoded.
//
//  Stopping is the fiddly part, and all of this was measured rather than
//  assumed: SIGINT hangs on an RTSP input, and SIGKILL leaves a file with no
//  moov atom that nothing will play. Writing "q" to stdin exits cleanly, and
//  for the canvas — whose stdin carries frames — closing the pipe does the same
//  job via EOF.
// ----------------------------------------------------------------------------

// Resolved once. Empty means the record controls are unavailable rather than
// silently broken — ffmpeg is a runtime dependency only for this feature.
static std::string g_ffmpeg;

static void
find_ffmpeg ()
{
  static const char * fixed[] = {"/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg"};
  for (const char * p : fixed)
    if (access (p, X_OK) == 0) {
      g_ffmpeg = p;
      return;
    }
  const char * path = getenv ("PATH");
  if (!path)
    return;
  std::string s (path), dir;
  std::istringstream iss (s);
  while (std::getline (iss, dir, ':')) {
    if (dir.empty ())
      continue;
    std::string cand = dir + "/ffmpeg";
    if (access (cand.c_str (), X_OK) == 0) {
      g_ffmpeg = cand;
      return;
    }
  }
}

// "HAWKEYE 21" -> "HAWKEYE-21", so the filename survives a shell and a USB stick.
static std::string
slug (const std::string & in)
{
  std::string out;
  for (char c : in) {
    if (isalnum ((unsigned char) c))
      out += (char) toupper ((unsigned char) c);
    else if (!out.empty () && out.back () != '-')
      out += '-';
  }
  while (!out.empty () && out.back () == '-')
    out.pop_back ();
  return out.empty () ? "FEED" : out;
}

static std::string
utc_stamp ()
{
  std::time_t now = std::time (nullptr);
  std::tm g{};
#if defined(_WIN32)
  gmtime_s (&g, &now);
#else
  gmtime_r (&now, &g);
#endif
  char buf[32];
  snprintf (buf, sizeof (buf), "%04d%02d%02dT%02d%02d%02dZ", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour,
            g.tm_min, g.tm_sec);
  return buf;
}

// mkdir -p, so a configured folder that does not exist yet is not an error the
// operator has to go and fix mid-demo.
static bool
ensure_dir (const std::string & path)
{
  if (path.empty ())
    return false;
  std::string acc;
  size_t i = 0;
  if (path[0] == '/') {
    acc = "/";
    i = 1;
  }
  while (i <= path.size ()) {
    size_t slash = path.find ('/', i);
    if (slash == std::string::npos)
      slash = path.size ();
    acc += path.substr (i, slash - i);
    if (!acc.empty () && mkdir (acc.c_str (), 0755) != 0 && errno != EEXIST)
      return false;
    acc += "/";
    i = slash + 1;
  }
  struct stat st{};
  return stat (path.c_str (), &st) == 0 && S_ISDIR (st.st_mode);
}

// Fork/exec with a pipe on stdin. ffmpeg's own output goes to a log beside the
// recording so a failure can be diagnosed after the fact.
static bool
spawn_recorder (Recorder & r, const std::vector<std::string> & args, bool nonblocking_stdin)
{
  int fds[2];
  if (pipe (fds) != 0)
    return false;

  std::vector<char *> argv;
  argv.reserve (args.size () + 1);
  for (const std::string & a : args)
    argv.push_back (const_cast<char *> (a.c_str ()));
  argv.push_back (nullptr);

  pid_t pid = fork ();
  if (pid < 0) {
    close (fds[0]);
    close (fds[1]);
    return false;
  }
  if (pid == 0) {
    dup2 (fds[0], STDIN_FILENO);
    close (fds[0]);
    close (fds[1]);
    int log = open ((r.path + ".log").c_str (), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log >= 0) {
      dup2 (log, STDOUT_FILENO);
      dup2 (log, STDERR_FILENO);
      close (log);
    }
    execv (argv[0], argv.data ());
    _exit (127);
  }

  close (fds[0]);
  if (nonblocking_stdin) {
    // The UI thread must never block on a stalled encoder; a full pipe drops
    // the frame instead.
    int fl = fcntl (fds[1], F_GETFL, 0);
    fcntl (fds[1], F_SETFL, fl | O_NONBLOCK);
  }
  r.pid = pid;
  r.in_fd = fds[1];
  r.started_at = ImGui::GetTime ();
  r.stopping = false;
  r.bytes = 0;
  return true;
}

static void
stop_recorder (Recorder & r, bool frames_on_stdin)
{
  if (r.pid <= 0 || r.stopping)
    return;
  if (r.in_fd >= 0) {
    // A feed recorder is told to quit; the canvas recorder simply gets EOF,
    // which finalises the file the same way.
    if (!frames_on_stdin) {
      ssize_t n = write (r.in_fd, "q\n", 2);
      (void) n;
    }
    close (r.in_fd);
    r.in_fd = -1;
  }
  r.stopping = true;
  r.kill_after = ImGui::GetTime () + 8.0;
}

// Called every frame. Reaping is deferred rather than waited on, so stopping a
// recording never stalls the UI.
static void
reap_recorder (Recorder & r)
{
  if (r.pid <= 0)
    return;
  int st = 0;
  pid_t got = waitpid (r.pid, &st, WNOHANG);
  if (got == r.pid || got < 0) {
    r.pid = -1;
    r.stopping = false;
    if (r.in_fd >= 0) {
      close (r.in_fd);
      r.in_fd = -1;
    }
    return;
  }
  if (r.stopping && ImGui::GetTime () > r.kill_after) {
    // It will not go quietly; the file is likely unplayable, but a wedged
    // child is worse.
    kill (r.pid, SIGKILL);
    r.kill_after = ImGui::GetTime () + 5.0;
  }
}


// Record one feed by stream-copying it: ffmpeg opens the RTSP URL itself, so
// this is independent of whether the feed is connected in the wall, and the
// file holds the source picture rather than the canvas tile.
static bool
start_feed_recording (App & app, Feed & f)
{
  if (g_ffmpeg.empty () || f.rec.busy ())
    return false;
  if (!ensure_dir (app.cfg.record_dir)) {
    set_status (app, "Cannot create " + app.cfg.record_dir);
    return false;
  }
  f.rec.path = app.cfg.record_dir + "/" + slug (f.name) + "_" + utc_stamp () + ".mp4";

  std::vector<std::string> args = {g_ffmpeg,
                                   "-hide_banner",
                                   "-loglevel",
                                   "error",
                                   "-rtsp_transport",
                                   app.cfg.rtsp_tcp ? "tcp" : "udp",
                                   "-i",
                                   f.url,
                                   "-c",
                                   "copy",
                                   "-movflags",
                                   "+faststart",
                                   "-y",
                                   f.rec.path};
  if (!spawn_recorder (f.rec, args, false)) {
    set_status (app, "Could not start recorder for " + f.name);
    return false;
  }
  set_status (app, "Recording " + f.name + " → " + f.rec.path);
  return true;
}

// Record the composed canvas. There is no source stream to copy, so frames go
// down a pipe and are encoded — hardware-encoded on macOS, which keeps this
// far cheaper than the compositing that produced them.
static bool
start_canvas_recording (App & app)
{
  if (g_ffmpeg.empty () || app.canvas_rec.busy ())
    return false;
  if (!ensure_dir (app.cfg.record_dir)) {
    set_status (app, "Cannot create " + app.cfg.record_dir);
    return false;
  }
  app.canvas_rec.path = app.cfg.record_dir + "/CANVAS_" + utc_stamp () + ".mp4";

  char size[32], rate[16];
  snprintf (size, sizeof (size), "%dx%d", app.cfg.canvas_w, app.cfg.canvas_h);
  snprintf (rate, sizeof (rate), "%d", std::max (1, app.cfg.send_fps));

  std::vector<std::string> args = {g_ffmpeg,
                                   "-hide_banner",
                                   "-loglevel",
                                   "error",
                                   "-f",
                                   "rawvideo",
                                   "-pixel_format",
                                   "rgba",
                                   "-video_size",
                                   size,
                                   "-framerate",
                                   rate,
                                   "-i",
                                   "-",
#if defined(__APPLE__)
                                   "-c:v",
                                   "h264_videotoolbox",
                                   "-b:v",
                                   "6M",
#else
                                   "-c:v",
                                   "libx264",
                                   "-preset",
                                   "veryfast",
                                   "-crf",
                                   "23",
#endif
                                   "-pix_fmt",
                                   "yuv420p",
                                   "-movflags",
                                   "+faststart",
                                   "-y",
                                   app.canvas_rec.path};
  if (!spawn_recorder (app.canvas_rec, args, false)) {
    set_status (app, "Could not start the canvas recorder");
    return false;
  }

  // A previous writer may still be joinable — the encoder can exit on its own
  // (EPIPE, a bad path), and assigning over a joinable std::thread terminates.
  if (app.canvas_writer.joinable ()) {
    {
      std::lock_guard<std::mutex> lock (app.canvas_wm);
      app.canvas_wquit = true;
      app.canvas_wq.clear ();
    }
    app.canvas_wcv.notify_all ();
    app.canvas_writer.join ();
  }

  app.canvas_wquit = false;
  app.canvas_rec.dropped = 0;
  app.canvas_wq.clear ();
  app.canvas_wfree.clear ();
  app.rec_next_frame = 0.0;
  app.canvas_writer = std::thread ([&app] () {
    for (;;) {
      std::vector<unsigned char> frame;
      {
        std::unique_lock<std::mutex> lock (app.canvas_wm);
        app.canvas_wcv.wait (lock, [&app] { return !app.canvas_wq.empty () || app.canvas_wquit; });
        // Drain what is queued before quitting, so a stop does not truncate
        // frames the operator has already seen composed.
        if (app.canvas_wq.empty ())
          return;
        frame.swap (app.canvas_wq.front ());
        app.canvas_wq.pop_front ();
      }
      const unsigned char * pp = frame.data ();
      size_t left = frame.size ();
      while (left > 0) {
        ssize_t n = write (app.canvas_rec.in_fd, pp, left);
        if (n > 0) {
          pp += n;
          left -= (size_t) n;
          continue;
        }
        if (n < 0 && errno == EINTR)
          continue;
        return; // EPIPE: the encoder is gone
      }
      app.canvas_rec.bytes += frame.size ();
      {
        std::lock_guard<std::mutex> lock (app.canvas_wm);
        if (app.canvas_wfree.size () < 4)
          app.canvas_wfree.push_back (std::move (frame)); // keep the allocation
      }
    }
  });

  set_status (app, "Recording the canvas → " + app.canvas_rec.path);
  return true;
}

// Join the writer before closing the pipe, so the last frame is whole and the
// EOF that finalises the file arrives after it.
static void
stop_canvas_recording (App & app)
{
  if (!app.canvas_rec.busy ())
    return;
  {
    std::lock_guard<std::mutex> lock (app.canvas_wm);
    app.canvas_wquit = true;
  }
  app.canvas_wcv.notify_all ();
  if (app.canvas_writer.joinable ())
    app.canvas_writer.join ();
  stop_recorder (app.canvas_rec, true);
}

// One composited frame to the encoder. Partial writes are normal on a pipe, so
// this loops, but never blocks: if the encoder is behind, the frame is dropped
// rather than stalling the UI thread.
static void
write_canvas_frame (App & app)
{
  Recorder & r = app.canvas_rec;
  if (!r.active () || r.in_fd < 0 || app.canvas.px.empty ())
    return;
  // A resolution change mid-recording would corrupt the stream: ffmpeg was told
  // the frame size up front. Stop instead of writing mismatched frames.
  if (app.canvas.w != app.cfg.canvas_w || app.canvas.h != app.cfg.canvas_h)
    return;

  std::lock_guard<std::mutex> lock (app.canvas_wm);
  if (app.canvas_wq.size () >= 3) {
    // Genuinely behind. Drop this frame whole — a partial write would
    // desynchronise the raw stream and corrupt everything after it.
    r.dropped++;
    return;
  }
  std::vector<unsigned char> buf;
  if (!app.canvas_wfree.empty ()) {
    buf = std::move (app.canvas_wfree.back ());
    app.canvas_wfree.pop_back ();
  }
  buf.assign (app.canvas.px.begin (), app.canvas.px.end ());
  app.canvas_wq.push_back (std::move (buf));
  app.canvas_wcv.notify_one ();
}

// Any recording at all — drives the footer cell and the composite gate.
static int
recording_count (const App & app)
{
  int n = app.canvas_rec.active () ? 1 : 0;
  for (const Feed & f : app.feeds)
    if (f.rec.active ())
      n++;
  return n;
}

// ----------------------------------------------------------------------------
//  Layout presets
// ----------------------------------------------------------------------------

// Feeds that are currently placed, in tile order — presets rearrange these.
static std::vector<int>
placed_feeds (App & app)
{
  std::vector<int> out;
  for (const Tile & t : app.tiles)
    if (t.feed >= 0)
      out.push_back (t.feed);
  return out;
}

// Geometry only, so the control deck can compute what a preset *would* produce
// and light the matching segment without mutating anything.
static std::vector<Tile>
grid_tiles (const Config & cfg, const std::vector<int> & feeds, int cols, int rows)
{
  std::vector<Tile> out;
  const float cw = (float) cfg.canvas_w / cols;
  const float ch = (float) cfg.canvas_h / rows;
  for (int i = 0; i < (int) feeds.size () && i < cols * rows; i++) {
    Tile t;
    t.feed = feeds[i];
    t.x = (i % cols) * cw;
    t.y = (i / cols) * ch;
    t.w = cw;
    t.h = ch;
    out.push_back (t);
  }
  return out;
}

static void
apply_grid (App & app, int cols, int rows)
{
  std::vector<int> feeds = placed_feeds (app);
  if (feeds.empty ())
    for (int i = 0; i < (int) app.feeds.size () && i < cols * rows; i++)
      feeds.push_back (i); // nothing placed yet: fill from the rail

  app.tiles = grid_tiles (app.cfg, feeds, cols, rows);
  app.fullscreen_feed = -1;
}

static std::vector<Tile>
pip_tiles (const Config & cfg, const std::vector<int> & feeds)
{
  std::vector<Tile> out;
  if (feeds.empty ())
    return out;
  Tile big;
  big.feed = feeds[0];
  big.x = big.y = 0;
  big.w = (float) cfg.canvas_w;
  big.h = (float) cfg.canvas_h;
  out.push_back (big);

  const float pw = cfg.canvas_w * 0.22f, ph = pw * 9.0f / 16.0f;
  const float margin = 24.0f;
  for (int i = 1; i < (int) feeds.size () && i <= 4; i++) {
    Tile t;
    t.feed = feeds[i];
    t.w = pw;
    t.h = ph;
    t.x = cfg.canvas_w - (pw + margin) * i;
    t.y = cfg.canvas_h - ph - margin;
    out.push_back (t);
  }
  return out;
}

// One feed full frame, the rest as a strip of PiPs along the bottom.
static void
apply_pip (App & app)
{
  std::vector<int> feeds = placed_feeds (app);
  if (feeds.empty ())
    for (int i = 0; i < (int) app.feeds.size (); i++)
      feeds.push_back (i);
  if (feeds.empty ())
    return;

  app.tiles = pip_tiles (app.cfg, feeds);
  app.fullscreen_feed = -1;
}

// Double-click gesture: punch a feed full frame, and punch back.
static void
toggle_fullscreen (App & app, int feed)
{
  if (app.fullscreen_feed == feed) {
    app.tiles = app.saved_tiles; // restore what was there before
    app.fullscreen_feed = -1;
    return;
  }
  if (app.fullscreen_feed == -1)
    app.saved_tiles = app.tiles; // first punch: remember the layout

  app.tiles.clear ();
  Tile t;
  t.feed = feed;
  t.x = t.y = 0;
  t.w = app.cfg.canvas_w;
  t.h = app.cfg.canvas_h;
  app.tiles.push_back (t);
  app.fullscreen_feed = feed;
}

static bool
feed_is_placed (App & app, int feed)
{
  for (const Tile & t : app.tiles)
    if (t.feed == feed)
      return true;
  return false;
}

static void
remove_feed_tiles (App & app, int feed)
{
  app.tiles.erase (std::remove_if (app.tiles.begin (), app.tiles.end (),
                                   [&] (const Tile & t) { return t.feed == feed; }),
                   app.tiles.end ());
}

// ----------------------------------------------------------------------------
//  Conference
// ----------------------------------------------------------------------------

static void
on_conf_status (const PulseConferenceStatusInfo * info, void * ctx)
{
  static_cast<App *> (ctx)->conf_status.store ((int) info->status);
}

static void
on_conf_result (const PulseError err, void * ctx)
{
  auto * app = static_cast<App *> (ctx);
  if (err != PULSE_SUCCESS) {
    set_status (*app, std::string ("conference: ") + pulse_strerror (err));
    // A refused or cancelled join leaves the UI showing "connecting…" forever
    // otherwise. The UI thread owns call_started, so flag it and let it clear.
    app->call_failed.store (true);
  }
}

static void
on_conf_progress (const PulseOperationProgressInfo * info, void * ctx)
{
  set_status (*static_cast<App *> (ctx), info->desc ? info->desc : "");
}

// ----------------------------------------------------------------------------
//  Incoming calls
// ----------------------------------------------------------------------------

#if defined(__APPLE__)
static void
play_ring ()
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
  if (sound)
    AudioServicesPlaySystemSound (sound);
  else
    AudioServicesPlayAlertSound (kSystemSoundID_UserPreferredAlert);
}
#else
static void
play_ring ()
{
}
#endif

// A conference is dialling the wall. This runs on a Pulse worker thread and
// blocks it until we return, so the decision is parked here while the UI (or
// the auto-accept setting) makes it. Returning true accepts; Pulse then runs
// the usual connect flow through the callbacks we fill in.
static bool
on_incoming (const PulseRegistrationsEventIncoming * event, void * ctx,
             PulseAsyncOperationResultCallbackConfig * result_cb, PulseOperationProgressCallbackConfig * progress_cb)
{
  auto * app = static_cast<App *> (ctx);

  {
    std::lock_guard<std::mutex> lock (app->incoming_mutex);
    app->incoming_from = (event->remote_display_name && event->remote_display_name[0])
                           ? event->remote_display_name
                           : (event->remote_alias ? event->remote_alias : "unknown");
    app->incoming_alias = event->conference_alias ? event->conference_alias : "";
  }
  std::fprintf (stderr, "[uavwall] incoming call from %s\n", app->incoming_from.c_str ());

  // One Pulse instance can only be in one conference, so answering while
  // already in a call means leaving that one first. Auto-accept deliberately
  // does NOT apply here: dropping a call in progress is the operator's
  // decision, never the software's.
  const bool busy = app->call_started;
  app->incoming_busy.store (busy);

  if (!busy && app->cfg.auto_accept) {
    app->incoming_answer.store (1);
  } else {
    app->incoming_answer.store (-1);
    app->incoming_pending.store (true);
    while (app->incoming_answer.load () == -1)
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
    app->incoming_pending.store (false);
  }

  bool accept = app->incoming_answer.load () == 1;

  if (accept && busy) {
    // Ask the UI thread to tear the current conference down, then wait for it
    // to actually reach DISCONNECTED before accepting — Pulse will not put us
    // into a second conference while the first is still up. Bounded, so a
    // disconnect that never completes cannot strand this worker thread.
    app->hangup_for_incoming.store (true);
    const int kTimeoutMs = 10000;
    int waited = 0;
    while (app->conf_status.load () != PULSE_CONNECTION_STATUS_DISCONNECTED && waited < kTimeoutMs) {
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
      waited += 100;
    }
    if (app->conf_status.load () != PULSE_CONNECTION_STATUS_DISCONNECTED) {
      std::fprintf (stderr, "[uavwall] gave up waiting for the current call to end\n");
      set_status (*app, "Could not leave the current conference — incoming call rejected");
      app->hangup_for_incoming.store (false);
      accept = false;
    }
  }
  app->incoming_busy.store (false);
  if (accept) {
    result_cb->func = on_conf_result;
    result_cb->user_context = app;
    progress_cb->func = on_conf_progress;
    progress_cb->user_context = app;
    // The canvas input session is opened when the conference reports CONNECTED
    // (see the frame loop), which serves dial-in and dial-out alike.
    app->call_started = true;
    app->floor_taken = false;
    app->input_content = app->cfg.send_as_content ? PULSE_MEDIA_CONTENT_PRESENTATION : PULSE_MEDIA_CONTENT_MAIN;
  }
  return accept;
}

static void
on_incoming_cancelled (const PulseRegistrationsEventIncomingCancelled *, void * ctx)
{
  auto * app = static_cast<App *> (ctx);
  if (app->incoming_pending.load ())
    app->incoming_answer.store (0); // caller gave up; release the parked worker
  set_status (*app, "Incoming call cancelled");
}

// The VMR wants a PIN. Like the incoming-call callback this blocks a Pulse
// worker until answered, and the supplied setter must be invoked before
// returning — it is invalid in any other context.
static bool
on_pin_request (bool guest_pin_required, const PulseSetPinCode * set_pin, void * ctx)
{
  auto * app = static_cast<App *> (ctx);
  {
    std::lock_guard<std::mutex> lock (app->pin_mutex);
    app->pin_value.clear ();
  }
  app->pin_guest_required.store (guest_pin_required);

  // A PIN configured up front is used without troubling the operator — an
  // unattended wall should be able to dial a known VMR unaided.
  if (!app->cfg.pin.empty () && !app->pin_auto_used) {
    app->pin_auto_used = true;
    set_pin->func (set_pin->context, app->cfg.pin.c_str ());
    return true;
  }

  app->pin_answer.store (-1);
  app->pin_pending.store (true);
  while (app->pin_answer.load () == -1)
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
  app->pin_pending.store (false);

  bool submit = app->pin_answer.load () == 1;
  if (submit) {
    std::lock_guard<std::mutex> lock (app->pin_mutex);
    // NULL means "no PIN", which is valid where a guest PIN is not required.
    set_pin->func (set_pin->context, app->pin_value.empty () ? nullptr : app->pin_value.c_str ());
  }
  return submit;
}

// ----------------------------------------------------------------------------
//  Registration
// ----------------------------------------------------------------------------

static void
on_reg_status (const PulseRegistrationStatusInfo * info, void * ctx)
{
  static_cast<App *> (ctx)->reg_status.store ((int) info->status);
}

static void
on_reg_result (const PulseError err, void * ctx)
{
  auto * app = static_cast<App *> (ctx);
  app->reg_in_flight.store (false);
  if (err == PULSE_SUCCESS) {
    set_status (*app, "");
    return;
  }
  std::fprintf (stderr, "[uavwall] registration failed: %s\n", pulse_strerror (err));
  set_status (*app, err == PULSE_ERROR_HANDLE_IN_USE
                      ? "Registration busy — is another uavwall running?"
                      : std::string ("registration: ") + pulse_strerror (err));
}

static void
on_reg_progress (const PulseOperationProgressInfo * info, void * ctx)
{
  set_status (*static_cast<App *> (ctx), info->desc ? info->desc : "");
}

static void
start_register (App & app)
{
  if (!app.conf)
    return;
  if (app.cfg.reg_host.empty () || app.cfg.reg_alias.empty ()) {
    set_status (app, "Set a registration host and alias in Settings.");
    return;
  }
  if (app.reg_in_flight.exchange (true)) {
    set_status (app, "Registration already in progress…");
    return;
  }

  PulseRegistrationRequest req{};
  req.host = app.cfg.reg_host.c_str ();
  req.alias = app.cfg.reg_alias.c_str ();
  req.username = app.cfg.reg_user.empty () ? nullptr : app.cfg.reg_user.c_str ();
  req.password = app.cfg.reg_pass.empty () ? nullptr : app.cfg.reg_pass.c_str ();
  req.use_sso = false;

  // Must be installed before registering: this is how Pulse delivers, and
  // lets us answer, calls made to our alias.
  PulseRegistrationsEventCallbackConfig ev{};
  ev.registrations_event_incoming_callback = on_incoming;
  ev.registrations_event_incoming_callback_user_context = &app;
  ev.registrations_event_incoming_cancelled_callback = on_incoming_cancelled;
  ev.registrations_event_incoming_cancelled_callback_user_context = &app;
  pulse_options_set_registrations_events_callbacks (app.conf, &ev);

  PulseAsyncOperationResultCallbackConfig rcb{on_reg_result, &app};
  PulseOperationProgressCallbackConfig pcb{on_reg_progress, &app};
  PulseError err = pulse_register_async (app.conf, &req, &rcb, &pcb);
  if (err != PULSE_SUCCESS) {
    app.reg_in_flight.store (false);
    on_reg_result (err, &app);
  }
}

static void
start_deregister (App & app)
{
  if (!app.conf)
    return;
  PulseAsyncOperationResultCallbackConfig rcb{on_reg_result, &app};
  pulse_deregister_async (app.conf, &rcb, nullptr);
}

// Ask the registrar for aliases matching what has been typed. Blocking, but
// it is a small query against the node we are registered to, and this is the
// same per-keystroke pattern pexclient uses.
static void
refresh_vmr_hits (App & app, const std::string & query)
{
  if (query == app.vmr_last_query)
    return;
  app.vmr_last_query = query;
  app.vmr_hits.clear ();

  if (!app.conf || app.reg_status.load () != PULSE_CONNECTION_STATUS_CONNECTED || query.empty ())
    return;

  PulseRegistrationAliasList * result = nullptr;
  // Conferences first — a wall usually dials a VMR — but devices are useful
  // too, for calling an endpoint directly.
  if (pulse_registrations_query_alias (app.conf, query.c_str (), 4, 8, &result) == PULSE_SUCCESS && result) {
    for (size_t i = 0; i < result->size; i++) {
      App::AliasHit h;
      h.alias = result->list[i]->alias ? result->list[i]->alias : "";
      h.description = result->list[i]->description ? result->list[i]->description : "";
      h.is_device = result->list[i]->type == PULSE_REGISTRATION_ALIAS_DEVICE;
      if (!h.alias.empty ())
        app.vmr_hits.push_back (std::move (h));
    }
  }
  pulse_registration_alias_list_free (result);
}

static bool
split_vmr (const std::string & id, std::string & conf, std::string & server)
{
  std::size_t at = id.find ('@');
  if (at == std::string::npos || at == 0 || at + 1 >= id.size ())
    return false;
  conf = id.substr (0, at);
  server = id.substr (at + 1);
  return true;
}

static void
conf_connect (App & app)
{
  std::string conference, server;
  if (!split_vmr (app.cfg.vmr, conference, server)) {
    set_status (app, "VMR must look like name@server");
    return;
  }

  if (!app.conf) {
    set_status (app, "no Pulse instance");
    return;
  }
  if (app.call_started) {
    set_status (app, "already in a call");
    return;
  }

  PulseRestConnectionConfig cfg{};
  cfg.server_address = server.c_str ();
  cfg.conference_name = conference.c_str ();
  cfg.display_name = app.cfg.display_name.c_str ();
  cfg.pin_code = app.cfg.pin.empty () ? nullptr : app.cfg.pin.c_str ();

  PulseAsyncOperationResultCallbackConfig rcb{on_conf_result, &app};
  PulseOperationProgressCallbackConfig pcb{on_conf_progress, &app};
  PulseError err = pulse_connect_with_rest_async (app.conf, &cfg, &rcb, &pcb);
  if (err != PULSE_SUCCESS) {
    set_status (app, std::string ("connect: ") + pulse_strerror (err));
    return; // the instance stays; only this call attempt failed
  }
  app.call_started = true;
  app.pin_auto_used = false;

  // The push side is opened when the conference reports CONNECTED, in the
  // frame loop — the same path an answered incoming call takes.
  app.input_content = app.cfg.send_as_content ? PULSE_MEDIA_CONTENT_PRESENTATION : PULSE_MEDIA_CONTENT_MAIN;
  app.floor_taken = false;
}

// Attach the canvas to whichever conference we are now in, however we got
// there (dialled out, or answered). Idempotent.
static void
ensure_canvas_input (App & app)
{
  if (!app.conf || app.input_open)
    return;
  PulseDataSessionConfig * icfg = make_rgba_input_config (app.cfg.canvas_w, app.cfg.canvas_h);
  if (pulse_data_session_connect_input (app.conf, icfg, app.input_content) == PULSE_SUCCESS) {
    app.input_open = true;
    app.last_push_w = app.cfg.canvas_w;
    app.last_push_h = app.cfg.canvas_h;
  }
  pulse_data_session_config_free (icfg);
}

static void
conf_disconnect (App & app)
{
  if (!app.conf || !app.call_started)
    return;
  if (app.floor_taken) {
    pulse_participant_control_release_floor (app.conf, nullptr);
    app.floor_taken = false;
  }
  if (app.input_open) {
    pulse_data_session_disconnect (app.conf, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT, app.input_content);
    app.input_open = false;
  }
  pulse_disconnect (app.conf, nullptr);
  app.call_started = false;
  app.conf_status.store (PULSE_CONNECTION_STATUS_DISCONNECTED);
  set_status (app, "");
}

// ----------------------------------------------------------------------------
//  Resource sampling
// ----------------------------------------------------------------------------

// This process's CPU (as a percentage of one core) and resident memory. CPU is
// a delta of consumed CPU time over wall time, so it needs two samples.
static void
sample_resources (App & app)
{
  double now = ImGui::GetTime ();
  if (now - app.stats_sampled_at < 1.0)
    return;

  struct rusage ru;
  if (getrusage (RUSAGE_SELF, &ru) == 0) {
    double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
    if (app.stats_sampled_at > 0.0)
      app.proc_cpu_pct = 100.0 * (cpu - app.last_cpu_seconds) / (now - app.stats_sampled_at);
    app.last_cpu_seconds = cpu;
  }

#if defined(__APPLE__)
  mach_task_basic_info info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info (mach_task_self (), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count) == KERN_SUCCESS)
    app.proc_rss_mb = info.resident_size / (1024.0 * 1024.0);
#else
  if (FILE * f = fopen ("/proc/self/statm", "r")) {
    long pages_total = 0, pages_res = 0;
    if (fscanf (f, "%ld %ld", &pages_total, &pages_res) == 2)
      app.proc_rss_mb = pages_res * (double) sysconf (_SC_PAGESIZE) / (1024.0 * 1024.0);
    fclose (f);
  }
#endif

  // Outbound conference stats, straight from Pulse.
  app.tx_valid = false;
  if (app.conf && app.conf_status.load () == PULSE_CONNECTION_STATUS_CONNECTED) {
    if (PulseMediaStats * st = pulse_media_stats_get (app.conf, 5, true)) {
      app.tx_bitrate = st->video_tx.total_bitrate;
      app.tx_loss_pct = st->video_tx.total_packets_lost_pct;
      app.tx_rtt_ms = st->video_tx.rtt_ms;
      app.tx_valid = true;
      pulse_media_stats_free (st);
    }
  }

  app.stats_sampled_at = now;
}

// ----------------------------------------------------------------------------
//  UI — Instrument primitives
//
//  Almost nothing in this design is a stock ImGui widget: the chrome is drawn
//  with the draw list over InvisibleButtons, which is what lets a control be
//  25 high with a 7 radius and mixed type inside it. The rules the other demos
//  learned the hard way still apply — anything interactive is a real item in a
//  real window, and no widget's submission is ever gated on another widget's
//  hovered/active state.
// ----------------------------------------------------------------------------

// A feed is stalled when it is connected, has delivered at least one frame,
// and has delivered nothing for two seconds. Derived every frame rather than
// stored, so recovery needs no bookkeeping.
static bool
feed_stalled (const Feed & f)
{
  return f.connected && f.last_frame_at > 0 && ImGui::GetTime () - f.last_frame_at > 2.0;
}

static bool
feed_live (const Feed & f)
{
  return f.connected && f.last_frame_at > 0 && !feed_stalled (f);
}

// Design units -> screen pixels. Every new metric goes through this so the
// layout holds at a 1440x900 laptop and at a control-room display alike.
static inline float
du (float design)
{
  return design * theme::scale;
}

enum class Btn
{
  Fill,    // solid colour, white text — the primary action of its group
  Tinted,  // 12% fill + 35% border — constructive but secondary
  Outline, // border only — destructive or quiet
  Ghost    // no chrome at all — text buttons in the alert bar
};

// The Instrument button: squared, control-label type, optional leading glyph
// drawn as a plain triangle or square (the design has no icon set at all).
static bool
deck_button (App & app, const char * id, const char * label, ImVec2 size, unsigned int color, Btn style,
             bool enabled = true, int glyph = 0) // glyph: 0 none, 1 play, 2 stop
{
  ImGui::PushID (id);
  ImVec2 p0 = ImGui::GetCursorScreenPos ();
  ImGui::InvisibleButton ("##b", size);
  const bool hovered = enabled && ImGui::IsItemHovered ();
  const bool held = hovered && ImGui::IsMouseDown (ImGuiMouseButton_Left);
  const bool clicked = enabled && ImGui::IsItemHovered () && ImGui::IsMouseReleased (ImGuiMouseButton_Left);
  ImVec2 p1 (p0.x + size.x, p0.y + size.y);
  ImDrawList * dl = ImGui::GetWindowDrawList ();

  const float dim = enabled ? 1.0f : 0.45f;
  ImU32 text_col;
  switch (style) {
  case Btn::Fill:
    dl->AddRectFilled (p0, p1, theme::HexU32 (color, (held ? 0.80f : hovered ? 1.0f : 0.92f) * dim),
                       du (theme::RadiusControl2));
    text_col = theme::WhiteU32 (dim);
    break;
  case Btn::Tinted:
    dl->AddRectFilled (p0, p1, theme::HexU32 (color, (hovered ? 0.22f : 0.12f) * dim), du (theme::RadiusControl2));
    dl->AddRect (p0, p1, theme::HexU32 (color, 0.35f * dim), du (theme::RadiusControl2), 0, 1.0f);
    text_col = theme::HexU32 (color, 0.95f * dim);
    break;
  case Btn::Outline:
    if (hovered)
      dl->AddRectFilled (p0, p1, theme::HexU32 (color, 0.10f), du (theme::RadiusControl2));
    dl->AddRect (p0, p1, theme::HexU32 (color, 0.35f * dim), du (theme::RadiusControl2), 0, 1.0f);
    text_col = theme::HexU32 (color, 0.90f * dim);
    break;
  default:
    text_col = theme::WhiteU32 ((hovered ? 0.70f : 0.45f) * dim);
    break;
  }

  const float fsz = theme::fs (9.5f);
  ImVec2 ts = app.fonts.label->CalcTextSizeA (fsz, FLT_MAX, 0, label);
  const float gw = glyph ? du (glyph == 1 ? 6.0f : 7.0f) + du (6.0f) : 0.0f;
  float tx = p0.x + (size.x - ts.x - gw) / 2 + gw;
  float ty = p0.y + (size.y - ts.y) / 2;

  if (glyph == 1) { // play: a filled triangle, no icon font
    float g = du (6.0f), gh = du (10.0f);
    float gx = tx - gw, gy = p0.y + (size.y - gh) / 2;
    dl->AddTriangleFilled (ImVec2 (gx, gy), ImVec2 (gx, gy + gh), ImVec2 (gx + g, gy + gh / 2), text_col);
  } else if (glyph == 2) { // stop: a filled square
    float g = du (7.0f);
    float gx = tx - gw, gy = p0.y + (size.y - g) / 2;
    dl->AddRectFilled (ImVec2 (gx, gy), ImVec2 (gx + g, gy + g), text_col, 1.0f);
  }
  dl->AddText (app.fonts.label, fsz, ImVec2 (tx, ty), text_col, label);

  if (hovered)
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
  ImGui::PopID ();
  return clicked;
}

// A segmented control: one rounded track, hairline dividers, no per-cell
// rounding. Returns the index pressed, or -1. `active` may be -1 for "none",
// which is what a hand-dragged layout leaves behind.
static int
segmented (App & app, const char * id, const char * const * labels, int count, int active, float cell_w, float h)
{
  ImGui::PushID (id);
  ImVec2 p0 = ImGui::GetCursorScreenPos ();
  ImVec2 size (cell_w * count, h);
  ImGui::InvisibleButton ("##seg", size);
  ImVec2 p1 (p0.x + size.x, p0.y + size.y);
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  const float r = du (theme::RadiusControl2);

  dl->AddRectFilled (p0, p1, theme::WhiteU32 (0.06f), r);

  int pressed = -1;
  const bool hovered = ImGui::IsItemHovered ();
  const int hot = hovered ? std::min (count - 1, std::max (0, (int) ((ImGui::GetIO ().MousePos.x - p0.x) / cell_w)))
                          : -1;
  if (hovered && ImGui::IsMouseReleased (ImGuiMouseButton_Left))
    pressed = hot;

  // The active cell is filled inside the clipped track so its corners follow
  // the group's rounding rather than showing square edges at the ends.
  dl->PushClipRect (p0, p1, true);
  for (int i = 0; i < count; i++) {
    ImVec2 c0 (p0.x + cell_w * i, p0.y), c1 (c0.x + cell_w, p1.y);
    if (i == active)
      dl->AddRectFilled (c0, c1, theme::HexU32 (theme::AccentPrimary, 0.92f));
    else if (i == hot)
      dl->AddRectFilled (c0, c1, theme::WhiteU32 (0.06f));
    if (i > 0)
      dl->AddLine (ImVec2 (c0.x, p0.y + du (1.0f)), ImVec2 (c0.x, p1.y - du (1.0f)), theme::WhiteU32 (0.10f), 1.0f);

    const float fsz = theme::fs (9.5f);
    ImVec2 ts = app.fonts.label->CalcTextSizeA (fsz, FLT_MAX, 0, labels[i]);
    dl->AddText (app.fonts.label, fsz, ImVec2 (c0.x + (cell_w - ts.x) / 2, c0.y + (h - ts.y) / 2),
                 i == active ? theme::WhiteU32 (1.0f) : theme::WhiteU32 (0.55f), labels[i]);
  }
  dl->PopClipRect ();
  dl->AddRect (p0, p1, theme::WhiteU32 (theme::ControlStroke), r, 0, 1.0f);

  if (hovered)
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
  ImGui::PopID ();
  return pressed;
}

// Small drawing conveniences — the design repeats these three shapes
// everywhere, and spelling them out at each site buried the layout.
static void
draw_label (App & app, ImDrawList * dl, ImVec2 at, const char * text, ImU32 col, float size = 10.5f)
{
  dl->AddText (app.fonts.label, theme::fs (size), at, col, text);
}

static void
draw_mono (App & app, ImDrawList * dl, ImVec2 at, const char * text, ImU32 col, float size = 10.5f)
{
  dl->AddText (app.fonts.mono, theme::fs (size), at, col, text);
}

static float
mono_w (App & app, const char * text, float size = 10.5f)
{
  return app.fonts.mono->CalcTextSizeA (theme::fs (size), FLT_MAX, 0, text).x;
}

static float
label_w (App & app, const char * text, float size = 10.5f)
{
  return app.fonts.label->CalcTextSizeA (theme::fs (size), FLT_MAX, 0, text).x;
}

// The record control: a ring that fills when armed. The design has no icon set,
// and a circle is the one shape that reads as "record" without one.
static bool
record_dot (App & app, const char * id, bool on, ImVec2 at, float d, bool enabled)
{
  (void) app;
  ImGui::PushID (id);
  ImGui::SetCursorScreenPos (at);
  ImGui::InvisibleButton ("##rec", ImVec2 (du (d), du (d)));
  const bool hovered = enabled && ImGui::IsItemHovered ();
  const bool clicked = hovered && ImGui::IsMouseReleased (ImGuiMouseButton_Left);
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 c (at.x + du (d) / 2, at.y + du (d) / 2);
  const float r = du (d) / 2;
  const float dim = enabled ? 1.0f : 0.35f;

  if (on) {
    // Slow pulse: recording is a state worth noticing, but a blink would be
    // noise on a wall an operator watches for an hour.
    float t = 0.75f + 0.25f * (float) sin (ImGui::GetTime () * 3.0);
    dl->AddCircleFilled (c, r, theme::HexU32 (theme::StatusError, t));
  } else {
    dl->AddCircle (c, r - 0.5f, theme::WhiteU32 ((hovered ? 0.55f : 0.30f) * dim), 0, 1.2f);
  }
  if (hovered)
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
  ImGui::PopID ();
  return clicked;
}

// A 22x12 pill switch — the per-feed connect toggle, in the rail and in
// Settings. Returns true when pressed.
static bool
toggle_switch (App & app, const char * id, bool on, ImVec2 at, float w = 22.0f, float h = 12.0f)
{
  (void) app;
  ImGui::PushID (id);
  ImGui::SetCursorScreenPos (at);
  ImVec2 size (du (w), du (h));
  ImGui::InvisibleButton ("##sw", size);
  const bool hovered = ImGui::IsItemHovered ();
  const bool clicked = hovered && ImGui::IsMouseReleased (ImGuiMouseButton_Left);
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 p1 (at.x + size.x, at.y + size.y);
  dl->AddRectFilled (at, p1, on ? theme::HexU32 (theme::StatusOnline, 0.85f) : theme::WhiteU32 (0.12f), size.y / 2);
  const float knob = size.y * 0.66f;
  const float kx = on ? p1.x - knob / 2 - du (2.0f) : at.x + knob / 2 + du (2.0f);
  dl->AddCircleFilled (ImVec2 (kx, at.y + size.y / 2), knob / 2, on ? theme::WhiteU32 (1.0f) : theme::WhiteU32 (0.40f));
  if (hovered)
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
  ImGui::PopID ();
  return clicked;
}

// ----------------------------------------------------------------------------
//  Layout presets — three slots, stored in uavwall.conf as text
// ----------------------------------------------------------------------------

static std::string
serialise_tiles (const std::vector<Tile> & tiles)
{
  std::string out;
  char buf[128];
  for (const Tile & t : tiles) {
    snprintf (buf, sizeof (buf), "%d,%.0f,%.0f,%.0f,%.0f;", t.feed, t.x, t.y, t.w, t.h);
    out += buf;
  }
  return out;
}

static std::vector<Tile>
parse_tiles (const std::string & s, int feed_count)
{
  std::vector<Tile> out;
  std::size_t i = 0;
  while (i < s.size ()) {
    std::size_t end = s.find (';', i);
    if (end == std::string::npos)
      end = s.size ();
    Tile t;
    if (sscanf (s.substr (i, end - i).c_str (), "%d,%f,%f,%f,%f", &t.feed, &t.x, &t.y, &t.w, &t.h) == 5) {
      // A preset saved against a longer feed list must not resurrect tiles
      // pointing past the end of it.
      if (t.feed >= 0 && t.feed < feed_count && t.w > 0 && t.h > 0)
        out.push_back (t);
    }
    i = end + 1;
  }
  return out;
}

static std::string &
preset_slot (App & app, int i)
{
  return i == 0 ? app.cfg.preset_a : i == 1 ? app.cfg.preset_b : app.cfg.preset_c;
}

// The inspector's fixed height, so the cards above it can be fitted to what is
// left. Two states only: with a selection and without.
static float
inspector_h (const App & app)
{
  return du (app.inspect_feed >= 0 && app.inspect_feed < (int) app.feeds.size () ? 150.0f : 58.0f);
}

// Feed inspector, pinned to the foot of the rail: what this source actually is,
// and the two things an operator does about it.
static void
ui_inspector (App & app, ImVec2 at, float w)
{
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  const float h = inspector_h (app);
  ImVec2 p1 (at.x + w, at.y + h);
  const float pad = du (8.0f);

  const bool has = app.inspect_feed >= 0 && app.inspect_feed < (int) app.feeds.size ();
  if (!has) {
    // Dashed, so an empty inspector reads as a slot rather than a broken card.
    const float dash = du (4.0f);
    for (float x = at.x; x < p1.x; x += dash * 2)
      dl->AddLine (ImVec2 (x, at.y), ImVec2 (std::min (x + dash, p1.x), at.y), theme::WhiteU32 (0.14f), 1.0f);
    for (float x = at.x; x < p1.x; x += dash * 2)
      dl->AddLine (ImVec2 (x, p1.y), ImVec2 (std::min (x + dash, p1.x), p1.y), theme::WhiteU32 (0.14f), 1.0f);
    for (float y = at.y; y < p1.y; y += dash * 2) {
      dl->AddLine (ImVec2 (at.x, y), ImVec2 (at.x, std::min (y + dash, p1.y)), theme::WhiteU32 (0.14f), 1.0f);
      dl->AddLine (ImVec2 (p1.x, y), ImVec2 (p1.x, std::min (y + dash, p1.y)), theme::WhiteU32 (0.14f), 1.0f);
    }
    draw_label (app, dl, ImVec2 (at.x + pad, at.y + pad), "INSPECTOR", theme::WhiteU32 (theme::TextLabel));
    const char * msg = "Select a source to see its";
    const char * msg2 = "URL, transport and uptime.";
    dl->AddText (app.fonts.body, theme::fs (10.0f), ImVec2 (at.x + pad, at.y + pad + du (16.0f)),
                 theme::WhiteU32 (0.30f), msg);
    dl->AddText (app.fonts.body, theme::fs (10.0f), ImVec2 (at.x + pad, at.y + pad + du (28.0f)),
                 theme::WhiteU32 (0.30f), msg2);
    return;
  }

  Feed & f = app.feeds[(size_t) app.inspect_feed];
  const bool stalled = feed_stalled (f);

  dl->AddRectFilled (at, p1, theme::WhiteU32 (theme::PanelFillRaised), du (8.0f));
  if (stalled)
    dl->AddRect (at, p1, theme::HexU32 (theme::StatusWarn, 0.35f), du (8.0f), 0, 1.0f);

  float y = at.y + pad;
  draw_label (app, dl, ImVec2 (at.x + pad, y), "INSPECTOR", theme::WhiteU32 (theme::TextLabel));

  const char * state = stalled ? "STALLED" : feed_live (f) ? "LIVE" : "OFFLINE";
  ImU32 state_col = stalled ? theme::HexU32 (theme::StatusWarn)
                            : feed_live (f) ? theme::HexU32 (theme::StatusOnline) : theme::WhiteU32 (0.35f);
  dl->AddText (app.fonts.label, theme::fs (8.3f),
               ImVec2 (p1.x - pad - label_w (app, state, 8.3f), y), state_col, state);
  y += du (14.0f);

  dl->AddText (app.fonts.label, theme::fs (10.8f), ImVec2 (at.x + pad, y), theme::WhiteU32 (0.88f), f.name.c_str ());
  y += du (16.0f);

  // Label/value rows: label left, value right-aligned, so the numbers form a
  // column the eye can run down.
  auto row = [&] (const char * k, const std::string & v, ImU32 vc) {
    draw_mono (app, dl, ImVec2 (at.x + pad, y), k, theme::WhiteU32 (0.35f), 9.2f);
    std::string val = v;
    // Tail-elide: the end of an RTSP URL is the part that identifies the feed.
    const float room = w - pad * 2 - du (52.0f);
    while (val.size () > 4 && mono_w (app, val.c_str (), 9.2f) > room)
      val = "…" + val.substr (2);
    dl->AddText (app.fonts.mono, theme::fs (9.2f), ImVec2 (p1.x - pad - mono_w (app, val.c_str (), 9.2f), y), vc,
                 val.c_str ());
    y += du (12.0f);
  };

  row ("URL", f.url, theme::WhiteU32 (0.72f));
  char tbuf[64];
  snprintf (tbuf, sizeof (tbuf), "%s · %dms", app.cfg.rtsp_tcp ? "TCP" : "UDP", app.cfg.rtsp_latency_ms);
  row ("TRANSPORT", tbuf, theme::WhiteU32 (0.72f));

  if (f.connected && f.connected_at > 0) {
    int up = (int) (ImGui::GetTime () - f.connected_at);
    snprintf (tbuf, sizeof (tbuf), "%d:%02d:%02d", up / 3600, (up / 60) % 60, up % 60);
  } else {
    snprintf (tbuf, sizeof (tbuf), "—");
  }
  row ("UPTIME", tbuf, theme::WhiteU32 (0.72f));

  double age = f.last_frame_at > 0 ? ImGui::GetTime () - f.last_frame_at : -1.0;
  if (age < 0)
    snprintf (tbuf, sizeof (tbuf), "—");
  else
    snprintf (tbuf, sizeof (tbuf), "%.1fs", age);
  row ("LAST FRAME", tbuf, stalled ? theme::HexU32 (theme::StatusWarn) : theme::WhiteU32 (0.72f));

  if (!f.error.empty ()) {
    std::string e = f.error;
    while (!e.empty () && mono_w (app, e.c_str (), 9.2f) > w - pad * 2)
      e.pop_back ();
    draw_mono (app, dl, ImVec2 (at.x + pad, y), e.c_str (), theme::HexU32 (theme::StatusError, 0.90f), 9.2f);
  }

  // Two half-width actions along the bottom.
  const float bh = du (22.0f), bw = (w - pad * 2 - du (6.0f)) / 2;
  ImGui::SetCursorScreenPos (ImVec2 (at.x + pad, p1.y - pad - bh));
  if (deck_button (app, "insp_rc", "RECONNECT", ImVec2 (bw, bh), theme::AccentPrimary, Btn::Fill)) {
    stop_feed (f);
    start_feed (f);
    if (f.connected)
      f.connected_at = ImGui::GetTime ();
  }
  ImGui::SetCursorScreenPos (ImVec2 (at.x + pad + bw + du (6.0f), p1.y - pad - bh));
  if (deck_button (app, "insp_rm", "REMOVE", ImVec2 (bw, bh), 0xFFFFFF, Btn::Outline)) {
    int idx = app.inspect_feed;
    remove_feed_tiles (app, idx);
    stop_feed (app.feeds[(size_t) idx]);
    app.feeds.erase (app.feeds.begin () + idx);
    for (Tile & tl : app.tiles)
      if (tl.feed > idx)
        tl.feed--;
    app.inspect_feed = -1;
    app.fullscreen_feed = -1;
  }
}

// Left rail: every configured feed, with a live thumbnail, its state, and a
// connect toggle; the inspector sits at the foot.
static void
ui_feed_rail (App & app, float w, float h)
{
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (du (10.0f), du (10.0f)));
  ImGui::PushStyleColor (ImGuiCol_ChildBg, theme::Hex (0xFFFFFF, theme::PanelFill));
  ImGui::PushStyleVar (ImGuiStyleVar_ChildRounding, du (theme::RadiusDeck));
  ImGui::BeginChild ("##rail", ImVec2 (w, h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
  ImDrawList * dl = ImGui::GetWindowDrawList ();

  const float cw = ImGui::GetContentRegionAvail ().x;

  // ---- header -------------------------------------------------------------
  {
    ImVec2 at = ImGui::GetCursorScreenPos ();
    draw_label (app, dl, at, "SOURCES", theme::WhiteU32 (theme::TextLabel));
    int live = 0;
    for (const Feed & f : app.feeds)
      if (f.connected)
        live++;
    char buf[48];
    snprintf (buf, sizeof (buf), "%d LIVE / %d", live, (int) app.feeds.size ());
    draw_mono (app, dl, ImVec2 (at.x + cw - mono_w (app, buf), at.y - du (1.0f)), buf, theme::WhiteU32 (0.30f));
    ImGui::Dummy (ImVec2 (0, du (14.0f) + du (theme::GapTight)));
  }

  // ---- density ------------------------------------------------------------
  // Thumbnail height is what flexes: the rail must hold every card without
  // scrolling wherever it can, so fit to the space left after the inspector
  // rather than hard-coding a height per feed count.
  const float bar_w = du (2.5f), bar_gap = du (7.0f);
  const float body_w = cw - bar_w - bar_gap;
  const float meta_h = du (12.0f);
  const float insp_h = inspector_h (app);
  const int n = (int) app.feeds.size ();

  float avail = ImGui::GetContentRegionAvail ().y - insp_h - du (theme::Gap);
  float thumb_h = du (60.0f);
  if (n > 0) {
    float per_card = (avail + du (theme::GapTight)) / n - du (theme::GapTight);
    thumb_h = per_card - du (3.0f) - meta_h;
    thumb_h = std::min (thumb_h, du (60.0f));
    thumb_h = std::max (thumb_h, du (45.0f)); // clamp, never shrink further
  }
  const float card_h = thumb_h + du (3.0f) + meta_h;

  // Only scroll when the clamp bit — otherwise the rail is a fixed board.
  const bool scrolls = n > 0 && (card_h + du (theme::GapTight)) * n - du (theme::GapTight) > avail + 0.5f;

  // Transparent: the rail already painted PanelFill, and a nested child would
  // paint it again and show as a lighter block.
  ImGui::PushStyleColor (ImGuiCol_ChildBg, ImVec4 (0, 0, 0, 0));
  ImGui::BeginChild ("##cards", ImVec2 (cw, avail), ImGuiChildFlags_None,
                     scrolls ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar);
  dl = ImGui::GetWindowDrawList ();

  int toggle_feed = -1, rec_feed = -1;
  for (int i = 0; i < n; i++) {
    Feed & f = app.feeds[(size_t) i];
    ImGui::PushID (i);

    ImVec2 p0 = ImGui::GetCursorScreenPos ();
    const bool placed = feed_is_placed (app, i);
    const bool stalled = feed_stalled (f);
    const bool live = feed_live (f);
    const float dim = f.connected ? 1.0f : 0.62f; // an offline card recedes

    // Placement bar — a feed's relationship to the canvas, read down the edge.
    ImVec2 b0 (p0.x, p0.y), b1 (p0.x + bar_w, p0.y + card_h);
    dl->AddRectFilled (b0, b1,
                       placed   ? theme::HexU32 (theme::AccentPrimary)
                       : stalled ? theme::HexU32 (theme::StatusWarn)
                                 : theme::WhiteU32 (0.08f),
                       du (2.0f));

    ImVec2 t0 (p0.x + bar_w + bar_gap, p0.y), t1 (t0.x + body_w, t0.y + thumb_h);

    // The whole card is the hit target; the toggle below claims its own rect
    // first, so this must allow overlap or it swallows the toggle's click.
    ImGui::SetCursorScreenPos (p0);
    ImGui::SetNextItemAllowOverlap ();
    ImGui::InvisibleButton ("##card", ImVec2 (cw, card_h));
    const bool hovered = ImGui::IsItemHovered ();
    const bool dbl = hovered && ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left);

    if (f.texture && f.tex_w > 0) {
      dl->AddImageRounded ((ImTextureID) (intptr_t) f.texture, t0, t1, ImVec2 (0, 0), ImVec2 (1, 1),
                           theme::WhiteU32 (dim), du (theme::RadiusThumb));
      dl->AddRect (t0, t1,
                   stalled  ? theme::HexU32 (theme::StatusWarn, 0.55f)
                   : hovered ? theme::HexU32 (theme::AccentPrimary)
                             : theme::WhiteU32 (theme::TileStroke),
                   du (theme::RadiusThumb), 0, 1.0f);
      // Name straight on the picture — no plate, which would fight the frame.
      // A 1px shadow rather than a plate: real aerial footage is often bright
      // enough that white-on-nothing is unreadable, which the mock's dark
      // placeholder imagery never showed.
      dl->AddText (app.fonts.label, theme::fs (8.3f), ImVec2 (t0.x + du (6.0f) + 1, t0.y + du (5.0f) + 1),
                   theme::HexU32 (theme::WindowBgStart, 0.75f * dim), f.name.c_str ());
      dl->AddText (app.fonts.label, theme::fs (8.3f), ImVec2 (t0.x + du (6.0f), t0.y + du (5.0f)),
                   theme::WhiteU32 (0.85f * dim), f.name.c_str ());
    } else {
      dl->AddRectFilled (t0, t1, theme::WhiteU32 (0.04f), du (theme::RadiusThumb));
      // Dashed edge: nothing is arriving, and the card should not look like a
      // picture that happens to be black.
      const float dash = du (4.0f);
      for (float x = t0.x; x < t1.x; x += dash * 2) {
        dl->AddLine (ImVec2 (x, t0.y), ImVec2 (std::min (x + dash, t1.x), t0.y), theme::WhiteU32 (0.16f), 1.0f);
        dl->AddLine (ImVec2 (x, t1.y), ImVec2 (std::min (x + dash, t1.x), t1.y), theme::WhiteU32 (0.16f), 1.0f);
      }
      for (float y = t0.y; y < t1.y; y += dash * 2) {
        dl->AddLine (ImVec2 (t0.x, y), ImVec2 (t0.x, std::min (y + dash, t1.y)), theme::WhiteU32 (0.16f), 1.0f);
        dl->AddLine (ImVec2 (t1.x, y), ImVec2 (t1.x, std::min (y + dash, t1.y)), theme::WhiteU32 (0.16f), 1.0f);
      }
      const char * msg = f.connected ? "waiting…" : "OFFLINE";
      float tw = label_w (app, msg, 8.3f);
      dl->AddText (app.fonts.label, theme::fs (8.3f),
                   ImVec2 ((t0.x + t1.x - tw) / 2, (t0.y + t1.y) / 2 - theme::fs (4.0f)), theme::WhiteU32 (0.35f),
                   msg);
      dl->AddText (app.fonts.label, theme::fs (8.3f), ImVec2 (t0.x + du (6.0f), t0.y + du (5.0f)),
                   theme::WhiteU32 (0.55f * dim), f.name.c_str ());
    }

    // ---- meta row ---------------------------------------------------------
    const float my = t1.y + du (3.0f);
    ImVec2 m0 (t0.x, my + (meta_h - du (5.0f)) / 2);
    // A square marker, not a dot: it reads as an indicator rather than a bullet.
    dl->AddRectFilled (m0, ImVec2 (m0.x + du (5.0f), m0.y + du (5.0f)),
                       live      ? theme::HexU32 (theme::StatusOnline)
                       : stalled ? theme::HexU32 (theme::StatusWarn)
                                 : theme::WhiteU32 (0.25f));

    char meta[96];
    if (f.connected && f.tex_w > 0) {
      char rate[24];
      if (f.mbps >= 1000.0)
        snprintf (rate, sizeof (rate), "%.1fGb", f.mbps / 1000.0);
      else
        snprintf (rate, sizeof (rate), "%.0fMb", f.mbps);
      snprintf (meta, sizeof (meta), "%d×%d %.0ffps %s", f.tex_w, f.tex_h, f.fps, rate);
    }
    else if (f.connected)
      snprintf (meta, sizeof (meta), "no frames yet");
    else if (!f.error.empty ())
      snprintf (meta, sizeof (meta), "error");
    else
      snprintf (meta, sizeof (meta), "not connected");
    draw_mono (app, dl, ImVec2 (m0.x + du (5.0f) + du (5.0f), my + du (1.0f)), meta,
               f.error.empty () ? theme::WhiteU32 (0.55f) : theme::HexU32 (theme::StatusError, 0.75f), 9.2f);

    if (toggle_switch (app, "sw", f.connected, ImVec2 (t1.x - du (22.0f), my + (meta_h - du (12.0f)) / 2)))
      toggle_feed = i;
    // Recording is independent of the wall: ffmpeg opens the URL itself, so a
    // feed can be captured whether or not it is connected here or on canvas.
    if (record_dot (app, "rec", f.rec.active (), ImVec2 (t1.x - du (22.0f) - du (11.0f) - du (6.0f),
                                                        my + (meta_h - du (11.0f)) / 2),
                    11.0f, !g_ffmpeg.empty ()))
      rec_feed = i;

    // ---- gestures ---------------------------------------------------------
    if (hovered) {
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
      if (!f.error.empty ())
        ImGui::SetTooltip ("%s\n%s", f.url.c_str (), f.error.c_str ());
      else
        ImGui::SetTooltip ("%s\n%s", f.url.c_str (),
                           placed ? "double-click: full screen · click: remove from canvas"
                                  : "click: add to canvas · double-click: full screen");
    }
    if (dbl) {
      toggle_fullscreen (app, i);
      app.inspect_feed = i;
    } else if (hovered && ImGui::IsMouseReleased (ImGuiMouseButton_Left) && !ImGui::IsMouseDragging (0)) {
      app.inspect_feed = i;
      if (placed) {
        remove_feed_tiles (app, i);
      } else {
        Tile t;
        t.feed = i;
        t.w = app.cfg.canvas_w * 0.45f;
        t.h = t.w * 9.0f / 16.0f;
        t.x = 40 + 30.0f * app.tiles.size ();
        t.y = 40 + 30.0f * app.tiles.size ();
        app.tiles.push_back (t);
        app.fullscreen_feed = -1;
      }
    }

    ImGui::SetCursorScreenPos (ImVec2 (p0.x, p0.y + card_h + du (theme::GapTight)));
    ImGui::PopID ();
  }
  ImGui::EndChild ();
  ImGui::PopStyleColor ();

  if (rec_feed >= 0) {
    Feed & f = app.feeds[(size_t) rec_feed];
    if (f.rec.busy ())
      stop_recorder (f.rec, false);
    else
      start_feed_recording (app, f);
  }

  // Deferred: start_feed/stop_feed mutate the feed list's contents, so do it
  // after the loop rather than under the iteration.
  if (toggle_feed >= 0) {
    Feed & f = app.feeds[(size_t) toggle_feed];
    if (f.connected) {
      stop_feed (f);
      remove_feed_tiles (app, toggle_feed); // a source that is gone should not hold canvas space
      app.fullscreen_feed = -1;
    } else {
      start_feed (f);
      if (f.connected)
        f.connected_at = ImGui::GetTime ();
    }
  }

  ImGui::Dummy (ImVec2 (0, du (theme::Gap) - du (4.0f)));
  ui_inspector (app, ImGui::GetCursorScreenPos (), cw);

  ImGui::EndChild ();
  ImGui::PopStyleVar (2);
  ImGui::PopStyleColor ();
}

// ----------------------------------------------------------------------------
//  Settings — a real window with a left nav, not one long scrolling form
// ----------------------------------------------------------------------------

// A card the operator picks between, rather than a combo they have to open.
// The cost line is what makes the choice informed.
static bool
choice_card (App & app, const char * id, const char * title, const char * note, ImVec2 size, bool selected,
             bool enabled = true)
{
  ImGui::PushID (id);
  ImVec2 p0 = ImGui::GetCursorScreenPos ();
  ImGui::InvisibleButton ("##card", size);
  const bool hovered = enabled && ImGui::IsItemHovered ();
  const bool clicked = hovered && ImGui::IsMouseReleased (ImGuiMouseButton_Left);
  ImVec2 p1 (p0.x + size.x, p0.y + size.y);
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  const float dim = enabled ? 1.0f : 0.45f;

  if (selected) {
    dl->AddRectFilled (p0, p1, theme::HexU32 (theme::AccentPrimary, 0.16f * dim), du (8.0f));
    dl->AddRect (p0, p1, theme::HexU32 (theme::AccentPrimary, dim), du (8.0f), 0, 1.5f);
  } else {
    if (hovered)
      dl->AddRectFilled (p0, p1, theme::WhiteU32 (0.04f), du (8.0f));
    dl->AddRect (p0, p1, theme::WhiteU32 (theme::ControlStroke * dim), du (8.0f), 0, 1.0f);
  }
  dl->AddText (app.fonts.bodyBold, theme::fs (12.0f), ImVec2 (p0.x + du (10.0f), p0.y + du (8.0f)),
               theme::WhiteU32 (0.88f * dim), title);
  if (note)
    draw_mono (app, dl, ImVec2 (p0.x + du (10.0f), p0.y + du (8.0f) + theme::fs (14.0f)), note,
               theme::WhiteU32 (0.50f * dim), 9.5f);
  if (hovered)
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
  ImGui::PopID ();
  return clicked;
}

// Slider with a mono value and min/mid/max ticks — the stock ImGui slider does
// not carry its own scale, and these two settings both have a cost story.
static bool
instrument_slider (App & app, const char * id, int * v, int lo, int hi, const char * fmt, float w)
{
  ImGui::PushID (id);
  ImVec2 p0 = ImGui::GetCursorScreenPos ();
  const float h = du (26.0f);
  ImGui::InvisibleButton ("##sl", ImVec2 (w, h));
  const bool active = ImGui::IsItemActive ();
  ImDrawList * dl = ImGui::GetWindowDrawList ();

  const float track_h = du (6.0f);
  const float ty = p0.y + (h - track_h) / 2;
  const float knob_r = du (6.5f);
  const float x0 = p0.x + knob_r, x1 = p0.x + w - du (90.0f) - knob_r;

  bool changed = false;
  if (active) {
    float t = (ImGui::GetIO ().MousePos.x - x0) / std::max (1.0f, x1 - x0);
    int nv = lo + (int) std::lround (std::max (0.0f, std::min (1.0f, t)) * (hi - lo));
    if (nv != *v) {
      *v = nv;
      changed = true;
    }
  }

  const float frac = (float) (*v - lo) / std::max (1, hi - lo);
  const float kx = x0 + frac * (x1 - x0);
  dl->AddRectFilled (ImVec2 (x0 - knob_r, ty), ImVec2 (x1 + knob_r, ty + track_h), theme::WhiteU32 (0.09f),
                     track_h / 2);
  dl->AddRectFilled (ImVec2 (x0 - knob_r, ty), ImVec2 (kx, ty + track_h), theme::HexU32 (theme::AccentPrimary),
                     track_h / 2);
  dl->AddCircleFilled (ImVec2 (kx, ty + track_h / 2), knob_r, theme::WhiteU32 (1.0f));

  char buf[48];
  snprintf (buf, sizeof (buf), fmt, *v);
  draw_mono (app, dl, ImVec2 (x1 + knob_r + du (12.0f), p0.y + (h - theme::fs (10.5f)) / 2), buf,
             theme::WhiteU32 (0.80f));

  if (ImGui::IsItemHovered ())
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
  ImGui::PopID ();
  return changed;
}

static void
locked_callout (App & app, const char * text, float w)
{
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 at = ImGui::GetCursorScreenPos ();
  const float h = du (30.0f);
  ImVec2 p1 (at.x + w, at.y + h);
  dl->AddRectFilled (at, p1, theme::HexU32 (theme::StatusError, 0.10f), du (8.0f));
  dl->AddRect (at, p1, theme::HexU32 (theme::StatusError, 0.30f), du (8.0f), 0, 1.0f);
  dl->AddCircleFilled (ImVec2 (at.x + du (13.0f), at.y + h / 2), du (3.0f), theme::HexU32 (theme::StatusError));
  dl->AddText (app.fonts.body, theme::fs (11.5f), ImVec2 (at.x + du (24.0f), at.y + (h - theme::fs (11.5f)) / 2),
               theme::WhiteU32 (0.75f), text);
  ImGui::Dummy (ImVec2 (w, h));
}

// Text field styled for the panel: mono for machine data, DM Sans for names.
static bool
panel_field (App & app, const char * id, char * buf, size_t sz, float w, bool mono, bool password = false,
             bool error = false)
{
  ImGui::PushFont (mono ? app.fonts.mono : app.fonts.body);
  const float pad_y = std::max (2.0f, (du (26.0f) - ImGui::GetFontSize ()) / 2);
  ImGui::PushStyleVar (ImGuiStyleVar_FramePadding, ImVec2 (du (9.0f), pad_y));
  ImGui::PushStyleVar (ImGuiStyleVar_FrameRounding, du (theme::RadiusControl2));
  ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleColor (ImGuiCol_FrameBg, theme::Hex (0xFFFFFF, theme::PanelFillRaised));
  ImGui::PushStyleColor (ImGuiCol_Border,
                         error ? theme::Hex (theme::StatusError, 0.40f) : theme::Hex (0xFFFFFF, theme::ControlStroke));
  ImGui::SetNextItemWidth (w);
  bool changed = ImGui::InputText (id, buf, sz, password ? ImGuiInputTextFlags_Password : 0);
  ImGui::PopStyleColor (2);
  ImGui::PopStyleVar (3);
  ImGui::PopFont ();
  return changed;
}

static void
ui_settings (App & app)
{
  if (!app.show_settings)
    return;

  const float win_w = du (683.0f), win_h = du (500.0f);
  ImGui::SetNextWindowSize (ImVec2 (win_w, win_h));
  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.99f));
  ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (0xFFFFFF, 0.10f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (0, 0));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowRounding, du (theme::RadiusPanel));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::Begin ("##settings", &app.show_settings,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

  ImDrawList * dl = ImGui::GetWindowDrawList ();
  const ImVec2 win = ImGui::GetWindowPos ();
  const bool live = app.call_started;

  // ---- title bar ----------------------------------------------------------
  const float title_h = du (38.0f);
  draw_label (app, dl, ImVec2 (win.x + du (17.0f), win.y + (title_h - theme::fs (10.5f)) / 2), "SETTINGS",
              theme::WhiteU32 (0.85f));
  draw_mono (app, dl, ImVec2 (win.x + win_w - du (17.0f) - mono_w (app, "uavwall.conf", 10.0f),
                              win.y + (title_h - theme::fs (10.0f)) / 2),
             "uavwall.conf", theme::WhiteU32 (0.30f), 10.0f);
  dl->AddRectFilled (ImVec2 (win.x, win.y + title_h), ImVec2 (win.x + win_w, win.y + title_h + 1),
                     theme::WhiteU32 (0.07f));

  // ---- left nav -----------------------------------------------------------
  const float nav_w = du (163.0f);
  dl->AddRectFilled (ImVec2 (win.x + nav_w, win.y + title_h), ImVec2 (win.x + nav_w + 1, win.y + win_h),
                     theme::WhiteU32 (0.07f));
  {
    static const char * tabs[] = {"Conference", "Registration", "Canvas & sending",
                                  "Feeds",      "Recording",    "Interface"};
    float y = win.y + title_h + du (8.0f);
    for (int i = 0; i < 6; i++) {
      ImVec2 r0 (win.x + du (12.0f), y), r1 (win.x + nav_w - du (12.0f), y + du (27.0f));
      ImGui::PushID (4000 + i);
      ImGui::SetCursorScreenPos (r0);
      ImGui::InvisibleButton ("##nav", ImVec2 (r1.x - r0.x, r1.y - r0.y));
      const bool hov = ImGui::IsItemHovered ();
      if (hov && ImGui::IsMouseReleased (ImGuiMouseButton_Left))
        app.settings_tab = i;
      const bool sel = app.settings_tab == i;
      if (sel)
        dl->AddRectFilled (r0, r1, theme::HexU32 (theme::AccentPrimary, 0.92f), du (8.0f));
      else if (hov)
        dl->AddRectFilled (r0, r1, theme::WhiteU32 (0.05f), du (8.0f));
      dl->AddText (sel ? app.fonts.bodyBold : app.fonts.smallMed, theme::fs (11.7f),
                   ImVec2 (r0.x + du (10.0f), r0.y + (du (27.0f) - theme::fs (11.7f)) / 2 - du (1.0f)),
                   sel ? theme::WhiteU32 (1.0f) : theme::WhiteU32 (0.60f), tabs[i]);
      if (i == 3) {
        char cnt[16];
        snprintf (cnt, sizeof (cnt), "%d", (int) app.feeds.size ());
        draw_mono (app, dl,
                   ImVec2 (r1.x - du (10.0f) - mono_w (app, cnt, 10.0f), r0.y + (du (27.0f) - theme::fs (10.0f)) / 2),
                   cnt, sel ? theme::WhiteU32 (0.75f) : theme::WhiteU32 (0.30f), 10.0f);
      }
      if (hov)
        ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
      ImGui::PopID ();
      y += du (27.0f) + du (4.0f);
    }

    if (live) {
      ImVec2 c0 (win.x + du (12.0f), win.y + win_h - du (52.0f));
      ImVec2 c1 (win.x + nav_w - du (12.0f), win.y + win_h - du (12.0f));
      dl->AddRectFilled (c0, c1, theme::HexU32 (theme::StatusError, 0.10f), du (8.0f));
      dl->AddText (app.fonts.microCap, theme::fs (8.5f), ImVec2 (c0.x + du (10.0f), c0.y + du (8.0f)),
                   theme::HexU32 (theme::StatusError, 0.85f), "SENDING");
      draw_mono (app, dl, ImVec2 (c0.x + du (10.0f), c0.y + du (21.0f)), "live — some", theme::WhiteU32 (0.55f),
                 9.5f);
      draw_mono (app, dl, ImVec2 (c0.x + du (10.0f), c0.y + du (31.0f)), "fields locked", theme::WhiteU32 (0.55f),
                 9.5f);
    }
  }

  // ---- panel --------------------------------------------------------------
  const float pad_x = du (15.0f), pad_y = du (17.0f);
  const float panel_x = win.x + nav_w + 1 + pad_x;
  const float panel_w = win_w - nav_w - 1 - pad_x * 2;
  const float foot_h = du (27.0f);
  ImGui::SetCursorScreenPos (ImVec2 (panel_x, win.y + title_h + pad_y));

  // Rows advance from an explicit origin. Reading the cursor back after a
  // widget instead would compound each control's own height into the gap, and
  // the form would drift further apart with every field.
  float row_y = 0.0f;
  auto seek = [&] (float y) {
    row_y = y;
    ImGui::SetCursorScreenPos (ImVec2 (panel_x, row_y));
  };
  auto heading = [&] (const char * title, const char * sub) {
    ImVec2 at = ImGui::GetCursorScreenPos ();
    draw_label (app, dl, at, title, theme::WhiteU32 (theme::TextLabel));
    dl->AddText (app.fonts.body, theme::fs (11.5f), ImVec2 (at.x, at.y + du (16.0f)), theme::WhiteU32 (0.40f), sub);
    seek (at.y + du (16.0f) + theme::fs (11.5f) + du (13.0f));
  };
  auto gap = [&] (float g = 8.0f) { seek (row_y + du (g)); };
  auto row = [&] (const char * label) {
    dl->AddText (app.fonts.smallMed, theme::fs (12.0f),
                 ImVec2 (panel_x, row_y + (du (26.0f) - theme::fs (12.0f)) / 2), theme::WhiteU32 (0.70f), label);
    ImGui::SetCursorScreenPos (ImVec2 (panel_x + du (125.0f), row_y));
  };
  auto next_row = [&] (float rh = 26.0f) { seek (row_y + du (rh) + du (6.0f)); };

  static char vmr[512], pin[64], dname[128], host[256], alias[256], user[256], pass[256];
  if (ImGui::IsWindowAppearing ()) {
    snprintf (vmr, sizeof (vmr), "%s", app.cfg.vmr.c_str ());
    snprintf (pin, sizeof (pin), "%s", app.cfg.pin.c_str ());
    snprintf (dname, sizeof (dname), "%s", app.cfg.display_name.c_str ());
    snprintf (host, sizeof (host), "%s", app.cfg.reg_host.c_str ());
    snprintf (alias, sizeof (alias), "%s", app.cfg.reg_alias.c_str ());
    snprintf (user, sizeof (user), "%s", app.cfg.reg_user.c_str ());
    snprintf (pass, sizeof (pass), "%s", app.cfg.reg_pass.c_str ());
  }

  switch (app.settings_tab) {
  case 0: { // ---- Conference ----------------------------------------------
    heading ("CONFERENCE", "Where the composed canvas is sent.");
    ImGui::BeginDisabled (live);
    row ("VMR");
    if (panel_field (app, "##svmr", vmr, sizeof (vmr), du (300.0f), true))
      app.cfg.vmr = vmr;
    next_row ();
    row ("PIN");
    if (panel_field (app, "##spin", pin, sizeof (pin), du (120.0f), true, true))
      app.cfg.pin = pin;
    next_row ();
    row ("Display name");
    if (panel_field (app, "##sdn", dname, sizeof (dname), du (300.0f), false))
      app.cfg.display_name = dname;
    next_row ();
    ImGui::EndDisabled ();

    if (live) {
      locked_callout (app, "Locked while sending. Stop sending to change the destination.", panel_w);
      gap (12.0f);
    }

    // The rest of the file at a glance, so the operator can see the whole
    // configuration without leaving the panel.
    ImVec2 at = ImGui::GetCursorScreenPos ();
    draw_label (app, dl, at, "ALSO IN THIS FILE", theme::WhiteU32 (theme::TextLabel), 9.0f);
    char sum[256];
    snprintf (sum, sizeof (sum), "canvas=%dx%d  send_fps=%d", app.cfg.canvas_w, app.cfg.canvas_h, app.cfg.send_fps);
    draw_mono (app, dl, ImVec2 (at.x, at.y + du (16.0f)), sum, theme::WhiteU32 (0.45f), 9.5f);
    snprintf (sum, sizeof (sum), "send_as=%s  rtsp_transport=%s", app.cfg.send_as_content ? "content" : "main",
              app.cfg.rtsp_tcp ? "tcp" : "udp");
    draw_mono (app, dl, ImVec2 (at.x, at.y + du (28.0f)), sum, theme::WhiteU32 (0.45f), 9.5f);
    break;
  }

  case 1: { // ---- Registration --------------------------------------------
    heading ("REGISTRATION", "Register the wall so a conference can dial it.");
    const int rs = app.reg_status.load ();
    const bool registered = rs == PULSE_CONNECTION_STATUS_CONNECTED;
    const bool busy = app.reg_in_flight.load () || rs == PULSE_CONNECTION_STATUS_CONNECTING ||
                      rs == PULSE_CONNECTION_STATUS_DISCONNECTING;

    ImGui::BeginDisabled (registered || busy);
    row ("Host / domain");
    if (panel_field (app, "##shost", host, sizeof (host), du (300.0f), true))
      app.cfg.reg_host = host;
    next_row ();
    row ("Device alias");
    if (panel_field (app, "##salias", alias, sizeof (alias), du (300.0f), true))
      app.cfg.reg_alias = alias;
    next_row ();
    row ("Username");
    if (panel_field (app, "##suser", user, sizeof (user), du (220.0f), false))
      app.cfg.reg_user = user;
    next_row ();
    row ("Password");
    if (panel_field (app, "##spass", pass, sizeof (pass), du (220.0f), false, true))
      app.cfg.reg_pass = pass;
    next_row ();
    ImGui::EndDisabled ();

    row ("Register at startup");
    if (toggle_switch (app, "regauto", app.cfg.reg_auto, ImGui::GetCursorScreenPos (), 28.0f, 15.0f))
      app.cfg.reg_auto = !app.cfg.reg_auto;
    next_row (18.0f);
    row ("Answer automatically");
    if (toggle_switch (app, "autoacc", app.cfg.auto_accept, ImGui::GetCursorScreenPos (), 28.0f, 15.0f))
      app.cfg.auto_accept = !app.cfg.auto_accept;
    next_row (18.0f);
    gap (6.0f);

    const float bw = label_w (app, "DEREGISTER", 9.5f) + du (15.0f) * 2;
    if (registered) {
      if (deck_button (app, "dereg", "DEREGISTER", ImVec2 (bw, du (27.0f)), theme::StatusError, Btn::Outline, !busy))
        start_deregister (app);
    } else {
      if (deck_button (app, "reg", "REGISTER", ImVec2 (bw, du (27.0f)), theme::AccentPrimary, Btn::Fill, !busy)) {
        save_config (app);
        start_register (app);
      }
    }
    ImGui::SameLine (0.0f, du (12.0f));
    {
      ImVec2 at = ImGui::GetCursorScreenPos ();
      const char * word = registered ? "REGISTERED" : busy ? "WORKING…" : "NOT REGISTERED";
      draw_mono (app, dl, ImVec2 (at.x, at.y + du (8.0f)), word,
                 registered ? theme::HexU32 (theme::StatusOnline, 0.9f) : theme::WhiteU32 (0.40f));
    }
    break;
  }

  case 2: { // ---- Canvas & sending ----------------------------------------
    heading ("CANVAS & SENDING", "What the far end receives, and how it arrives.");
    static const struct
    {
      const char * title;
      const char * note;
      int w, h;
    } sizes[] = {{"1920×1080", "78% CPU · 16.6 ms", 1920, 1080},
                 {"1280×720", "48% CPU · half the pixels", 1280, 720},
                 {"960×540", "lightest", 960, 540}};

    const float cw = (panel_w - du (8.0f) * 2) / 3, cardh = du (46.0f);
    ImVec2 base = ImGui::GetCursorScreenPos ();
    for (int i = 0; i < 3; i++) {
      ImGui::SetCursorScreenPos (ImVec2 (base.x + (cw + du (8.0f)) * i, base.y));
      const bool sel = sizes[i].w == app.cfg.canvas_w && sizes[i].h == app.cfg.canvas_h;
      char cid[16];
      snprintf (cid, sizeof (cid), "sz%d", i);
      if (choice_card (app, cid, sizes[i].title, sizes[i].note, ImVec2 (cw, cardh), sel) && !sel) {
        // Rescale existing tiles so the layout survives the change.
        float sx = (float) sizes[i].w / app.cfg.canvas_w, sy = (float) sizes[i].h / app.cfg.canvas_h;
        for (Tile & t : app.tiles) {
          t.x *= sx;
          t.y *= sy;
          t.w *= sx;
          t.h *= sy;
        }
        app.cfg.canvas_w = sizes[i].w;
        app.cfg.canvas_h = sizes[i].h;
      }
    }
    seek (base.y + cardh + du (16.0f));

    row ("Frame rate");
    instrument_slider (app, "fps", &app.cfg.send_fps, 10, 30, "%d fps", panel_w - du (125.0f));
    next_row ();
    gap (4.0f);

    ImVec2 at = ImGui::GetCursorScreenPos ();
    draw_label (app, dl, at, "SEND AS", theme::WhiteU32 (theme::TextLabel), 9.0f);
    seek (at.y + du (18.0f));
    base = ImGui::GetCursorScreenPos ();
    const float cw2 = (panel_w - du (8.0f)) / 2;
    ImGui::BeginDisabled (live);
    ImGui::SetCursorScreenPos (base);
    if (choice_card (app, "asmain", "Main video", "replaces our camera", ImVec2 (cw2, cardh), !app.cfg.send_as_content,
                     !live))
      app.cfg.send_as_content = false;
    ImGui::SetCursorScreenPos (ImVec2 (base.x + cw2 + du (8.0f), base.y));
    if (choice_card (app, "ascont", "Content", "shown alongside people", ImVec2 (cw2, cardh),
                     app.cfg.send_as_content, !live))
      app.cfg.send_as_content = true;
    ImGui::EndDisabled ();
    seek (base.y + cardh + du (10.0f));
    if (live)
      locked_callout (app, "Locked while sending. Stop sending to change the stream.", panel_w);
    break;
  }

  case 3: { // ---- Feeds ---------------------------------------------------
    heading ("FEEDS", "The RTSP sources this wall can place on the canvas.");
    ImGui::BeginDisabled (live);
    row ("Transport");
    {
      static const char * tr[] = {"TCP", "UDP"};
      int hit = segmented (app, "tr", tr, 2, app.cfg.rtsp_tcp ? 0 : 1, du (58.0f), du (26.0f));
      if (hit >= 0)
        app.cfg.rtsp_tcp = (hit == 0);
    }
    next_row ();
    row ("Jitter buffer");
    instrument_slider (app, "jit", &app.cfg.rtsp_latency_ms, 0, 1000, "%d ms", panel_w - du (125.0f));
    next_row ();
    ImGui::EndDisabled ();
    row ("Connect at startup");
    if (toggle_switch (app, "autoconn", app.cfg.autoconnect, ImGui::GetCursorScreenPos (), 28.0f, 15.0f))
      app.cfg.autoconnect = !app.cfg.autoconnect;
    next_row (18.0f);
    gap (4.0f);

    // ---- table ------------------------------------------------------------
    const float col_name = du (98.0f), col_state = du (80.0f);
    const float col_url = panel_w - col_name - col_state - du (16.0f);
    ImVec2 hdr = ImGui::GetCursorScreenPos ();
    dl->AddText (app.fonts.microCap, theme::fs (8.5f), hdr, theme::WhiteU32 (0.30f), "NAME");
    dl->AddText (app.fonts.microCap, theme::fs (8.5f), ImVec2 (hdr.x + col_name + du (8.0f), hdr.y),
                 theme::WhiteU32 (0.30f), "RTSP URL");
    dl->AddText (app.fonts.microCap, theme::fs (8.5f),
                 ImVec2 (hdr.x + col_name + col_url + du (16.0f), hdr.y), theme::WhiteU32 (0.30f), "STATE");
    seek (hdr.y + du (14.0f));

    const float rows_h = win.y + win_h - ImGui::GetCursorScreenPos ().y - foot_h - pad_y - du (36.0f);
    ImGui::BeginChild ("##feedrows", ImVec2 (panel_w, std::max (du (40.0f), rows_h)));
    ImDrawList * rdl = ImGui::GetWindowDrawList ();
    int remove = -1, toggle = -1;
    for (int i = 0; i < (int) app.feeds.size (); i++) {
      Feed & f = app.feeds[(size_t) i];
      ImGui::PushID (5000 + i);
      ImVec2 r0 = ImGui::GetCursorScreenPos ();

      char nbuf[128], ubuf[512];
      snprintf (nbuf, sizeof (nbuf), "%s", f.name.c_str ());
      snprintf (ubuf, sizeof (ubuf), "%s", f.url.c_str ());

      ImGui::SetCursorScreenPos (r0);
      ImGui::PushFont (app.fonts.bodyBold);
      if (panel_field (app, "##fn", nbuf, sizeof (nbuf), col_name, false))
        f.name = nbuf;
      ImGui::PopFont ();

      ImGui::SetCursorScreenPos (ImVec2 (r0.x + col_name + du (8.0f), r0.y));
      if (panel_field (app, "##fu", ubuf, sizeof (ubuf), col_url, true, false, !f.error.empty ()))
        f.url = ubuf;

      const float sx = r0.x + col_name + col_url + du (16.0f);
      if (toggle_switch (app, "fsw", f.connected, ImVec2 (sx, r0.y + du (7.0f)), 26.0f, 14.0f))
        toggle = i;
      const char * st = feed_stalled (f)  ? "STALLED"
                        : feed_live (f)   ? "LIVE"
                        : !f.error.empty () ? "ERROR"
                                            : "OFF";
      ImU32 sc = feed_stalled (f)     ? theme::HexU32 (theme::StatusWarn)
                 : feed_live (f)      ? theme::HexU32 (theme::StatusOnline)
                 : !f.error.empty () ? theme::HexU32 (theme::StatusError)
                                     : theme::WhiteU32 (0.35f);
      rdl->AddText (app.fonts.label, theme::fs (8.3f), ImVec2 (sx + du (32.0f), r0.y + du (9.0f)), sc, st);

      ImGui::SetCursorScreenPos (ImVec2 (r0.x, r0.y + du (30.0f)));
      ImGui::PopID ();
      (void) remove;
    }
    ImGui::EndChild ();

    if (toggle >= 0) {
      Feed & f = app.feeds[(size_t) toggle];
      if (f.connected) {
        stop_feed (f);
        remove_feed_tiles (app, toggle);
        app.fullscreen_feed = -1;
      } else {
        start_feed (f);
        if (f.connected)
          f.connected_at = ImGui::GetTime ();
      }
    }

    seek (win.y + win_h - foot_h - pad_y - du (32.0f));
    {
      // Dashed outline: this row adds something that is not there yet.
      ImVec2 a0 = ImGui::GetCursorScreenPos ();
      const float aw = du (93.0f), ah = du (24.0f);
      ImGui::InvisibleButton ("##addfeed", ImVec2 (aw, ah));
      const bool hov = ImGui::IsItemHovered ();
      ImVec2 a1 (a0.x + aw, a0.y + ah);
      const float dash = du (4.0f);
      ImU32 dc = theme::WhiteU32 (hov ? 0.40f : 0.20f);
      for (float x = a0.x; x < a1.x; x += dash * 2) {
        dl->AddLine (ImVec2 (x, a0.y), ImVec2 (std::min (x + dash, a1.x), a0.y), dc, 1.0f);
        dl->AddLine (ImVec2 (x, a1.y), ImVec2 (std::min (x + dash, a1.x), a1.y), dc, 1.0f);
      }
      for (float y = a0.y; y < a1.y; y += dash * 2) {
        dl->AddLine (ImVec2 (a0.x, y), ImVec2 (a0.x, std::min (y + dash, a1.y)), dc, 1.0f);
        dl->AddLine (ImVec2 (a1.x, y), ImVec2 (a1.x, std::min (y + dash, a1.y)), dc, 1.0f);
      }
      float tw = label_w (app, "+ ADD FEED", 9.2f);
      dl->AddText (app.fonts.label, theme::fs (9.2f),
                   ImVec2 (a0.x + (aw - tw) / 2, a0.y + (ah - theme::fs (9.2f)) / 2), theme::WhiteU32 (0.55f),
                   "+ ADD FEED");
      if (hov)
        ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
      if (hov && ImGui::IsMouseReleased (ImGuiMouseButton_Left)) {
        Feed nf;
        char buf[64];
        snprintf (buf, sizeof (buf), "FEED %02d", (int) app.feeds.size () + 1);
        nf.name = buf;
        nf.url = "rtsp://";
        app.feeds.push_back (std::move (nf));
      }
    }
    break;
  }

  case 4: { // ---- Recording ---------------------------------------------
    heading ("RECORDING", "Captured with ffmpeg, alongside the wall.");

    static char rdir[512];
    if (ImGui::IsWindowAppearing ())
      snprintf (rdir, sizeof (rdir), "%s", app.cfg.record_dir.c_str ());
    row ("Folder");
    if (panel_field (app, "##rdir", rdir, sizeof (rdir), panel_w - du (125.0f), true))
      app.cfg.record_dir = rdir;
    next_row ();
    {
      ImVec2 at = ImGui::GetCursorScreenPos ();
      dl->AddText (app.fonts.body, theme::fs (11.0f), ImVec2 (at.x + du (125.0f), at.y), theme::WhiteU32 (0.40f),
                   "Relative to the working directory. Created if missing.");
      seek (at.y + du (22.0f));
    }
    gap (6.0f);

    // Whether the feature works at all comes down to one binary being present,
    // so say which one was found rather than failing silently at the controls.
    {
      ImVec2 at = ImGui::GetCursorScreenPos ();
      const bool ok = !g_ffmpeg.empty ();
      dl->AddCircleFilled (ImVec2 (at.x + du (3.0f), at.y + theme::fs (10.5f) / 2), du (3.0f),
                           ok ? theme::HexU32 (theme::StatusOnline) : theme::HexU32 (theme::StatusError));
      draw_mono (app, dl, ImVec2 (at.x + du (12.0f), at.y), ok ? g_ffmpeg.c_str () : "ffmpeg not found",
                 ok ? theme::WhiteU32 (0.60f) : theme::HexU32 (theme::StatusError, 0.90f), 9.5f);
      if (!ok)
        dl->AddText (app.fonts.body, theme::fs (11.0f), ImVec2 (at.x, at.y + du (16.0f)), theme::WhiteU32 (0.40f),
                     "Install it (brew install ffmpeg) to enable the record controls.");
      seek (at.y + du (ok ? 22.0f : 38.0f));
    }
    gap (8.0f);

    {
      ImVec2 at = ImGui::GetCursorScreenPos ();
      draw_label (app, dl, at, "IN PROGRESS", theme::WhiteU32 (theme::TextLabel), 9.0f);
      float ry = at.y + du (18.0f);
      int shown = 0;
      auto line = [&] (const char * what, const Recorder & r) {
        int d = (int) (ImGui::GetTime () - r.started_at);
        char t[64];
        snprintf (t, sizeof (t), "%d:%02d:%02d", d / 3600, (d / 60) % 60, d % 60);
        dl->AddCircleFilled (ImVec2 (at.x + du (3.0f), ry + theme::fs (9.5f) / 2), du (3.0f),
                             theme::HexU32 (theme::StatusError));
        draw_mono (app, dl, ImVec2 (at.x + du (12.0f), ry), what, theme::WhiteU32 (0.75f), 9.5f);
        draw_mono (app, dl, ImVec2 (at.x + du (140.0f), ry), t, theme::WhiteU32 (0.50f), 9.5f);
        std::string base = r.path.substr (r.path.find_last_of ('/') + 1);
        if (r.dropped > 0) {
          char d[64];
          snprintf (d, sizeof (d), "%llu dropped", (unsigned long long) r.dropped);
          draw_mono (app, dl, ImVec2 (at.x + du (200.0f), ry), d, theme::HexU32 (theme::StatusWarn, 0.85f), 9.5f);
        } else {
          draw_mono (app, dl, ImVec2 (at.x + du (200.0f), ry), base.c_str (), theme::WhiteU32 (0.35f), 9.5f);
        }
        ry += du (14.0f);
        shown++;
      };
      if (app.canvas_rec.active ())
        line ("canvas", app.canvas_rec);
      for (const Feed & f : app.feeds)
        if (f.rec.active ())
          line (f.name.c_str (), f.rec);
      if (shown == 0)
        dl->AddText (app.fonts.body, theme::fs (11.0f), ImVec2 (at.x, ry), theme::WhiteU32 (0.35f),
                     "Nothing recording. Arm a source in the rail, or the canvas above it.");
      seek (ry + du (18.0f));
    }
    break;
  }

  default: { // ---- Interface ----------------------------------------------
    heading ("INTERFACE", "How the wall itself is drawn.");
    row ("UI scale");
    {
      int pct = (int) std::lround (app.cfg.ui_scale * 100.0f);
      if (instrument_slider (app, "uis", &pct, 80, 200, "%d%%", panel_w - du (125.0f)))
        app.cfg.ui_scale = pct / 100.0f;
    }
    next_row ();
    {
      ImVec2 at = ImGui::GetCursorScreenPos ();
      dl->AddText (app.fonts.body, theme::fs (11.0f), ImVec2 (at.x + du (125.0f), at.y), theme::WhiteU32 (0.40f),
                   "Applies on next start.");
      seek (at.y + du (22.0f));
    }
    row ("Show stats");
    if (toggle_switch (app, "shstats", app.cfg.show_stats, ImGui::GetCursorScreenPos (), 28.0f, 15.0f))
      app.cfg.show_stats = !app.cfg.show_stats;
    next_row (18.0f);
    break;
  }
  }

  // ---- footer -------------------------------------------------------------
  {
    const float sw = label_w (app, "SAVE", 9.5f) + du (15.0f) * 2;
    const float cw = label_w (app, "CLOSE", 9.5f) + du (15.0f) * 2;
    float fx = win.x + win_w - pad_x - cw;
    ImGui::SetCursorScreenPos (ImVec2 (fx, win.y + win_h - pad_y - foot_h));
    if (deck_button (app, "sclose", "CLOSE", ImVec2 (cw, foot_h), 0xFFFFFF, Btn::Outline))
      app.show_settings = false;
    fx -= sw + du (8.0f);
    ImGui::SetCursorScreenPos (ImVec2 (fx, win.y + win_h - pad_y - foot_h));
    if (deck_button (app, "ssave", "SAVE", ImVec2 (sw, foot_h), theme::AccentPrimary, Btn::Fill)) {
      save_config (app);
      g_cfg_rtsp_tcp = app.cfg.rtsp_tcp;
      g_cfg_rtsp_latency_ms = app.cfg.rtsp_latency_ms;
      set_status (app, "Saved to uavwall.conf");
      app.show_settings = false;
    }
  }

  ImGui::End ();
  ImGui::PopStyleVar (3);
  ImGui::PopStyleColor (2);
}

// A coloured strip along the top edge of an overlay window — the same tally
// language as the main window, so "this is urgent" reads the same everywhere.
static void
overlay_strip (ImU32 col)
{
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 p = ImGui::GetWindowPos ();
  float w = ImGui::GetWindowSize ().x;
  dl->PushClipRect (p, ImVec2 (p.x + w, p.y + du (14.0f)), true);
  dl->AddRectFilled (p, ImVec2 (p.x + w, p.y + du (3.0f)), col, du (theme::RadiusDeck),
                     ImDrawFlags_RoundCornersTop);
  dl->PopClipRect ();
}

// Incoming-call banner. A real window, top-centre, so its buttons are reliably
// clickable above the shell and the canvas.
static void
ui_incoming (App & app, ImVec2 win_size)
{
  if (!app.incoming_pending.load ())
    return;

  std::string from, alias;
  {
    std::lock_guard<std::mutex> lock (app.incoming_mutex);
    from = app.incoming_from;
    alias = app.incoming_alias;
  }
  const bool busy = app.incoming_busy.load ();

  ImVec2 vp = ImGui::GetMainViewport ()->Pos;
  ImGui::SetNextWindowPos (ImVec2 (vp.x + win_size.x / 2, vp.y + du (70.0f)), ImGuiCond_Always, ImVec2 (0.5f, 0.0f));
  ImGui::SetNextWindowSize (ImVec2 (du (busy ? 430.0f : 380.0f), 0));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.99f));
  ImGui::PushStyleColor (ImGuiCol_Border,
                         busy ? theme::Hex (theme::StatusError, 0.55f) : theme::Hex (theme::AccentPrimary, 0.55f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (du (16.0f), du (14.0f)));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_WindowRounding, du (theme::RadiusDeck));
  ImGui::Begin ("##incoming", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

  overlay_strip (busy ? theme::HexU32 (theme::StatusError) : theme::HexU32 (theme::AccentPrimary));
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  const float w = ImGui::GetContentRegionAvail ().x;

  ImGui::Dummy (ImVec2 (0, du (2.0f)));
  {
    ImVec2 at = ImGui::GetCursorScreenPos ();
    draw_label (app, dl, at, "INCOMING CALL",
                busy ? theme::HexU32 (theme::StatusError) : theme::HexU32 (theme::AccentPrimary));
    ImGui::Dummy (ImVec2 (0, du (15.0f)));
  }
  {
    ImVec2 at = ImGui::GetCursorScreenPos ();
    dl->AddText (app.fonts.bodyBold, theme::fs (13.3f), at, theme::WhiteU32 (0.92f),
                 from.empty () ? "Unknown caller" : from.c_str ());
    ImGui::Dummy (ImVec2 (0, theme::fs (15.0f)));
  }
  if (!alias.empty ()) {
    ImVec2 at = ImGui::GetCursorScreenPos ();
    draw_mono (app, dl, at, alias.c_str (), theme::WhiteU32 (0.42f), 10.4f);
    ImGui::Dummy (ImVec2 (0, theme::fs (12.0f)));
  }

  const float bh = du (28.0f);
  if (busy) {
    // Explain the consequence before offering the button that causes it.
    ImGui::Dummy (ImVec2 (0, du (6.0f)));
    ImVec2 at = ImGui::GetCursorScreenPos ();
    const float eh = du (34.0f);
    dl->AddRectFilled (at, ImVec2 (at.x + w, at.y + eh), theme::WhiteU32 (0.05f), du (8.0f));
    char msg[320];
    snprintf (msg, sizeof (msg), "Accepting leaves %s — the wall", app.cfg.vmr.c_str ());
    dl->AddText (app.fonts.body, theme::fs (11.0f), ImVec2 (at.x + du (10.0f), at.y + du (6.0f)),
                 theme::WhiteU32 (0.75f), msg);
    dl->AddText (app.fonts.body, theme::fs (11.0f), ImVec2 (at.x + du (10.0f), at.y + du (19.0f)),
                 theme::WhiteU32 (0.75f), "stops sending there first.");
    ImGui::Dummy (ImVec2 (w, eh + du (10.0f)));

    const float aw = label_w (app, "DISCONNECT & ACCEPT", 9.5f) + du (14.0f) * 2;
    const float rw = label_w (app, "REJECT", 9.5f) + du (14.0f) * 2;
    ImVec2 base = ImGui::GetCursorScreenPos ();
    ImGui::SetCursorScreenPos (ImVec2 (base.x + w - rw, base.y));
    if (deck_button (app, "inc_rej", "REJECT", ImVec2 (rw, bh), 0xFFFFFF, Btn::Outline))
      app.incoming_answer.store (0);
    ImGui::SetCursorScreenPos (ImVec2 (base.x + w - rw - aw - du (8.0f), base.y));
    if (deck_button (app, "inc_acc", "DISCONNECT & ACCEPT", ImVec2 (aw, bh), theme::StatusError, Btn::Fill))
      app.incoming_answer.store (1);
    ImGui::SetCursorScreenPos (ImVec2 (base.x, base.y + bh));
  } else {
    const float aw = label_w (app, "ACCEPT", 9.5f) + du (14.0f) * 2;
    const float dw = label_w (app, "DECLINE", 9.5f) + du (14.0f) * 2;
    ImGui::Dummy (ImVec2 (0, du (4.0f)));
    ImVec2 base = ImGui::GetCursorScreenPos ();
    ImGui::SetCursorScreenPos (ImVec2 (base.x + w - dw, base.y));
    if (deck_button (app, "inc_dec", "DECLINE", ImVec2 (dw, bh), theme::StatusError, Btn::Outline))
      app.incoming_answer.store (0);
    ImGui::SetCursorScreenPos (ImVec2 (base.x + w - dw - aw - du (8.0f), base.y));
    if (deck_button (app, "inc_ok", "ACCEPT", ImVec2 (aw, bh), theme::StatusOnline, Btn::Fill))
      app.incoming_answer.store (1);
    ImGui::SetCursorScreenPos (ImVec2 (base.x, base.y + bh));
  }

  ImGui::End ();
  ImGui::PopStyleVar (3);
  ImGui::PopStyleColor (2);
}

// PIN entry, shown while a dial-out is parked on on_pin_request. A real modal:
// ImGui gives it the dimmed backdrop, top-of-stack input routing and keyboard
// capture, which hand-rolled overlays in this repo have repeatedly not had.
static void
ui_pin (App & app)
{
  bool pending = app.pin_pending.load ();
  if (pending && !ImGui::IsPopupOpen ("##pin"))
    ImGui::OpenPopup ("##pin");
  if (!ImGui::IsPopupOpen ("##pin"))
    return;

  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::SetNextWindowSize (ImVec2 (du (333.0f), 0));
  ImGui::PushStyleColor (ImGuiCol_PopupBg, theme::Hex (theme::WindowBgMid, 0.99f));
  ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (0xFFFFFF, 0.12f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (du (16.0f), du (15.0f)));
  ImGui::PushStyleVar (ImGuiStyleVar_PopupRounding, du (theme::RadiusDeck));

  if (ImGui::BeginPopupModal ("##pin", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    static char buf[32] = "";
    ImDrawList * dl = ImGui::GetWindowDrawList ();
    const float w = ImGui::GetContentRegionAvail ().x;

    {
      ImVec2 at = ImGui::GetCursorScreenPos ();
      draw_label (app, dl, at, "PIN REQUIRED", theme::WhiteU32 (0.32f));
      ImGui::Dummy (ImVec2 (0, du (15.0f)));
    }
    {
      ImVec2 at = ImGui::GetCursorScreenPos ();
      dl->AddText (app.fonts.bodyBold, theme::fs (13.3f), at, theme::WhiteU32 (0.92f),
                   app.cfg.vmr.empty () ? "this conference" : app.cfg.vmr.c_str ());
      ImGui::Dummy (ImVec2 (0, theme::fs (15.0f) + du (8.0f)));
    }

    // The one place a reader stares at individual digits: bigger mono, wide
    // tracking, and an accent border so the field is obviously the subject.
    ImGui::PushFont (app.fonts.monoLg);
    const float pad_y = std::max (2.0f, (du (32.0f) - ImGui::GetFontSize ()) / 2);
    ImGui::PushStyleVar (ImGuiStyleVar_FramePadding, ImVec2 (du (10.0f), pad_y));
    ImGui::PushStyleVar (ImGuiStyleVar_FrameRounding, du (theme::RadiusControl2));
    ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor (ImGuiCol_FrameBg, theme::Hex (0xFFFFFF, theme::PanelFillRaised));
    ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (theme::AccentPrimary, 0.60f));
    if (ImGui::IsWindowAppearing ())
      ImGui::SetKeyboardFocusHere ();
    ImGui::SetNextItemWidth (w);
    bool entered = ImGui::InputText ("##pinv", buf, sizeof (buf),
                                     ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CharsDecimal |
                                       ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor (2);
    ImGui::PopStyleVar (3);
    ImGui::PopFont ();

    {
      ImGui::Dummy (ImVec2 (0, du (6.0f)));
      ImVec2 at = ImGui::GetCursorScreenPos ();
      dl->AddText (app.fonts.body, theme::fs (10.8f), at, theme::WhiteU32 (0.40f),
                   app.pin_guest_required.load () ? "This conference requires a PIN to join."
                                                  : "Enter the host PIN, or join as a guest.");
      ImGui::Dummy (ImVec2 (0, theme::fs (12.0f) + du (10.0f)));
    }

    const float bh = du (27.0f);
    const float jw = label_w (app, "JOIN", 9.5f) + du (15.0f) * 2;
    const float cw = label_w (app, "CANCEL", 9.5f) + du (15.0f) * 2;
    ImVec2 base = ImGui::GetCursorScreenPos ();
    ImGui::SetCursorScreenPos (ImVec2 (base.x + w - cw, base.y));
    bool cancel = deck_button (app, "pin_c", "CANCEL", ImVec2 (cw, bh), 0xFFFFFF, Btn::Outline);
    ImGui::SetCursorScreenPos (ImVec2 (base.x + w - cw - jw - du (8.0f), base.y));
    bool join = deck_button (app, "pin_j", "JOIN", ImVec2 (jw, bh), theme::AccentPrimary, Btn::Fill) || entered;
    ImGui::SetCursorScreenPos (ImVec2 (base.x, base.y + bh));

    if (pending && join) {
      {
        std::lock_guard<std::mutex> lock (app.pin_mutex);
        app.pin_value = buf;
      }
      buf[0] = '\0';
      app.pin_answer.store (1);
      ImGui::CloseCurrentPopup ();
    } else if (pending && cancel) {
      buf[0] = '\0';
      app.pin_answer.store (0);
      ImGui::CloseCurrentPopup ();
    } else if (!pending) {
      ImGui::CloseCurrentPopup ();
    }
    ImGui::EndPopup ();
  }

  ImGui::PopStyleVar (2);
  ImGui::PopStyleColor (2);
}

// Feed error: what failed, and the two things worth trying. Replaces a plain
// tooltip, which could not offer an action.
static void
ui_feed_error (App & app, ImVec2 win_size)
{
  // The inspected feed is the one an operator is asking about.
  if (app.inspect_feed < 0 || app.inspect_feed >= (int) app.feeds.size ())
    return;
  Feed & f = app.feeds[(size_t) app.inspect_feed];
  if (f.error.empty ())
    return;

  ImVec2 vp = ImGui::GetMainViewport ()->Pos;
  ImGui::SetNextWindowPos (ImVec2 (vp.x + win_size.x / 2, vp.y + win_size.y - du (40.0f)), ImGuiCond_Always,
                           ImVec2 (0.5f, 1.0f));
  ImGui::SetNextWindowSize (ImVec2 (du (430.0f), 0));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.99f));
  ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (theme::StatusError, 0.40f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (du (16.0f), du (14.0f)));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_WindowRounding, du (theme::RadiusDeck));
  ImGui::Begin ("##feederr", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                  ImGuiWindowFlags_NoFocusOnAppearing);

  ImDrawList * dl = ImGui::GetWindowDrawList ();
  const float w = ImGui::GetContentRegionAvail ().x;

  {
    ImVec2 at = ImGui::GetCursorScreenPos ();
    dl->AddCircleFilled (ImVec2 (at.x + du (3.0f), at.y + theme::fs (10.5f) / 2), du (3.0f),
                         theme::HexU32 (theme::StatusError));
    draw_label (app, dl, ImVec2 (at.x + du (12.0f), at.y), "FEED ERROR", theme::HexU32 (theme::StatusError));
    dl->AddText (app.fonts.label, theme::fs (9.5f),
                 ImVec2 (at.x + w - label_w (app, f.name.c_str (), 9.5f), at.y), theme::WhiteU32 (0.60f),
                 f.name.c_str ());
    ImGui::Dummy (ImVec2 (0, du (18.0f)));
  }

  auto line = [&] (const char * text, ImU32 col) {
    ImVec2 at = ImGui::GetCursorScreenPos ();
    dl->AddText (app.fonts.mono, theme::fs (10.0f), at, col, text);
    ImGui::Dummy (ImVec2 (0, theme::fs (13.0f)));
  };
  line (f.url.c_str (), theme::WhiteU32 (0.70f));
  line (f.error.c_str (), theme::HexU32 (theme::StatusError, 0.90f));
  line (app.cfg.rtsp_tcp ? "Some cameras only offer UDP. Try switching transport."
                         : "Some cameras only offer TCP. Try switching transport.",
        theme::WhiteU32 (0.35f));

  ImGui::Dummy (ImVec2 (0, du (8.0f)));
  const float bh = du (23.0f);
  const float rw = label_w (app, "RETRY", 9.5f) + du (14.0f) * 2;
  const char * swlabel = app.cfg.rtsp_tcp ? "SWITCH TO UDP" : "SWITCH TO TCP";
  const float sw = label_w (app, swlabel, 9.5f) + du (14.0f) * 2;
  ImVec2 base = ImGui::GetCursorScreenPos ();
  if (deck_button (app, "fe_retry", "RETRY", ImVec2 (rw, bh), theme::AccentPrimary, Btn::Fill)) {
    stop_feed (f);
    start_feed (f);
    if (f.connected)
      f.connected_at = ImGui::GetTime ();
  }
  ImGui::SetCursorScreenPos (ImVec2 (base.x + rw + du (8.0f), base.y));
  if (deck_button (app, "fe_udp", swlabel, ImVec2 (sw, bh), 0xFFFFFF, Btn::Outline)) {
    // Switches this feed only — the global default is a Settings decision.
    app.cfg.rtsp_tcp = !app.cfg.rtsp_tcp;
    g_cfg_rtsp_tcp = app.cfg.rtsp_tcp;
    stop_feed (f);
    start_feed (f);
    if (f.connected)
      f.connected_at = ImGui::GetTime ();
  }
  ImGui::SetCursorScreenPos (ImVec2 (base.x, base.y + bh));

  ImGui::End ();
  ImGui::PopStyleVar (3);
  ImGui::PopStyleColor (2);
}

// The send canvas: what the VMR receives, drawn to scale. Square-cornered on
// purpose — the far end receives a rectangle, and rounding it here would be the
// UI telling a small lie about the output.
static void
ui_canvas (App & app, ImVec2 size)
{
  const bool sending = app.input_open && app.conf_status.load () == PULSE_CONNECTION_STATUS_CONNECTED;

  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (du (10.0f), du (10.0f)));
  ImGui::PushStyleColor (ImGuiCol_ChildBg, theme::Hex (0xFFFFFF, theme::PanelFill));
  ImGui::PushStyleVar (ImGuiStyleVar_ChildRounding, du (theme::RadiusDeck));
  ImGui::BeginChild ("##canvas", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
  ImDrawList * dl = ImGui::GetWindowDrawList ();

  // ---- header -------------------------------------------------------------
  {
    ImVec2 at = ImGui::GetCursorScreenPos ();
    const float cw = ImGui::GetContentRegionAvail ().x;
    draw_label (app, dl, at, sending ? "PROGRAM · WHAT THE VMR RECEIVES" : "CANVAS · READY TO SEND",
                theme::WhiteU32 (theme::TextLabel));
    char spec[128];
    snprintf (spec, sizeof (spec), "%d×%d · %d fps · H.264 · %s", app.cfg.canvas_w, app.cfg.canvas_h,
              app.cfg.send_fps, app.cfg.send_as_content ? "CONTENT" : "MAIN");
    float rx = at.x + cw - mono_w (app, spec);
    draw_mono (app, dl, ImVec2 (rx, at.y - du (1.0f)), spec, theme::WhiteU32 (0.45f));

    // Capture the programme itself — what the VMR receives, composed.
    const bool rec = app.canvas_rec.active ();
    char rlabel[64];
    if (rec) {
      int d = (int) (ImGui::GetTime () - app.canvas_rec.started_at);
      snprintf (rlabel, sizeof (rlabel), "REC %d:%02d", d / 60, d % 60);
    } else {
      snprintf (rlabel, sizeof (rlabel), "RECORD CANVAS");
    }
    rx -= du (14.0f) + mono_w (app, rlabel, 9.5f);
    draw_mono (app, dl, ImVec2 (rx, at.y - du (1.0f)), rlabel,
               rec ? theme::HexU32 (theme::StatusError, 0.95f) : theme::WhiteU32 (0.40f), 9.5f);
    rx -= du (11.0f) + du (6.0f);
    if (record_dot (app, "canvasrec", rec, ImVec2 (rx, at.y - du (2.0f)), 11.0f, !g_ffmpeg.empty ())) {
      if (app.canvas_rec.busy ())
        stop_canvas_recording (app);
      else
        start_canvas_recording (app);
    }
    ImGui::SetCursorScreenPos (at);
    ImGui::Dummy (ImVec2 (0, du (14.0f) + du (theme::GapTight)));
  }

  // Fit the canvas inside what is left.
  ImVec2 avail = ImGui::GetContentRegionAvail ();
  float scale = std::min (avail.x / app.cfg.canvas_w, avail.y / app.cfg.canvas_h);
  ImVec2 origin = ImGui::GetCursorScreenPos ();
  ImVec2 c0 (origin.x + (avail.x - app.cfg.canvas_w * scale) / 2, origin.y + (avail.y - app.cfg.canvas_h * scale) / 2);
  ImVec2 c1 (c0.x + app.cfg.canvas_w * scale, c0.y + app.cfg.canvas_h * scale);

  dl->AddRectFilled (c0, c1, theme::HexU32 (theme::WindowBgStart));

  auto to_screen = [&] (float cx, float cy) { return ImVec2 (c0.x + cx * scale, c0.y + cy * scale); };

  for (int i = 0; i < (int) app.tiles.size (); i++) {
    Tile & t = app.tiles[i];
    if (t.feed < 0 || t.feed >= (int) app.feeds.size ())
      continue;
    Feed & f = app.feeds[t.feed];
    const bool stalled = feed_stalled (f);

    ImVec2 t0 = to_screen (t.x, t.y);
    ImVec2 t1 = to_screen (t.x + t.w, t.y + t.h);

    if (f.texture && f.tex_w > 0)
      dl->AddImage ((ImTextureID) (intptr_t) f.texture, t0, t1);
    else
      dl->AddRectFilled (t0, t1, theme::WhiteU32 (0.06f));
    if (stalled)
      dl->AddRectFilled (t0, t1, theme::HexU32 (theme::StatusWarn, 0.05f));

    ImGui::PushID (1000 + i);

    // Body: drag to move. Double-click: full screen.
    ImGui::SetCursorScreenPos (t0);
    ImGui::SetNextItemAllowOverlap ();
    ImGui::InvisibleButton ("##move", ImVec2 (std::max (1.0f, t1.x - t0.x), std::max (1.0f, t1.y - t0.y)));
    bool hovered = ImGui::IsItemHovered ();
    if (hovered && ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left))
      toggle_fullscreen (app, t.feed);
    if (ImGui::IsItemActive () && ImGui::IsMouseDragging (0)) {
      ImVec2 d = ImGui::GetIO ().MouseDelta;
      t.x = std::max (0.0f, std::min ((float) app.cfg.canvas_w - t.w, t.x + d.x / scale));
      t.y = std::max (0.0f, std::min ((float) app.cfg.canvas_h - t.h, t.y + d.y / scale));
      app.fullscreen_feed = -1;
    }
    if (hovered && ImGui::IsMouseReleased (ImGuiMouseButton_Left) && !ImGui::IsMouseDragging (0))
      app.inspect_feed = t.feed;

    // Resize handle, bottom-right.
    const float grip = du (16.0f);
    ImGui::SetCursorScreenPos (ImVec2 (t1.x - grip, t1.y - grip));
    ImGui::InvisibleButton ("##size", ImVec2 (grip, grip));
    bool grip_hover = ImGui::IsItemHovered () || ImGui::IsItemActive ();
    if (ImGui::IsItemActive () && ImGui::IsMouseDragging (0)) {
      ImVec2 d = ImGui::GetIO ().MouseDelta;
      float nw = std::max (160.0f, t.w + d.x / scale);
      t.w = std::min (nw, (float) app.cfg.canvas_w - t.x);
      t.h = t.w * 9.0f / 16.0f; // keep 16:9
      app.fullscreen_feed = -1;
    }

    // Grid presets are edge-to-edge, so neighbours want a divider rather than
    // a box each — a border per tile would double up to 2px on every seam.
    dl->AddRect (t0, t1, theme::WhiteU32 (theme::TileStroke), 0, 0, 1.0f);
    if (hovered || grip_hover) {
      dl->AddRect (t0, t1, theme::HexU32 (theme::AccentPrimary), 0, 0, 2.0f);
      dl->AddTriangleFilled (ImVec2 (t1.x - grip, t1.y), ImVec2 (t1.x, t1.y - grip), t1,
                             theme::HexU32 (theme::AccentPrimary));
    }
    const bool selected = app.inspect_feed == t.feed;
    if (selected)
      dl->AddRect (ImVec2 (t0.x + 1, t0.y + 1), ImVec2 (t1.x - 1, t1.y - 1), theme::HexU32 (theme::AccentPrimary), 0,
                   0, 2.0f);

    // ---- overlay ----------------------------------------------------------
    const float pad = du (7.0f);
    const float badge = du (15.0f);
    ImVec2 b0 (t0.x + pad, t0.y + pad);
    dl->AddRectFilled (b0, ImVec2 (b0.x + badge, b0.y + badge),
                       stalled ? theme::HexU32 (theme::StatusWarn) : theme::HexU32 (theme::AccentPrimary),
                       du (4.0f));
    char num[8];
    snprintf (num, sizeof (num), "%d", i + 1);
    float nw = label_w (app, num, 9.2f);
    dl->AddText (app.fonts.label, theme::fs (9.2f),
                 ImVec2 (b0.x + (badge - nw) / 2, b0.y + (badge - theme::fs (9.2f)) / 2 - du (0.5f)),
                 stalled ? theme::HexU32 (theme::WindowBgStart) : theme::WhiteU32 (1.0f), num);

    float nx = b0.x + badge + du (6.0f);
    dl->AddText (app.fonts.label, theme::fs (10.0f), ImVec2 (nx + 1, b0.y + du (2.5f) + 1),
                 theme::HexU32 (theme::WindowBgStart, 0.75f), f.name.c_str ());
    dl->AddText (app.fonts.label, theme::fs (10.0f), ImVec2 (nx, b0.y + du (2.5f)), theme::WhiteU32 (0.92f),
                 f.name.c_str ());

    if (stalled) {
      // The tile says why it is amber, where the operator is already looking.
      char chip[64];
      snprintf (chip, sizeof (chip), "LAST FRAME %.1fs", ImGui::GetTime () - f.last_frame_at);
      float cwid = mono_w (app, chip, 9.0f) + du (10.0f);
      ImVec2 k0 (nx + label_w (app, f.name.c_str (), 10.0f) + du (7.0f), b0.y + du (1.0f));
      ImVec2 k1 (k0.x + cwid, k0.y + du (13.0f));
      dl->AddRect (k0, k1, theme::HexU32 (theme::StatusWarn, 0.55f), du (3.0f), 0, 1.0f);
      draw_mono (app, dl, ImVec2 (k0.x + du (5.0f), k0.y + du (2.0f)), chip, theme::HexU32 (theme::StatusWarn), 9.0f);
    }

    if (selected)
      dl->AddText (app.fonts.label, theme::fs (8.3f), ImVec2 (t0.x + pad, t1.y - pad - theme::fs (8.3f)),
                   theme::HexU32 (theme::AccentPrimary), "SELECTED");

    if (hovered)
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
    ImGui::PopID ();
  }

  // Tally, repeated where the operator is actually looking.
  dl->AddRect (c0, c1, sending ? theme::HexU32 (theme::StatusError, 0.55f) : theme::WhiteU32 (0.14f), 0, 0, 1.0f);
  if (sending) {
    const float tick = du (12.0f), th = du (2.0f), off = du (2.0f);
    const ImU32 tc = theme::HexU32 (theme::StatusError, 0.90f);
    ImVec2 o0 (c0.x - off, c0.y - off), o1 (c1.x + off, c1.y + off);
    dl->AddRectFilled (o0, ImVec2 (o0.x + tick, o0.y + th), tc);
    dl->AddRectFilled (o0, ImVec2 (o0.x + th, o0.y + tick), tc);
    dl->AddRectFilled (ImVec2 (o1.x - tick, o0.y), ImVec2 (o1.x, o0.y + th), tc);
    dl->AddRectFilled (ImVec2 (o1.x - th, o0.y), ImVec2 (o1.x, o0.y + tick), tc);
    dl->AddRectFilled (ImVec2 (o0.x, o1.y - th), ImVec2 (o0.x + tick, o1.y), tc);
    dl->AddRectFilled (ImVec2 (o0.x, o1.y - tick), ImVec2 (o0.x + th, o1.y), tc);
    dl->AddRectFilled (ImVec2 (o1.x - tick, o1.y - th), ImVec2 (o1.x, o1.y), tc);
    dl->AddRectFilled (ImVec2 (o1.x - th, o1.y - tick), ImVec2 (o1.x, o1.y), tc);
  }

  if (app.tiles.empty ()) {
    const char * msg = "Click a source to place it · double-click for full screen";
    ImVec2 ts = app.fonts.body->CalcTextSizeA (theme::fs (11.5f), FLT_MAX, 0, msg);
    dl->AddText (app.fonts.body, theme::fs (11.5f), ImVec2 ((c0.x + c1.x - ts.x) / 2, (c0.y + c1.y) / 2),
                 theme::WhiteU32 (0.35f), msg);
  }

  ImGui::EndChild ();
  ImGui::PopStyleVar (2);
  ImGui::PopStyleColor ();
}

// ----------------------------------------------------------------------------
//  Instrument chrome — tally, header, control deck, alert bar, footer
// ----------------------------------------------------------------------------

// True when the canvas is actually reaching the far end. This one predicate
// drives the tally strip, the header readout and the canvas ticks together —
// one state shown in three places, which is the point of the treatment.
static bool
is_on_air (const App & app)
{
  return app.input_open && app.conf_status.load () == PULSE_CONNECTION_STATUS_CONNECTED;
}

// A 3px strip along the very top of the window, drawn outside the shell's
// padding so it touches the edge. The strongest "we are live" signal there is.
static void
ui_tally (App & app, ImGuiViewport * vp)
{
  static float lit = 0.0f;
  const float target = is_on_air (app) ? 1.0f : 0.0f;
  // ~150ms fade, never a blink.
  const float step = ImGui::GetIO ().DeltaTime / 0.15f;
  lit += std::max (-step, std::min (step, target - lit));

  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 p0 (vp->Pos.x, vp->Pos.y);
  ImVec2 p1 (vp->Pos.x + vp->Size.x, vp->Pos.y + theme::TallyStrip); // a hairline is a hairline: not scaled
  dl->AddRectFilled (p0, p1, theme::WhiteU32 (0.07f));
  if (lit > 0.001f)
    dl->AddRectFilled (p0, p1, theme::HexU32 (theme::StatusError, lit));
}

// Identity · state · registration · clock.
static void
ui_header (App & app, float width)
{
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  const float h = du (22.0f);
  ImVec2 at = ImGui::GetCursorScreenPos ();
  const float cy = at.y + h / 2;
  float x = at.x;

  // App mark: a filled rounded square. The design has no icon set at all.
  const float mark = du (16.0f);
  dl->AddRectFilled (ImVec2 (x, cy - mark / 2), ImVec2 (x + mark, cy + mark / 2),
                     theme::HexU32 (theme::AccentPrimary), du (4.0f));
  x += mark + du (12.0f);

  dl->AddText (app.fonts.label, theme::fs (10.5f), ImVec2 (x, cy - theme::fs (10.5f) / 2 - du (1.0f)),
               theme::WhiteU32 (theme::TextPrimary), "UAV WALL");
  x += label_w (app, "UAV WALL") + du (12.0f);

  auto divider = [&] () {
    dl->AddRectFilled (ImVec2 (x, cy - du (7.0f)), ImVec2 (x + 1, cy + du (7.0f)), theme::WhiteU32 (0.12f));
    x += 1 + du (12.0f);
  };
  divider ();

  const bool live = is_on_air (app);
  dl->AddCircleFilled (ImVec2 (x + du (3.0f), cy), du (3.0f),
                       live ? theme::HexU32 (theme::StatusError) : theme::WhiteU32 (0.22f));
  x += du (6.0f) + du (7.0f);

  const char * word = live ? "ON AIR" : "NOT SENDING";
  dl->AddText (app.fonts.label, theme::fs (9.5f), ImVec2 (x, cy - theme::fs (9.5f) / 2 - du (1.0f)),
               live ? theme::HexU32 (theme::StatusError) : theme::WhiteU32 (0.45f), word);
  x += label_w (app, word, 9.5f) + du (10.0f);

  char detail[256];
  if (live)
    snprintf (detail, sizeof (detail), "%s → %s%s", app.cfg.send_as_content ? "CONTENT" : "MAIN VIDEO",
              app.cfg.vmr.c_str (), app.cfg.send_as_content && app.floor_taken ? " · floor taken" : "");
  else if (app.call_started)
    snprintf (detail, sizeof (detail), "connecting to %s", app.cfg.vmr.c_str ());
  else
    snprintf (detail, sizeof (detail), "canvas composited on demand");
  draw_mono (app, dl, ImVec2 (x, cy - theme::fs (10.5f) / 2), detail, theme::WhiteU32 (0.62f));

  // ---- right-hand cluster, laid out from the edge backwards ---------------
  char clock[32];
  std::time_t now = std::time (nullptr);
  std::tm g{};
#if defined(_WIN32)
  gmtime_s (&g, &now);
#else
  gmtime_r (&now, &g);
#endif
  snprintf (clock, sizeof (clock), "%02d:%02d:%02dZ", g.tm_hour, g.tm_min, g.tm_sec);

  float rx = at.x + width;
  rx -= mono_w (app, clock);
  draw_mono (app, dl, ImVec2 (rx, cy - theme::fs (10.5f) / 2), clock, theme::WhiteU32 (0.70f));
  rx -= du (12.0f);
  dl->AddRectFilled (ImVec2 (rx - 1, cy - du (7.0f)), ImVec2 (rx, cy + du (7.0f)), theme::WhiteU32 (0.12f));
  rx -= du (12.0f) + 1;

  // Registration is omitted entirely when the wall was never configured to
  // register — an absent feature should not advertise itself as broken.
  if (!app.cfg.reg_host.empty ()) {
    const bool reg = app.reg_status.load () == PULSE_CONNECTION_STATUS_CONNECTED;
    char rbuf[256];
    if (reg)
      snprintf (rbuf, sizeof (rbuf), "REGISTERED %s", app.cfg.reg_alias.c_str ());
    else
      snprintf (rbuf, sizeof (rbuf), "NOT REGISTERED");
    rx -= mono_w (app, rbuf);
    draw_mono (app, dl, ImVec2 (rx, cy - theme::fs (10.5f) / 2), rbuf,
               reg ? theme::WhiteU32 (0.50f) : theme::WhiteU32 (0.32f));
    rx -= du (7.0f) + du (6.0f);
    if (reg)
      dl->AddCircleFilled (ImVec2 (rx + du (3.0f), cy), du (3.0f), theme::HexU32 (theme::StatusOnline));
  }

  ImGui::Dummy (ImVec2 (width, h));
}

// The control deck: four labelled groups in one panel. Grouping is most of
// what separates this from the undifferentiated row of pills it replaces.
static void
ui_deck (App & app, float width, char * vmr_buf, size_t vmr_sz, char * pin_buf, size_t pin_sz)
{
  const float h = du (58.0f);
  ImVec2 at = ImGui::GetCursorScreenPos ();
  ImDrawList * dl = ImGui::GetWindowDrawList ();

  dl->AddRectFilled (at, ImVec2 (at.x + width, at.y + h), theme::WhiteU32 (theme::PanelFill),
                     du (theme::RadiusDeck));
  dl->AddRect (at, ImVec2 (at.x + width, at.y + h), theme::WhiteU32 (theme::PanelStroke), du (theme::RadiusDeck), 0,
               1.0f);

  const float label_y = at.y + du (theme::DeckPadY);
  const float ctrl_y = label_y + du (11.0f) + du (theme::GapTight);
  const float ch = du (theme::ControlH);
  float x = at.x + du (theme::GroupPadX);

  auto group_label = [&] (const char * text) { draw_label (app, dl, ImVec2 (x, label_y), text, theme::WhiteU32 (theme::TextLabel)); };
  auto divider = [&] () {
    x += du (theme::GroupPadX);
    dl->AddRectFilled (ImVec2 (x, at.y + du (2.0f)), ImVec2 (x + 1, at.y + h - du (2.0f)),
                       theme::WhiteU32 (theme::PanelStroke));
    x += 1 + du (theme::GroupPadX);
  };

  // ---- DESTINATION --------------------------------------------------------
  group_label ("DESTINATION");
  {
    const bool registered = app.reg_status.load () == PULSE_CONNECTION_STATUS_CONNECTED;

    ImGui::PushFont (app.fonts.mono);
    const float pad_y = std::max (2.0f, (ch - ImGui::GetFontSize ()) / 2);
    ImGui::PushStyleVar (ImGuiStyleVar_FramePadding, ImVec2 (du (9.0f), pad_y));
    ImGui::PushStyleVar (ImGuiStyleVar_FrameRounding, du (theme::RadiusControl2));
    ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor (ImGuiCol_FrameBg, theme::Hex (0xFFFFFF, theme::PanelFillRaised));
    ImGui::PushStyleColor (ImGuiCol_Border, theme::Hex (0xFFFFFF, theme::ControlStroke));

    ImGui::SetCursorScreenPos (ImVec2 (x, ctrl_y));
    ImGui::SetNextItemWidth (du (208.0f));
    if (ImGui::InputTextWithHint ("##vmr", registered ? "vmr@server — or search" : "vmr@server", vmr_buf, vmr_sz))
      app.cfg.vmr = vmr_buf;

    ImVec2 f_min = ImGui::GetItemRectMin (), f_max = ImGui::GetItemRectMax ();
    const bool field_active = ImGui::IsItemActive ();
    if (registered)
      refresh_vmr_hits (app, vmr_buf);

    ImGui::SetCursorScreenPos (ImVec2 (x + du (208.0f) + du (theme::GapTight), ctrl_y));
    ImGui::SetNextItemWidth (du (62.0f));
    if (ImGui::InputTextWithHint ("##pin", "PIN", pin_buf, pin_sz, ImGuiInputTextFlags_Password))
      app.cfg.pin = pin_buf;

    ImGui::PopStyleColor (2);
    ImGui::PopStyleVar (3);
    ImGui::PopFont ();

    // Directory search results. The list is kept up while the field is active
    // OR while the list itself was hovered last frame — never gated on the
    // field alone, or the click that picks a row unfocuses it first and the
    // list vanishes before the release lands.
    bool keep_list = field_active || app.vmr_popup_hovered;
    if (registered && keep_list && !app.vmr_hits.empty ()) {
      ImGui::SetNextWindowPos (ImVec2 (f_min.x, f_max.y + du (4.0f)));
      ImGui::SetNextWindowSize (ImVec2 (f_max.x - f_min.x, 0));
      ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.98f));
      ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (du (6.0f), du (6.0f)));
      ImGui::Begin ("##vmrhits", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                      ImGuiWindowFlags_NoFocusOnAppearing);
      app.vmr_popup_hovered =
        ImGui::IsWindowHovered (ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows);

      for (size_t i = 0; i < app.vmr_hits.size (); i++) {
        const App::AliasHit & hit = app.vmr_hits[i];
        ImGui::PushID ((int) i);
        std::string row = hit.alias;
        if (!hit.description.empty ())
          row += "   " + hit.description;
        if (ImGui::Selectable (row.c_str ())) {
          snprintf (vmr_buf, vmr_sz, "%s", hit.alias.c_str ());
          app.cfg.vmr = hit.alias;
          app.vmr_last_query = hit.alias;
          app.vmr_hits.clear ();
          app.vmr_popup_hovered = false;
          set_status (app, "Selected " + hit.alias);
        }
        ImGui::SameLine ();
        ImGui::TextDisabled ("%s", hit.is_device ? "device" : "conference");
        ImGui::PopID ();
      }
      ImGui::End ();
      ImGui::PopStyleVar ();
      ImGui::PopStyleColor ();
    } else {
      app.vmr_popup_hovered = false;
    }

    x += du (208.0f) + du (theme::GapTight) + du (62.0f) + du (theme::GapTight);

    // Primary action — the only coloured fill in the deck.
    const int cstat = app.conf_status.load ();
    ImGui::SetCursorScreenPos (ImVec2 (x, ctrl_y));
    if (!app.call_started) {
      const float bw = label_w (app, "SEND TO VMR", 9.5f) + du (12.0f) * 2 + du (12.0f);
      if (deck_button (app, "send", "SEND TO VMR", ImVec2 (bw, ch), theme::AccentPrimary, Btn::Fill, true, 1))
        conf_connect (app);
      x += bw;
    } else if (cstat != PULSE_CONNECTION_STATUS_CONNECTED) {
      const float bw = label_w (app, "CONNECTING…", 9.5f) + du (12.0f) * 2;
      deck_button (app, "conn", "CONNECTING…", ImVec2 (bw, ch), theme::AccentPrimary, Btn::Fill, false);
      x += bw;
    } else {
      const float bw = label_w (app, "STOP SENDING", 9.5f) + du (12.0f) * 2 + du (13.0f);
      if (deck_button (app, "stop", "STOP SENDING", ImVec2 (bw, ch), theme::StatusError, Btn::Fill, true, 2))
        conf_disconnect (app);
      x += bw;
    }
  }
  divider ();

  // ---- LAYOUT -------------------------------------------------------------
  group_label ("LAYOUT");
  {
    static const char * presets[] = {"1-UP", "2×2", "3×3", "PIP"};
    // The active cell is whichever preset the current tile geometry matches;
    // a hand-dragged layout matches none, and none lights up.
    int active = -1;
    if (!app.tiles.empty ()) {
      std::vector<int> feeds = placed_feeds (app);
      auto matches = [&] (const std::vector<Tile> & cand) {
        if (cand.size () != app.tiles.size ())
          return false;
        for (size_t i = 0; i < cand.size (); i++)
          if (cand[i].feed != app.tiles[i].feed || std::fabs (cand[i].x - app.tiles[i].x) > 1.0f ||
              std::fabs (cand[i].y - app.tiles[i].y) > 1.0f || std::fabs (cand[i].w - app.tiles[i].w) > 1.0f)
            return false;
        return true;
      };
      if (matches (grid_tiles (app.cfg, feeds, 1, 1)))
        active = 0;
      else if (matches (grid_tiles (app.cfg, feeds, 2, 2)))
        active = 1;
      else if (matches (grid_tiles (app.cfg, feeds, 3, 3)))
        active = 2;
      else if (matches (pip_tiles (app.cfg, feeds)))
        active = 3;
    }

    ImGui::SetCursorScreenPos (ImVec2 (x, ctrl_y));
    int hit = segmented (app, "layout", presets, 4, active, du (48.0f), ch);
    if (hit == 0)
      apply_grid (app, 1, 1);
    else if (hit == 1)
      apply_grid (app, 2, 2);
    else if (hit == 2)
      apply_grid (app, 3, 3);
    else if (hit == 3)
      apply_pip (app);
    x += du (48.0f) * 4 + du (theme::GapTight);

    // ---- saved slots ------------------------------------------------------
    const float saved_w = label_w (app, "SAVED", 8.3f);
    const float chip_w = du (18.0f), chip_h = du (17.0f);
    const float slots_w = du (9.0f) + saved_w + du (7.0f) + chip_w * 3 + du (4.0f) * 2 + du (9.0f);
    ImVec2 s0 (x, ctrl_y), s1 (x + slots_w, ctrl_y + ch);
    dl->AddRectFilled (s0, s1, theme::WhiteU32 (0.06f), du (theme::RadiusControl2));
    dl->AddRect (s0, s1, theme::WhiteU32 (theme::ControlStroke), du (theme::RadiusControl2), 0, 1.0f);
    dl->AddText (app.fonts.label, theme::fs (8.3f), ImVec2 (s0.x + du (9.0f), ctrl_y + (ch - theme::fs (8.3f)) / 2),
                 theme::WhiteU32 (0.30f), "SAVED");

    float chx = s0.x + du (9.0f) + saved_w + du (7.0f);
    const std::string current = serialise_tiles (app.tiles);
    for (int i = 0; i < 3; i++) {
      const std::string & slot = preset_slot (app, i);
      const bool occupied = !slot.empty ();
      const bool is_current = occupied && slot == current;
      ImVec2 c0 (chx, ctrl_y + (ch - chip_h) / 2), c1 (c0.x + chip_w, c0.y + chip_h);

      ImGui::PushID (3100 + i);
      ImGui::SetCursorScreenPos (c0);
      ImGui::InvisibleButton ("##slot", ImVec2 (chip_w, chip_h), ImGuiButtonFlags_MouseButtonLeft |
                                                                   ImGuiButtonFlags_MouseButtonRight);
      const bool hov = ImGui::IsItemHovered ();

      // Click recalls; press-and-hold (or right-click) stores. Held state is
      // tracked on the app so the chip can show it filling.
      if (hov && ImGui::IsMouseClicked (ImGuiMouseButton_Left)) {
        app.preset_held = i;
        app.preset_held_since = ImGui::GetTime ();
      }
      bool stored = false;
      if (hov && ImGui::IsMouseClicked (ImGuiMouseButton_Right)) {
        preset_slot (app, i) = current;
        save_config (app);
        stored = true;
        set_status (app, std::string ("Stored layout ") + (char) ('A' + i));
      }
      if (app.preset_held == i && ImGui::IsMouseDown (ImGuiMouseButton_Left) &&
          ImGui::GetTime () - app.preset_held_since > 0.5) {
        preset_slot (app, i) = current;
        save_config (app);
        stored = true;
        app.preset_held = -1;
        set_status (app, std::string ("Stored layout ") + (char) ('A' + i));
      }
      if (ImGui::IsMouseReleased (ImGuiMouseButton_Left) && app.preset_held == i) {
        app.preset_held = -1;
        if (hov && !stored && occupied) {
          app.tiles = parse_tiles (slot, (int) app.feeds.size ());
          app.fullscreen_feed = -1;
          set_status (app, std::string ("Recalled layout ") + (char) ('A' + i));
        }
      }

      ImU32 fill = is_current ? theme::HexU32 (theme::AccentPrimary, 0.92f) : theme::WhiteU32 (0.09f);
      ImU32 txt = is_current  ? theme::WhiteU32 (1.0f)
                  : occupied ? theme::WhiteU32 (0.60f)
                             : theme::WhiteU32 (0.28f);
      dl->AddRectFilled (c0, c1, fill, du (theme::RadiusChip));
      if (hov)
        dl->AddRect (c0, c1, theme::HexU32 (theme::AccentPrimary, 0.6f), du (theme::RadiusChip), 0, 1.0f);
      const char letter[2] = {(char) ('A' + i), 0};
      float lw = label_w (app, letter, 9.2f);
      dl->AddText (app.fonts.label, theme::fs (9.2f),
                   ImVec2 (c0.x + (chip_w - lw) / 2, c0.y + (chip_h - theme::fs (9.2f)) / 2 - du (0.5f)), txt,
                   letter);
      if (hov)
        ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
      if (hov)
        ImGui::SetTooltip ("%s", occupied ? "click: recall · hold or right-click: overwrite"
                                          : "hold or right-click: store the current layout");
      ImGui::PopID ();
      chx += chip_w + du (4.0f);
    }
    x += slots_w + du (theme::GapTight);

    const float clear_w = label_w (app, "CLEAR", 9.5f) + du (12.0f) * 2;
    ImGui::SetCursorScreenPos (ImVec2 (x, ctrl_y));
    if (deck_button (app, "clear", "CLEAR", ImVec2 (clear_w, ch), theme::StatusError, Btn::Outline)) {
      app.tiles.clear ();
      app.fullscreen_feed = -1;
    }
    x += clear_w;
  }
  divider ();

  // ---- SOURCES ------------------------------------------------------------
  group_label ("SOURCES");
  {
    int live = 0;
    bool any_off = false;
    for (const Feed & f : app.feeds) {
      if (f.connected)
        live++;
      else
        any_off = true;
    }

    const char * lbl = any_off ? "CONNECT ALL" : "DISCONNECT ALL";
    const float bw = label_w (app, lbl, 9.5f) + du (12.0f) * 2;
    ImGui::SetCursorScreenPos (ImVec2 (x, ctrl_y));
    // Constructive gets a fill, destructive only an outline — the quieter of
    // the two is the one that throws work away.
    if (deck_button (app, "connall", lbl, ImVec2 (bw, ch), any_off ? theme::StatusOnline : theme::StatusError,
                     any_off ? Btn::Tinted : Btn::Outline)) {
      for (Feed & f : app.feeds) {
        if (any_off) {
          start_feed (f);
          if (f.connected)
            f.connected_at = ImGui::GetTime ();
        } else {
          stop_feed (f);
        }
      }
      if (!any_off) {
        app.tiles.clear ();
        app.fullscreen_feed = -1;
      }
    }
    x += bw + du (theme::GapTight) + du (3.0f);

    char tally[48];
    snprintf (tally, sizeof (tally), "%d/%d live", live, (int) app.feeds.size ());
    draw_mono (app, dl, ImVec2 (x, ctrl_y + (ch - theme::fs (10.5f)) / 2), tally, theme::WhiteU32 (0.45f));
    x += mono_w (app, tally);
  }

  // ---- VIEW (right-aligned) -----------------------------------------------
  {
    const float sw = label_w (app, "STATS", 9.5f) + du (12.0f) * 2;
    const float gw = label_w (app, "SETTINGS", 9.5f) + du (12.0f) * 2;
    float rx = at.x + width - du (theme::GroupPadX) - gw - du (theme::GapTight) - sw;
    draw_label (app, dl, ImVec2 (rx, label_y), "VIEW", theme::WhiteU32 (theme::TextLabel));

    ImGui::SetCursorScreenPos (ImVec2 (rx, ctrl_y));
    if (deck_button (app, "stats", "STATS", ImVec2 (sw, ch), theme::AccentPrimary,
                     app.cfg.show_stats ? Btn::Fill : Btn::Outline))
      app.cfg.show_stats = !app.cfg.show_stats;
    rx += sw + du (theme::GapTight);
    ImGui::SetCursorScreenPos (ImVec2 (rx, ctrl_y));
    if (deck_button (app, "settings", "SETTINGS", ImVec2 (gw, ch), theme::AccentPrimary,
                     app.show_settings ? Btn::Fill : Btn::Outline))
      app.show_settings = !app.show_settings;
  }

  ImGui::SetCursorScreenPos (at);
  ImGui::Dummy (ImVec2 (width, h));
}

// Amber bar between the deck and the body: a feed is connected but has stopped
// delivering. Returns the height it consumed, so the body below can be sized.
static float
ui_alert (App & app, float width)
{
  // Worst offender = the stalled feed with the oldest frame.
  int worst = -1, count = 0;
  double worst_age = 0;
  for (int i = 0; i < (int) app.feeds.size (); i++) {
    if (!feed_stalled (app.feeds[(size_t) i]))
      continue;
    count++;
    double age = ImGui::GetTime () - app.feeds[(size_t) i].last_frame_at;
    if (age > worst_age) {
      worst_age = age;
      worst = i;
    }
  }
  app.alert_feed = worst;

  if (worst < 0) {
    // Everything recovered: clear the dismissal so a fresh stall re-raises,
    // and re-arm the cue for the next episode.
    app.alert_dismissed = false;
    app.alert_dismissed_feed = -1;
    app.alert_rang_at = 0.0;
    return 0.0f;
  }

  // One cue per stall episode — armed by the recovery above, so a feed that
  // stays down does not ring every frame.
  if (!app.cfg.alerts_muted && app.alert_rang_at == 0.0) {
    play_ring ();
    app.alert_rang_at = ImGui::GetTime ();
  }

  if (app.alert_dismissed && app.alert_dismissed_feed == worst)
    return 0.0f;
  app.alert_dismissed = false;

  const float h = du (27.0f);
  ImVec2 at = ImGui::GetCursorScreenPos ();
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 p1 (at.x + width, at.y + h);
  dl->AddRectFilled (at, p1, theme::HexU32 (theme::StatusWarn, 0.13f), du (theme::RadiusControl2));
  dl->AddRect (at, p1, theme::HexU32 (theme::StatusWarn, 0.40f), du (theme::RadiusControl2), 0, 1.0f);

  const float cy = at.y + h / 2;
  float x = at.x + du (10.0f);
  dl->AddCircleFilled (ImVec2 (x + du (3.5f), cy), du (3.5f), theme::HexU32 (theme::StatusWarn));
  x += du (7.0f) + du (8.0f);
  dl->AddText (app.fonts.label, theme::fs (9.5f), ImVec2 (x, cy - theme::fs (9.5f) / 2 - du (1.0f)),
               theme::HexU32 (theme::StatusWarn), "FEED LOSS");
  x += label_w (app, "FEED LOSS", 9.5f) + du (8.0f);

  char msg[256];
  const Feed & wf = app.feeds[(size_t) worst];
  if (count > 1)
    snprintf (msg, sizeof (msg), "%s — no frames for %.1fs, still connected  and %d other%s", wf.name.c_str (),
              worst_age, count - 1, count == 2 ? "" : "s");
  else
    snprintf (msg, sizeof (msg), "%s — no frames for %.1fs, still connected", wf.name.c_str (), worst_age);
  draw_mono (app, dl, ImVec2 (x, cy - theme::fs (10.5f) / 2), msg, theme::WhiteU32 (0.70f));

  const float mw = label_w (app, "MUTE ALERTS", 9.2f) + du (16.0f);
  const float dw = label_w (app, "DISMISS", 9.2f) + du (16.0f);
  float rx = at.x + width - du (10.0f) - dw;
  ImGui::SetCursorScreenPos (ImVec2 (rx, cy - h / 2));
  if (deck_button (app, "al_dis", "DISMISS", ImVec2 (dw, h), 0xFFFFFF, Btn::Ghost)) {
    app.alert_dismissed = true;
    app.alert_dismissed_feed = worst;
  }
  rx -= mw;
  ImGui::SetCursorScreenPos (ImVec2 (rx, cy - h / 2));
  if (deck_button (app, "al_mute", app.cfg.alerts_muted ? "ALERTS MUTED" : "MUTE ALERTS", ImVec2 (mw, h), 0xFFFFFF,
                   Btn::Ghost))
    app.cfg.alerts_muted = !app.cfg.alerts_muted;

  ImGui::SetCursorScreenPos (at);
  ImGui::Dummy (ImVec2 (width, h));
  return h;
}

// Discrete metric cells, rather than a run of concatenated text.
static void
ui_footer (App & app, float width)
{
  const float h = du (theme::FooterH);
  ImVec2 at = ImGui::GetCursorScreenPos ();
  ImDrawList * dl = ImGui::GetWindowDrawList ();
  ImVec2 p1 (at.x + width, at.y + h);
  dl->AddRectFilled (at, p1, theme::WhiteU32 (theme::PanelFill), du (theme::RadiusFooter));
  dl->AddRect (at, p1, theme::WhiteU32 (theme::PanelStroke), du (theme::RadiusFooter), 0, 1.0f);

  const float cy = at.y + h / 2;
  float x = at.x;
  bool first = true;

  auto cell = [&] (const char * label, const char * value, ImU32 vcol, const char * suffix = nullptr) {
    if (!first)
      dl->AddRectFilled (ImVec2 (x, at.y + du (8.0f)), ImVec2 (x + 1, p1.y - du (8.0f)), theme::WhiteU32 (0.07f));
    x += (first ? 0.0f : 1.0f) + du (13.0f);
    first = false;
    dl->AddText (app.fonts.microCap, theme::fs (8.5f), ImVec2 (x, cy - theme::fs (8.5f) / 2 - du (0.5f)),
                 theme::WhiteU32 (0.30f), label);
    x += app.fonts.microCap->CalcTextSizeA (theme::fs (8.5f), FLT_MAX, 0, label).x + du (7.0f);
    draw_mono (app, dl, ImVec2 (x, cy - theme::fs (10.5f) / 2), value, vcol);
    x += mono_w (app, value);
    if (suffix) {
      x += du (4.0f);
      draw_mono (app, dl, ImVec2 (x, cy - theme::fs (9.5f) / 2), suffix, theme::WhiteU32 (0.30f), 9.5f);
      x += mono_w (app, suffix, 9.5f);
    }
    x += du (13.0f);
  };

  char buf[64], buf2[64];
  const bool sending = app.input_open;
  const ImU32 idle_col = theme::WhiteU32 (0.35f);

  if (app.cfg.show_stats) {
    snprintf (buf, sizeof (buf), "%.0f%%", app.proc_cpu_pct);
    cell ("CPU", buf, theme::WhiteU32 (0.80f));
    snprintf (buf, sizeof (buf), "%.0f MB", app.proc_rss_mb);
    cell ("RSS", buf, theme::WhiteU32 (0.80f));

    if (sending) {
      snprintf (buf, sizeof (buf), "%.1f ms", app.composite_ms);
      snprintf (buf2, sizeof (buf2), "/ %.0f budget", 1000.0 / std::max (1, app.cfg.send_fps));
      cell ("COMPOSITE", buf, theme::WhiteU32 (0.80f), buf2);
    } else {
      cell ("COMPOSITE", "idle", idle_col);
    }

    if (app.tx_valid) {
      snprintf (buf, sizeof (buf), "%.1f Mbps", app.tx_bitrate / 1e6);
      cell ("TX", buf, theme::HexU32 (theme::AccentPrimary, 0.95f));
      snprintf (buf, sizeof (buf), "%.1f%%", app.tx_loss_pct);
      cell ("LOSS", buf, theme::WhiteU32 (0.80f));
      snprintf (buf, sizeof (buf), "%.0f ms", app.tx_rtt_ms);
      cell ("RTT", buf, theme::WhiteU32 (0.80f));
    } else {
      cell ("TX", "—", idle_col);
      cell ("LOSS", "—", idle_col);
      cell ("RTT", "—", idle_col);
    }
  }

  // Recording is a state the operator must not lose track of, so it earns a
  // cell of its own — but only while it is happening.
  {
    const int n = recording_count (app);
    if (n > 0) {
      char v[64];
      const Recorder & lead = app.canvas_rec.active () ? app.canvas_rec : app.feeds[0].rec;
      double since = lead.started_at;
      for (const Feed & f : app.feeds)
        if (f.rec.active () && (since <= 0 || f.rec.started_at < since))
          since = f.rec.started_at;
      if (app.canvas_rec.active () && app.canvas_rec.started_at < since)
        since = app.canvas_rec.started_at;
      int d = (int) (ImGui::GetTime () - since);
      if (n == 1)
        snprintf (v, sizeof (v), "%d:%02d:%02d", d / 3600, (d / 60) % 60, d % 60);
      else
        snprintf (v, sizeof (v), "%d × %d:%02d:%02d", n, d / 3600, (d / 60) % 60, d % 60);
      cell ("REC", v, theme::HexU32 (theme::StatusError, 0.95f));
    }
  }

  // Right-aligned: today's status line, plus the call duration once up.
  std::string s = get_status (app);
  if (app.call_started && app.call_connected_at > 0) {
    int d = (int) (ImGui::GetTime () - app.call_connected_at);
    char dur[32];
    snprintf (dur, sizeof (dur), "%d:%02d:%02d", d / 3600, (d / 60) % 60, d % 60);
    s = s.empty () ? std::string (dur) : s + "  ·  " + dur;
  }
  if (!s.empty ()) {
    float sw = mono_w (app, s.c_str ());
    const float room = width - (x - at.x) - du (13.0f);
    while (!s.empty () && sw > room) {
      s.erase (s.begin ());
      sw = mono_w (app, s.c_str ());
    }
    draw_mono (app, dl, ImVec2 (p1.x - du (13.0f) - sw, cy - theme::fs (10.5f) / 2), s.c_str (),
               theme::WhiteU32 (0.35f));
  }

  ImGui::SetCursorScreenPos (at);
  ImGui::Dummy (ImVec2 (width, h));
}

// ----------------------------------------------------------------------------
//  main
// ----------------------------------------------------------------------------

// Registration requires a handle built by pulse_new_with_internal_sso_handling()
// on macOS/Linux — plain pulse_new() fails with "missing sso callbacks" even for
// password auth. We never start an SSO flow (this returns -1, "no provider
// chosen"), so no pexip-auth:// callback is ever expected and no .app bundle is
// needed. See the README's Planned section.
static int
on_sso_select (PulseSSOProviderList *, void *)
{
  return -1;
}

int
main (int argc, char ** argv)
{
  if (!glfwInit ()) {
    std::fprintf (stderr, "glfwInit failed\n");
    return 1;
  }
  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  GLFWwindow * window = glfwCreateWindow (1440, 900, "UAV Wall — Pexip", nullptr, nullptr);
  if (!window) {
    std::fprintf (stderr, "glfwCreateWindow failed\n");
    return 1;
  }
  glfwMakeContextCurrent (window);
  glfwSwapInterval (1);

  IMGUI_CHECKVERSION ();
  ImGui::CreateContext ();
  ImGuiIO & io = ImGui::GetIO ();
  io.IniFilename = nullptr;

  App app;

  // --bench [secs] · --feeds N · --grid CxR
  int forced_feeds = 0;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&] (double def) { return (i + 1 < argc && argv[i + 1][0] != '-') ? atof (argv[++i]) : def; };
    if (a == "--bench") {
      app.bench = true;
      app.bench_force_composite = true;
      app.bench_seconds = next (20.0);
    } else if (a == "--feeds") {
      forced_feeds = (int) next (4);
    } else if (a == "--grid" && i + 1 < argc) {
      sscanf (argv[++i], "%dx%d", &app.bench_grid_c, &app.bench_grid_r);
    }
  }

  if (forced_feeds > 0) {
    static const char * kCallsigns[] = {"HAWKEYE 21", "KESTREL 33", "NOMAD 14",   "OSPREY 12",
                                        "SENTINEL 07", "TALON 26",  "MERLIN 18",  "VIGIL 05",
                                        "LANCER 41",  "GOSHAWK 09", "PEREGRINE 52", "WARDEN 63"};
    for (int i = 1; i <= forced_feeds; i++) {
      Feed f;
      f.name = kCallsigns[(i - 1) % 12];
      f.url = "rtsp://127.0.0.1:8554/uav" + std::to_string (i);
      app.feeds.push_back (std::move (f));
    }
  } else {
    load_config (app);
  }

  // Resolved once, for both normal and bench starts.
  find_ffmpeg ();

  g_cfg_rtsp_tcp = app.cfg.rtsp_tcp;
  g_cfg_rtsp_latency_ms = app.cfg.rtsp_latency_ms;

  // Created before any feed instance and freed last: this is both the
  // conference instance and the one that keeps Pulse's global state alive.
#if defined(HOST_WINDOWS)
  app.conf = pulse_new ();
#else
  app.conf = pulse_new_with_internal_sso_handling (argc, (const char **) argv, on_sso_select, &app);
#endif
  if (!app.conf) {
    std::fprintf (stderr, "[uavwall] pulse_new() failed — cannot continue\n");
    return 1;
  }
  pulse_options_set_self_view_window_handle (app.conf, nullptr);
  pulse_options_set_remote_video_window_handle (app.conf, nullptr);
  pulse_options_set_presentation_video_window_handle (app.conf, nullptr);
  pulse_options_set_application_user_agent_string (app.conf, "uavwall/0.1");
  {
    PulseConferenceStatusCallbackConfig scb{on_conf_status, &app};
    pulse_options_set_conference_state_callback (app.conf, &scb);
    PulseRegistrationStatusCallbackConfig rcb{on_reg_status, &app};
    pulse_options_set_registration_state_callback (app.conf, &rcb);
    PulsePinCodeRequestCallbackConfig pcb{};
    pcb.func = on_pin_request;
    pcb.user_context = &app;
    pulse_options_set_pin_code_request_callbacks (app.conf, &pcb);
  }

  if (app.cfg.reg_auto)
    start_register (app);

  float xscale = 1.0f, yscale = 1.0f;
  glfwGetWindowContentScale (window, &xscale, &yscale);
  theme::scale = app.cfg.ui_scale; // must precede font sizing
  app.fonts = theme::LoadFonts (io, UAVWALL_ASSET_DIR "/fonts", xscale);
  theme::Apply ();

  ImGui_ImplGlfw_InitForOpenGL (window, true);
  ImGui_ImplOpenGL3_Init ("#version 150");

  if (app.cfg.autoconnect && !app.bench) {
    for (Feed & f : app.feeds)
      start_feed (f);
  }

  if (app.bench) {
    for (Feed & f : app.feeds)
      start_feed (f);
    if (app.bench_grid_c > 0)
      apply_grid (app, app.bench_grid_c, app.bench_grid_r);
    else
      apply_grid (app, 2, 2);
    app.bench_started = glfwGetTime ();
    std::fprintf (stderr, "[bench] %zu feeds, grid %dx%d, %.0fs …\n", app.feeds.size (),
                  app.bench_grid_c ? app.bench_grid_c : 2, app.bench_grid_r ? app.bench_grid_r : 2,
                  app.bench_seconds);
  }

  while (!glfwWindowShouldClose (window)) {
    glfwPollEvents ();


    // Only feeds that are both placed on the canvas and being sent somewhere
    // need a CPU-side copy.
    {
      bool sending = app.input_open || app.bench_force_composite || app.canvas_rec.active ();
      for (int i = 0; i < (int) app.feeds.size (); i++)
        pump_feed (app.feeds[i], sending && feed_is_placed (app, i));
    }

    // Compositing a 1920x1080 frame on the CPU is the most expensive thing
    // this app does, and the canvas preview draws from the feed textures
    // directly — so only build it when there is a conference to push it to,
    // and only at the 30fps the send session is configured for.
    if (app.input_open || app.bench_force_composite || app.canvas_rec.active ()) {
      double now = glfwGetTime ();
      static double last_push = 0.0;
      if (now - last_push >= 1.0 / std::max (1, app.cfg.send_fps)) {
        double t0 = glfwGetTime ();
        composite (app);
        app.composite_ms = (glfwGetTime () - t0) * 1000.0;
        if (app.input_open)
          push_canvas (app);

        // Same composite to the encoder, so the recording is exactly what the
        // far end receives. Emitted on its own fixed cadence: if the loop ran
        // slow, the previous picture is repeated to fill the gap rather than
        // the file coming out short and playing fast.
        if (app.canvas_rec.active ()) {
          const double period = 1.0 / std::max (1, app.cfg.send_fps);
          if (app.rec_next_frame == 0.0)
            app.rec_next_frame = now;
          int emitted = 0;
          while (now + 1e-6 >= app.rec_next_frame && emitted < 4) {
            write_canvas_frame (app);
            app.rec_next_frame += period;
            emitted++;
          }
          // A long stall (a drag, a resize) should not be paid back forever.
          if (now - app.rec_next_frame > 1.0)
            app.rec_next_frame = now;
        }
        last_push = now;
      }
    }

    // Reap finished recorders. Deferred rather than waited on, so stopping a
    // recording never stalls a frame.
    reap_recorder (app.canvas_rec);
    for (Feed & f : app.feeds)
      reap_recorder (f.rec);

    // Ring while an incoming call is waiting on the operator, and bring the
    // window forward so the banner is actually seen. GLFW window calls must
    // come from this thread, not the Pulse worker that is parked in the
    // callback.
    {
      bool pending = app.incoming_pending.load ();
      double now = ImGui::GetTime ();
      if (pending && !app.ringing) {
        app.ringing = true;
        app.ring_next_at = 0.0;
        glfwRequestWindowAttention (window);
        if (glfwGetWindowAttrib (window, GLFW_ICONIFIED))
          glfwRestoreWindow (window);
        glfwFocusWindow (window);
      } else if (!pending && app.ringing) {
        app.ringing = false;
      }
      if (app.ringing && now >= app.ring_next_at) {
        play_ring ();
        app.ring_next_at = now + 2.6;
      }
    }

    // A parked incoming call is waiting for us to leave the current
    // conference. Async, so the UI keeps drawing; the callback is watching
    // conf_status for the transition.
    if (app.hangup_for_incoming.exchange (false)) {
      if (app.floor_taken) {
        pulse_participant_control_release_floor (app.conf, nullptr);
        app.floor_taken = false;
      }
      if (app.input_open) {
        pulse_data_session_disconnect (app.conf, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT, app.input_content);
        app.input_open = false;
      }
      PulseAsyncOperationResultCallbackConfig rcb{on_conf_result, &app};
      pulse_disconnect_async (app.conf, &rcb, nullptr);
      app.call_started = false;
      set_status (app, "Leaving the current conference for the incoming call…");
    }

    if (app.call_failed.exchange (false) && app.call_started &&
        app.conf_status.load () != PULSE_CONNECTION_STATUS_CONNECTED) {
      app.call_started = false;
      app.floor_taken = false;
      app.input_open = false;
    }

    // Attach the canvas once the conference is up — dialled or answered.
    if (app.call_started && app.conf_status.load () == PULSE_CONNECTION_STATUS_CONNECTED)
      ensure_canvas_input (app);

    // Presenting requires the floor, and only once the call is established.
    if (app.call_started && app.cfg.send_as_content && !app.floor_taken &&
        app.conf_status.load () == PULSE_CONNECTION_STATUS_CONNECTED) {
      pulse_participant_control_take_floor (app.conf, nullptr);
      app.floor_taken = true;
    }

    sample_resources (app);

    if (app.bench) {
      double elapsed = glfwGetTime () - app.bench_started;
      // Ignore the first 5s: RTSP connect, decoder warm-up and the first
      // texture uploads are not steady state.
      if (elapsed > 5.0 && app.proc_cpu_pct > 0.0) {
        app.bench_cpu_sum += app.proc_cpu_pct;
        app.bench_cpu_peak = std::max (app.bench_cpu_peak, app.proc_cpu_pct);
        app.bench_comp_sum += app.composite_ms;
        app.bench_samples++;
      }
      if (elapsed >= app.bench_seconds) {
        int live = 0;
        double fps_sum = 0;
        for (const Feed & f : app.feeds)
          if (f.connected) {
            live++;
            fps_sum += f.fps;
          }
        double n = std::max (1, app.bench_samples);
        std::printf ("%-6zu %-6d %-9.0f %-9.0f %-9.0f %-10.1f %-8.1f %.0f\n", app.feeds.size (), live,
                     app.bench_cpu_sum / n, app.bench_cpu_peak, app.proc_rss_mb, app.bench_comp_sum / n,
                     live ? fps_sum / live : 0.0, app.bench_seconds);
        std::fflush (stdout);
        break;
      }
    }

    ImGui_ImplOpenGL3_NewFrame ();
    ImGui_ImplGlfw_NewFrame ();
    ImGui::NewFrame ();

    ImGuiViewport * vp = ImGui::GetMainViewport ();
    ImGui::SetNextWindowPos (vp->Pos);
    ImGui::SetNextWindowSize (vp->Size);
    ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (14, 14));
    ImGui::Begin ("##shell", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- Instrument chrome ---------------------------------------------
    ui_tally (app, vp);

    static char vmr_buf[512], pin_buf[64];
    static bool primed = false;
    if (!primed) {
      snprintf (vmr_buf, sizeof (vmr_buf), "%s", app.cfg.vmr.c_str ());
      snprintf (pin_buf, sizeof (pin_buf), "%s", app.cfg.pin.c_str ());
      primed = true;
    }

    const float content_w = ImGui::GetContentRegionAvail ().x;

    ui_header (app, content_w);
    ImGui::Dummy (ImVec2 (0, du (theme::Gap)));

    ui_deck (app, content_w, vmr_buf, sizeof (vmr_buf), pin_buf, sizeof (pin_buf));
    ImGui::Dummy (ImVec2 (0, du (theme::Gap)));

    if (ui_alert (app, content_w) > 0.0f)
      ImGui::Dummy (ImVec2 (0, du (theme::Gap)));

    // ---- rail + canvas ---------------------------------------------------
    const float body_h = ImGui::GetContentRegionAvail ().y - du (theme::FooterH) - du (theme::Gap);
    ui_settings (app);
    ui_incoming (app, vp->Size);
    ui_pin (app);
    ui_feed_error (app, vp->Size);

    ui_feed_rail (app, du (theme::RailW), body_h);
    ImGui::SameLine (0.0f, du (theme::Gap));
    ui_canvas (app, ImVec2 (ImGui::GetContentRegionAvail ().x, body_h));

    ImGui::Dummy (ImVec2 (0, du (theme::Gap)));
    ui_footer (app, content_w);
    ImGui::End ();
    ImGui::PopStyleVar ();
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

  save_config (app);

  // Stop every recording before anything else and wait for the children to
  // finalise their files: a killed ffmpeg leaves an MP4 with no moov atom that
  // nothing will play. Bounded, so a wedged encoder cannot hang the exit.
  stop_canvas_recording (app);
  for (Feed & f : app.feeds)
    stop_recorder (f.rec, false);
  for (int waited = 0; waited < 100; waited++) {
    bool any = app.canvas_rec.busy ();
    for (Feed & f : app.feeds)
      any = any || f.rec.busy ();
    if (!any)
      break;
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    int st = 0;
    if (app.canvas_rec.pid > 0 && waitpid (app.canvas_rec.pid, &st, WNOHANG) == app.canvas_rec.pid)
      app.canvas_rec.pid = -1;
    for (Feed & f : app.feeds)
      if (f.rec.pid > 0 && waitpid (f.rec.pid, &st, WNOHANG) == f.rec.pid)
        f.rec.pid = -1;
  }

  conf_disconnect (app);
  for (Feed & f : app.feeds)
    stop_feed (f);
  if (app.conf) {
    // Drop the registration so the registrar releases the alias immediately
    // rather than waiting for it to expire. Blocking on purpose.
    if (app.reg_status.load () == PULSE_CONNECTION_STATUS_CONNECTED)
      pulse_deregister (app.conf, nullptr);
    pulse_options_set_registration_state_callback (app.conf, nullptr);
    pulse_options_set_conference_state_callback (app.conf, nullptr);
    pulse_free (app.conf); // last one out
    app.conf = nullptr;
  }

  ImGui_ImplOpenGL3_Shutdown ();
  ImGui_ImplGlfw_Shutdown ();
  ImGui::DestroyContext ();
  glfwDestroyWindow (window);
  glfwTerminate ();
  return 0;
}
