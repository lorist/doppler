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
#include <pexpulse/pulse_media_stats.h>
#include <pexpulse/pulse_rtsp_session.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#include <sys/resource.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
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

  // Feeds
  bool rtsp_tcp = true;                  // TCP suits most IP cameras
  int rtsp_latency_ms = 200;
  bool autoconnect = false;              // connect every feed at startup

  // Interface
  float ui_scale = 1.2f;
  bool show_stats = true;
};

// ----------------------------------------------------------------------------
//  Model
// ----------------------------------------------------------------------------

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

  // Conference (the VMR we push the canvas into).
  Pulse * conf = nullptr;
  std::atomic<int> conf_status{PULSE_CONNECTION_STATUS_DISCONNECTED};
  std::mutex status_mutex;
  std::string status;
  bool input_open = false;
  PulseMediaContent input_content = PULSE_MEDIA_CONTENT_MAIN;
  bool floor_taken = false;
  int last_push_w = 0, last_push_h = 0;

  RgbaImage canvas; // composited each frame

  theme::Fonts fonts;
  int drag_tile = -1;      // tile being moved
  int resize_tile = -1;    // tile being resized
  int rail_drag_feed = -1; // feed being dragged out of the rail

  // Double-click full-screen: remember what to go back to.
  std::vector<Tile> saved_tiles;
  int fullscreen_feed = -1;

  // A Pulse instance held for the whole run. The first pulse_new() performs
  // global (GStreamer) initialisation and the last pulse_free() tears it back
  // down — so a process that drops to zero instances and then creates another
  // crashes inside pulse_new(). Keeping one alive makes that impossible.
  // videowall does the same thing with its device-enumeration instance.
  Pulse * keepalive = nullptr;

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
    else if (k == "canvas") {
      int w = 0, h = 0;
      if (sscanf (v.c_str (), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
        app.cfg.canvas_w = w;
        app.cfg.canvas_h = h;
      }
    } else if (k == "send_fps")
      app.cfg.send_fps = std::max (1, std::min (60, atoi (v.c_str ())));
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

static void
apply_grid (App & app, int cols, int rows)
{
  std::vector<int> feeds = placed_feeds (app);
  if (feeds.empty ())
    for (int i = 0; i < (int) app.feeds.size () && i < cols * rows; i++)
      feeds.push_back (i); // nothing placed yet: fill from the rail

  app.tiles.clear ();
  const float cw = (float) app.cfg.canvas_w / cols;
  const float ch = (float) app.cfg.canvas_h / rows;
  for (int i = 0; i < (int) feeds.size () && i < cols * rows; i++) {
    Tile t;
    t.feed = feeds[i];
    t.x = (i % cols) * cw;
    t.y = (i / cols) * ch;
    t.w = cw;
    t.h = ch;
    app.tiles.push_back (t);
  }
  app.fullscreen_feed = -1;
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

  app.tiles.clear ();
  Tile big;
  big.feed = feeds[0];
  big.x = big.y = 0;
  big.w = app.cfg.canvas_w;
  big.h = app.cfg.canvas_h;
  app.tiles.push_back (big);

  const float pw = app.cfg.canvas_w * 0.22f, ph = pw * 9.0f / 16.0f;
  const float margin = 24.0f;
  for (int i = 1; i < (int) feeds.size () && i <= 4; i++) {
    Tile t;
    t.feed = feeds[i];
    t.w = pw;
    t.h = ph;
    t.x = app.cfg.canvas_w - (pw + margin) * i;
    t.y = app.cfg.canvas_h - ph - margin;
    app.tiles.push_back (t);
  }
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
  if (err != PULSE_SUCCESS)
    set_status (*app, std::string ("conference: ") + pulse_strerror (err));
}

static void
on_conf_progress (const PulseOperationProgressInfo * info, void * ctx)
{
  set_status (*static_cast<App *> (ctx), info->desc ? info->desc : "");
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

  app.conf = pulse_new ();
  if (!app.conf) {
    set_status (app, "pulse_new() failed");
    return;
  }
  pulse_options_set_self_view_window_handle (app.conf, nullptr);
  pulse_options_set_remote_video_window_handle (app.conf, nullptr);
  pulse_options_set_presentation_video_window_handle (app.conf, nullptr);
  pulse_options_set_application_user_agent_string (app.conf, "uavwall/0.1");

  PulseConferenceStatusCallbackConfig scb{on_conf_status, &app};
  pulse_options_set_conference_state_callback (app.conf, &scb);

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
    pulse_free (app.conf);
    app.conf = nullptr;
    return;
  }

  // Open the push side now, so the moment a canvas is composited it can go
  // out. Content goes on the PRESENTATION slot, which additionally needs the
  // floor to be taken once the call is up (see the status handling below).
  app.input_content = app.cfg.send_as_content ? PULSE_MEDIA_CONTENT_PRESENTATION : PULSE_MEDIA_CONTENT_MAIN;
  app.floor_taken = false;
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
  if (!app.conf)
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
  pulse_options_set_conference_state_callback (app.conf, nullptr);
  pulse_free (app.conf);
  app.conf = nullptr;
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
//  UI
// ----------------------------------------------------------------------------

static bool
pill_button (App & app, const char * label, unsigned int color, ImVec2 size, bool active = false)
{
  ImGui::PushStyleColor (ImGuiCol_Button, theme::Hex (color, active ? 0.95f : 0.14f));
  ImGui::PushStyleColor (ImGuiCol_ButtonHovered, theme::Hex (color, active ? 1.0f : 0.30f));
  ImGui::PushStyleColor (ImGuiCol_ButtonActive, theme::Hex (color, 0.85f));
  ImGui::PushStyleColor (ImGuiCol_Text, theme::Hex (0xFFFFFF, active ? 1.0f : 0.85f));
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

// Left rail: every configured feed, with a live thumbnail and state.
static void
ui_feed_rail (App & app, float w, float h)
{
  ImGui::BeginChild ("##rail", ImVec2 (w, h), ImGuiChildFlags_Borders);
  ImDrawList * dl = ImGui::GetWindowDrawList ();

  ImGui::PushFont (app.fonts.bodyBold);
  ImGui::TextUnformatted ("FEEDS");
  ImGui::PopFont ();
  ImGui::Dummy (ImVec2 (0, 4));

  const float tw = ImGui::GetContentRegionAvail ().x;
  const float th = tw * 9.0f / 16.0f;

  for (int i = 0; i < (int) app.feeds.size (); i++) {
    Feed & f = app.feeds[i];
    ImGui::PushID (i);

    ImVec2 p0 = ImGui::GetCursorScreenPos ();
    ImGui::InvisibleButton ("##thumb", ImVec2 (tw, th + (app.cfg.show_stats ? 40 : 26)));
    bool hovered = ImGui::IsItemHovered ();
    bool dbl = hovered && ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left);
    ImVec2 p1 (p0.x + tw, p0.y + th);

    // Thumbnail (or a placeholder when there is no picture yet).
    if (f.texture && f.tex_w > 0) {
      dl->AddImageRounded ((ImTextureID) (intptr_t) f.texture, p0, p1, ImVec2 (0, 0), ImVec2 (1, 1), IM_COL32_WHITE,
                           theme::RadiusRow);
    } else {
      dl->AddRectFilled (p0, p1, theme::WhiteU32 (0.05f), theme::RadiusRow);
      const char * msg = f.connected ? "waiting…" : (f.error.empty () ? "offline" : "error");
      ImVec2 ts = app.fonts.small_->CalcTextSizeA (theme::fs (11.0f), FLT_MAX, 0, msg);
      dl->AddText (app.fonts.small_, theme::fs (11.0f), ImVec2 ((p0.x + p1.x - ts.x) / 2, (p0.y + p1.y) / 2),
                   theme::WhiteU32 (0.4f), msg);
    }

    bool placed = feed_is_placed (app, i);
    dl->AddRect (p0, p1,
                 placed ? theme::HexU32 (theme::AccentPrimary, 0.9f) : theme::WhiteU32 (hovered ? 0.35f : 0.12f),
                 theme::RadiusRow, 0, placed ? 2.0f : 1.0f);

    // Live dot: green while frames are arriving, amber if stalled.
    double age = ImGui::GetTime () - f.last_frame_at;
    if (f.connected) {
      unsigned int c = (f.last_frame_at > 0 && age < 2.0) ? theme::StatusOnline : 0xF59E0B;
      dl->AddCircleFilled (ImVec2 (p1.x - 12, p0.y + 12), 4.5f, theme::HexU32 (c));
    }

    dl->AddText (app.fonts.smallMed, theme::fs (11.0f), ImVec2 (p0.x + 2, p1.y + 5), theme::WhiteU32 (0.8f),
                 f.name.c_str ());

    // Per-feed readout: resolution, decoded frame rate and payload rate.
    if (app.cfg.show_stats && f.connected) {
      char line[96];
      if (f.tex_w > 0)
        snprintf (line, sizeof (line), "%dx%d  %.0f fps  %.0f Mbps", f.tex_w, f.tex_h, f.fps, f.mbps);
      else
        snprintf (line, sizeof (line), "no frames yet");
      dl->AddText (app.fonts.small_, theme::fs (10.0f), ImVec2 (p0.x + 2, p1.y + 5 + theme::fs (13.0f)),
                   theme::WhiteU32 (0.42f), line);
    }

    if (hovered) {
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
      if (!f.error.empty ())
        ImGui::SetTooltip ("%s\n%s", f.url.c_str (), f.error.c_str ());
      else
        ImGui::SetTooltip ("%s\n%s", f.url.c_str (),
                           placed ? "double-click: full screen · click: remove from canvas"
                                  : "click: add to canvas · double-click: full screen");
    }

    // Double-click punches full screen; a single click adds/removes.
    if (dbl) {
      toggle_fullscreen (app, i);
    } else if (hovered && ImGui::IsMouseReleased (ImGuiMouseButton_Left) && !ImGui::IsMouseDragging (0)) {
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

    ImGui::Dummy (ImVec2 (0, 6));
    ImGui::PopID ();
  }

  ImGui::EndChild ();
}

// Settings. A real window (not a draw-list overlay) so its widgets receive
// input reliably; see the ImGui overlay rules the other demos learned the hard
// way. Changes apply immediately where that is safe, and are written to
// uavwall.conf on Save.
static void
ui_settings (App & app)
{
  if (!app.show_settings)
    return;

  ImGui::SetNextWindowSize (ImVec2 (560 * theme::scale, 0));
  ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::PushStyleColor (ImGuiCol_WindowBg, theme::Hex (theme::WindowBgMid, 0.98f));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (18, 16));
  ImGui::Begin ("Settings", &app.show_settings,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                  ImGuiWindowFlags_AlwaysAutoResize);

  const bool live = app.conf != nullptr;

  // ---- Conference -------------------------------------------------------
  ImGui::TextDisabled ("CONFERENCE");
  {
    static char vmr[512], pin[64], name[128];
    if (ImGui::IsWindowAppearing ()) {
      snprintf (vmr, sizeof (vmr), "%s", app.cfg.vmr.c_str ());
      snprintf (pin, sizeof (pin), "%s", app.cfg.pin.c_str ());
      snprintf (name, sizeof (name), "%s", app.cfg.display_name.c_str ());
    }
    ImGui::BeginDisabled (live); // changing these mid-call would not take effect
    if (ImGui::InputText ("VMR (name@server)", vmr, sizeof (vmr)))
      app.cfg.vmr = vmr;
    if (ImGui::InputText ("PIN", pin, sizeof (pin), ImGuiInputTextFlags_Password))
      app.cfg.pin = pin;
    if (ImGui::InputText ("Display name", name, sizeof (name)))
      app.cfg.display_name = name;
    ImGui::EndDisabled ();
    if (live)
      ImGui::TextDisabled ("Stop sending to change these.");
  }

  // ---- Canvas & sending -------------------------------------------------
  ImGui::Dummy (ImVec2 (0, 8));
  ImGui::TextDisabled ("CANVAS & SENDING");
  {
    // Presets rather than free entry: these are the sizes that make sense for
    // a conference, and the cost difference between them is significant.
    static const struct
    {
      const char * label;
      int w, h;
    } sizes[] = {{"1920x1080 (default)", 1920, 1080}, {"1280x720 (about half the compositing cost)", 1280, 720},
                 {"960x540 (lightest)", 960, 540}};

    int current = 0;
    for (int i = 0; i < 3; i++)
      if (sizes[i].w == app.cfg.canvas_w && sizes[i].h == app.cfg.canvas_h)
        current = i;

    if (ImGui::BeginCombo ("Send resolution", sizes[current].label)) {
      for (int i = 0; i < 3; i++) {
        if (ImGui::Selectable (sizes[i].label, i == current) &&
            (sizes[i].w != app.cfg.canvas_w || sizes[i].h != app.cfg.canvas_h)) {
          // Rescale existing tiles so the layout survives the change.
          float sx = (float) sizes[i].w / app.cfg.canvas_w;
          float sy = (float) sizes[i].h / app.cfg.canvas_h;
          for (Tile & t : app.tiles) {
            t.x *= sx;
            t.y *= sy;
            t.w *= sx;
            t.h *= sy;
          }
          app.cfg.canvas_w = sizes[i].w;
          app.cfg.canvas_h = sizes[i].h;
          // push_canvas notices the size change and reconfigures the session.
        }
      }
      ImGui::EndCombo ();
    }

    ImGui::SliderInt ("Send frame rate", &app.cfg.send_fps, 10, 30, "%d fps");
    ImGui::TextDisabled ("Lower is cheaper; 25-30 looks smooth for moving imagery.");

    ImGui::BeginDisabled (live);
    static const char * as[] = {"Main video (as this participant's camera)", "Content (the presentation stream)"};
    int mode = app.cfg.send_as_content ? 1 : 0;
    if (ImGui::Combo ("Send as", &mode, as, 2))
      app.cfg.send_as_content = (mode == 1);
    ImGui::EndDisabled ();
    ImGui::TextDisabled ("%s", app.cfg.send_as_content
                                 ? "Most endpoints show content alongside the participants."
                                 : "Replaces our camera; every endpoint can show it.");
  }

  // ---- Feeds ------------------------------------------------------------
  ImGui::Dummy (ImVec2 (0, 8));
  ImGui::TextDisabled ("FEEDS");
  {
    ImGui::BeginDisabled (live);
    static const char * transports[] = {"TCP (most IP cameras)", "UDP"};
    int t = app.cfg.rtsp_tcp ? 0 : 1;
    if (ImGui::Combo ("Transport", &t, transports, 2))
      app.cfg.rtsp_tcp = (t == 0);
    ImGui::SliderInt ("Jitter buffer", &app.cfg.rtsp_latency_ms, 0, 1000, "%d ms");
    ImGui::EndDisabled ();
    ImGui::Checkbox ("Connect all feeds on startup", &app.cfg.autoconnect);

    ImGui::Dummy (ImVec2 (0, 4));
    int remove = -1;
    for (int i = 0; i < (int) app.feeds.size (); i++) {
      ImGui::PushID (2000 + i);
      char nbuf[128], ubuf[512];
      snprintf (nbuf, sizeof (nbuf), "%s", app.feeds[i].name.c_str ());
      snprintf (ubuf, sizeof (ubuf), "%s", app.feeds[i].url.c_str ());

      ImGui::SetNextItemWidth (110 * theme::scale);
      if (ImGui::InputText ("##n", nbuf, sizeof (nbuf)))
        app.feeds[i].name = nbuf;
      ImGui::SameLine ();
      ImGui::SetNextItemWidth (-90 * theme::scale);
      if (ImGui::InputText ("##u", ubuf, sizeof (ubuf)))
        app.feeds[i].url = ubuf;
      ImGui::SameLine ();
      ImGui::BeginDisabled (app.feeds[i].connected);
      if (ImGui::Button ("Remove"))
        remove = i;
      ImGui::EndDisabled ();
      ImGui::PopID ();
    }
    if (remove >= 0) {
      remove_feed_tiles (app, remove);
      stop_feed (app.feeds[(size_t) remove]);
      app.feeds.erase (app.feeds.begin () + remove);
      // Tiles reference feeds by index, so anything after the removed one
      // shifts down.
      for (Tile & tl : app.tiles)
        if (tl.feed > remove)
          tl.feed--;
      app.fullscreen_feed = -1;
    }
    if (ImGui::Button ("Add feed")) {
      Feed f;
      char buf[64];
      snprintf (buf, sizeof (buf), "FEED %02d", (int) app.feeds.size () + 1);
      f.name = buf; // a plain placeholder; rename it to a callsign
      f.url = "rtsp://";
      app.feeds.push_back (std::move (f));
    }
  }

  // ---- Interface --------------------------------------------------------
  ImGui::Dummy (ImVec2 (0, 8));
  ImGui::TextDisabled ("INTERFACE");
  ImGui::SliderFloat ("UI scale", &app.cfg.ui_scale, 0.8f, 2.0f, "%.2f");
  ImGui::TextDisabled ("Applies on next start.");
  ImGui::Checkbox ("Show stats", &app.cfg.show_stats);

  // ---- Save -------------------------------------------------------------
  ImGui::Dummy (ImVec2 (0, 10));
  if (pill_button (app, "Save", theme::AccentPrimary, ImVec2 (100, 28), true)) {
    save_config (app);
    g_cfg_rtsp_tcp = app.cfg.rtsp_tcp;
    g_cfg_rtsp_latency_ms = app.cfg.rtsp_latency_ms;
    set_status (app, "Saved to uavwall.conf");
    app.show_settings = false;
  }
  ImGui::SameLine ();
  if (ImGui::Button ("Close", ImVec2 (100, 28)))
    app.show_settings = false;

  ImGui::End ();
  ImGui::PopStyleVar ();
  ImGui::PopStyleColor ();
}

// The send canvas: what the VMR receives, drawn to scale.
static void
ui_canvas (App & app, ImVec2 size)
{
  ImGui::BeginChild ("##canvas", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
  ImDrawList * dl = ImGui::GetWindowDrawList ();

  // Fit the 16:9 canvas inside the available area.
  ImVec2 avail = ImGui::GetContentRegionAvail ();
  float scale = std::min (avail.x / app.cfg.canvas_w, avail.y / app.cfg.canvas_h);
  ImVec2 origin = ImGui::GetCursorScreenPos ();
  ImVec2 c0 (origin.x + (avail.x - app.cfg.canvas_w * scale) / 2, origin.y + (avail.y - app.cfg.canvas_h * scale) / 2);
  ImVec2 c1 (c0.x + app.cfg.canvas_w * scale, c0.y + app.cfg.canvas_h * scale);

  dl->AddRectFilled (c0, c1, theme::HexU32 (theme::WindowBgStart), 6.0f);
  dl->AddRect (c0, c1, theme::WhiteU32 (0.12f), 6.0f);

  auto to_screen = [&] (float cx, float cy) { return ImVec2 (c0.x + cx * scale, c0.y + cy * scale); };

  for (int i = 0; i < (int) app.tiles.size (); i++) {
    Tile & t = app.tiles[i];
    if (t.feed < 0 || t.feed >= (int) app.feeds.size ())
      continue;
    Feed & f = app.feeds[t.feed];

    ImVec2 t0 = to_screen (t.x, t.y);
    ImVec2 t1 = to_screen (t.x + t.w, t.y + t.h);

    if (f.texture && f.tex_w > 0)
      dl->AddImage ((ImTextureID) (intptr_t) f.texture, t0, t1);
    else
      dl->AddRectFilled (t0, t1, theme::WhiteU32 (0.06f));

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

    // Resize handle, bottom-right.
    const float grip = 16.0f;
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

    dl->AddRect (t0, t1, hovered || grip_hover ? theme::HexU32 (theme::AccentPrimary) : theme::WhiteU32 (0.25f), 2.0f,
                 0, hovered || grip_hover ? 2.0f : 1.0f);
    dl->AddTriangleFilled (ImVec2 (t1.x - grip, t1.y), ImVec2 (t1.x, t1.y - grip), t1,
                           grip_hover ? theme::HexU32 (theme::AccentPrimary) : theme::WhiteU32 (0.35f));

    // Name badge.
    ImVec2 ts = app.fonts.smallMed->CalcTextSizeA (theme::fs (11.0f), FLT_MAX, 0, f.name.c_str ());
    dl->AddRectFilled (ImVec2 (t0.x + 6, t0.y + 6), ImVec2 (t0.x + 14 + ts.x, t0.y + 12 + ts.y),
                       theme::HexU32 (theme::WindowBgStart, 0.65f), 4.0f);
    dl->AddText (app.fonts.smallMed, theme::fs (11.0f), ImVec2 (t0.x + 10, t0.y + 9), theme::WhiteU32 (0.9f),
                 f.name.c_str ());

    if (hovered)
      ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);
    ImGui::PopID ();
  }

  if (app.tiles.empty ()) {
    const char * msg = "Click a feed to place it · double-click for full screen";
    ImVec2 ts = app.fonts.body->CalcTextSizeA (theme::fs (13.5f), FLT_MAX, 0, msg);
    dl->AddText (app.fonts.body, theme::fs (13.5f), ImVec2 ((c0.x + c1.x - ts.x) / 2, (c0.y + c1.y) / 2),
                 theme::WhiteU32 (0.35f), msg);
  }

  ImGui::EndChild ();
}

// ----------------------------------------------------------------------------
//  main
// ----------------------------------------------------------------------------

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

  g_cfg_rtsp_tcp = app.cfg.rtsp_tcp;
  g_cfg_rtsp_latency_ms = app.cfg.rtsp_latency_ms;

  // Before any other instance — see App::keepalive.
  app.keepalive = pulse_new ();
  if (!app.keepalive)
    std::fprintf (stderr, "[uavwall] warning: keepalive pulse_new() failed\n");

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
      bool sending = app.input_open || app.bench_force_composite;
      for (int i = 0; i < (int) app.feeds.size (); i++)
        pump_feed (app.feeds[i], sending && feed_is_placed (app, i));
    }

    // Compositing a 1920x1080 frame on the CPU is the most expensive thing
    // this app does, and the canvas preview draws from the feed textures
    // directly — so only build it when there is a conference to push it to,
    // and only at the 30fps the send session is configured for.
    if (app.input_open || app.bench_force_composite) {
      double now = glfwGetTime ();
      static double last_push = 0.0;
      if (now - last_push >= 1.0 / std::max (1, app.cfg.send_fps)) {
        double t0 = glfwGetTime ();
        composite (app);
        app.composite_ms = (glfwGetTime () - t0) * 1000.0;
        push_canvas (app);
        last_push = now;
      }
    }

    // Presenting requires the floor, and only once the call is established.
    if (app.conf && app.cfg.send_as_content && !app.floor_taken &&
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

    // ---- top bar: VMR + presets ----------------------------------------
    int cstat = app.conf_status.load ();
    bool in_conf = cstat == PULSE_CONNECTION_STATUS_CONNECTED;

    {
      static char vmr_buf[512], pin_buf[64];
      static bool primed = false;
      if (!primed) {
        snprintf (vmr_buf, sizeof (vmr_buf), "%s", app.cfg.vmr.c_str ());
        snprintf (pin_buf, sizeof (pin_buf), "%s", app.cfg.pin.c_str ());
        primed = true;
      }
      ImGui::SetNextItemWidth (300);
      if (ImGui::InputTextWithHint ("##vmr", "vmr@server", vmr_buf, sizeof (vmr_buf)))
        app.cfg.vmr = vmr_buf;
      ImGui::SameLine ();
      ImGui::SetNextItemWidth (110);
      if (ImGui::InputTextWithHint ("##pin", "PIN", pin_buf, sizeof (pin_buf), ImGuiInputTextFlags_Password))
        app.cfg.pin = pin_buf;
    }
    ImGui::SameLine ();
    if (!app.conf) {
      if (pill_button (app, "Send to VMR", theme::AccentPrimary, ImVec2 (130, 30), true))
        conf_connect (app);
    } else {
      if (pill_button (app, in_conf ? "Stop sending" : "Cancel", theme::StatusError, ImVec2 (130, 30), true))
        conf_disconnect (app);
    }

    ImGui::SameLine ();
    ImGui::Dummy (ImVec2 (18, 0));
    ImGui::SameLine ();
    if (pill_button (app, "1-up", theme::AccentPrimary, ImVec2 (64, 30)))
      apply_grid (app, 1, 1);
    ImGui::SameLine ();
    if (pill_button (app, "2x2", theme::AccentPrimary, ImVec2 (64, 30)))
      apply_grid (app, 2, 2);
    ImGui::SameLine ();
    if (pill_button (app, "3x3", theme::AccentPrimary, ImVec2 (64, 30)))
      apply_grid (app, 3, 3);
    ImGui::SameLine ();
    if (pill_button (app, "PiP", theme::AccentPrimary, ImVec2 (64, 30)))
      apply_pip (app);
    ImGui::SameLine ();
    if (pill_button (app, "Clear", theme::StatusError, ImVec2 (70, 30))) {
      app.tiles.clear ();
      app.fullscreen_feed = -1;
    }

    ImGui::SameLine ();
    ImGui::Dummy (ImVec2 (18, 0));
    ImGui::SameLine ();
    if (pill_button (app, "Settings", theme::AccentPrimary, ImVec2 (86, 30), app.show_settings))
      app.show_settings = !app.show_settings;

    ImGui::SameLine ();
    if (pill_button (app, app.cfg.show_stats ? "Stats on" : "Stats off", theme::AccentPrimary, ImVec2 (86, 30),
                     app.cfg.show_stats))
      app.cfg.show_stats = !app.cfg.show_stats;
    ImGui::SameLine ();

    bool any_off = false;
    for (Feed & f : app.feeds)
      if (!f.connected)
        any_off = true;
    if (pill_button (app, any_off ? "Connect all" : "Disconnect all",
                     any_off ? theme::StatusOnline : theme::StatusError, ImVec2 (128, 30))) {
      for (Feed & f : app.feeds) {
        if (any_off)
          start_feed (f);
        else
          stop_feed (f);
      }
      if (!any_off) {
        app.tiles.clear ();
        app.fullscreen_feed = -1;
      }
    }

    ImGui::Dummy (ImVec2 (0, 8));

    // ---- rail + canvas ---------------------------------------------------
    const float rail_w = 190.0f * theme::scale;
    float body_h = ImGui::GetContentRegionAvail ().y - 30;
    ui_settings (app);
    ui_feed_rail (app, rail_w, body_h);
    ImGui::SameLine ();
    ui_canvas (app, ImVec2 (ImGui::GetContentRegionAvail ().x, body_h));

    // ---- status ----------------------------------------------------------
    {
      std::string s = get_status (app);
      const char * state = in_conf   ? (app.cfg.send_as_content ? "sending as content" : "sending as main video")
                           : app.conf ? "connecting…"
                                      : "not sending";
      ImGui::PushFont (app.fonts.small_);
      ImGui::TextColored (theme::Hex (0xFFFFFF, in_conf ? 0.75f : theme::TextTertiary), "%s%s%s", state,
                          s.empty () ? "" : "  ·  ", s.c_str ());

      if (app.cfg.show_stats) {
        int live = 0;
        for (const Feed & f : app.feeds)
          if (f.connected)
            live++;

        ImGui::SameLine ();
        ImGui::TextColored (theme::Hex (0xFFFFFF, theme::TextTertiary),
                            "     %d feed%s  ·  CPU %.0f%%  ·  RSS %.0f MB", live, live == 1 ? "" : "s",
                            app.proc_cpu_pct, app.proc_rss_mb);
        if (app.input_open) {
          ImGui::SameLine ();
          ImGui::TextColored (theme::Hex (0xFFFFFF, theme::TextTertiary), "  ·  composite %.1f ms",
                              app.composite_ms);
        }
        if (app.tx_valid) {
          ImGui::SameLine ();
          ImGui::TextColored (theme::Hex (theme::AccentPrimary, 0.9f),
                              "  ·  tx %.1f Mbps  loss %.1f%%  rtt %.0f ms", app.tx_bitrate / 1e6,
                              app.tx_loss_pct, app.tx_rtt_ms);
        }
      }
      ImGui::PopFont ();
    }

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
  conf_disconnect (app);
  for (Feed & f : app.feeds)
    stop_feed (f);
  if (app.keepalive)
    pulse_free (app.keepalive); // last one out

  ImGui_ImplOpenGL3_Shutdown ();
  ImGui_ImplGlfw_Shutdown ();
  ImGui::DestroyContext ();
  glfwDestroyWindow (window);
  glfwTerminate ();
  return 0;
}
