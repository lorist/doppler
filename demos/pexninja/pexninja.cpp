#if HAVE_CONFIG_H
#include "pexninjaconfig.h"
#endif

#if defined(HOST_WINDOWS)
/* Prevent <windows.h> from defining min/max macros that collide with
 * std::min / std::max. */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
/* WIN32_LEAN_AND_MEAN (defined by the build) excludes <shellapi.h>, which is
 * needed for ShellExecute below; include it explicitly. */
#include <shellapi.h>
#endif

#if defined(HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#endif

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuiFileBrowser.h>
#include <ImGuiUtils.h>
#include <implot.h>
#include <imgui_stdlib.h>

#include "PexipTheme.h"

/* ---------------------------------------------------------------------------
 * Dear ImGui compatibility shims
 *
 * pexninja was originally written against the "docking" branch of Dear
 * ImGui, which adds multi-viewport / docking-aware flags that are absent
 * from the upstream release branch. When built against the release branch
 * (as doppler does, via FetchContent), the symbols below don't exist —
 * stub them so the corresponding code paths compile into no-ops.
 * ------------------------------------------------------------------------ */
#ifndef IMGUI_HAS_DOCK
#  ifndef ImGuiWindowFlags_NoDocking
#    define ImGuiWindowFlags_NoDocking 0
#  endif
#endif

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <cassert>
#include <inttypes.h>
#if defined(HOST_WINDOWS)
/* The Pexip.Pulse NuGet package lays its C headers out flat (pulse.h next to
 * pulse_types.h …) rather than under a pexpulse/ subdirectory like the Linux
 * .deb / macOS SDK do. The MSVC project adds build/native/include to the
 * include path, so include the header by its bare name there. */
#include <pulse.h>
#else
#include <pexpulse/pulse.h>
#endif

#if !defined(HOST_WINDOWS)
#include <unistd.h> /* gethostname() — used to build the displayed RTMP Publish URL */
#endif

using namespace std::chrono_literals;

/* ---------------------------------------------------------------------------
 * Native helpers (formerly GLib / GStreamer)
 *
 * pexninja was lifted from a codebase that used GLib + GStreamer. The Pexip
 * Pulse library (libpexlgpl) statically embeds its own copies of both, which
 * collide at runtime with the system copies if we also link those. pexninja
 * only used GStreamer for debug logging and GLib for a few small utilities, so
 * rather than depend on (or re-implement) those libraries we use plain C++/
 * POSIX here: logging goes straight to stdout/stderr and the handful of helper
 * functions below cover the rest.
 * ------------------------------------------------------------------------ */

/* Debug logging — replaces the GStreamer GST_* debug categories. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((format (printf, 2, 3)))
#endif
static inline void
pexninja_log (const char * level, const char * fmt, ...)
{
  /* Errors/warnings to stderr, everything else to stdout. */
  FILE * stream = (level[0] == 'E' || level[0] == 'W') ? stderr : stdout;
  fprintf (stream, "%s ", level);
  va_list args;
  va_start (args, fmt);
  vfprintf (stream, fmt, args);
  va_end (args);
  fputc ('\n', stream);
}

#define PEX_LOG_ERROR(...) pexninja_log ("ERROR  ", __VA_ARGS__)
#define PEX_LOG_WARNING(...) pexninja_log ("WARNING", __VA_ARGS__)
#define PEX_LOG_INFO(...) pexninja_log ("INFO   ", __VA_ARGS__)
#define PEX_LOG_DEBUG(...) pexninja_log ("DEBUG  ", __VA_ARGS__)

/* Platform directory separator (was G_DIR_SEPARATOR_S). */
#if defined(HOST_WINDOWS)
#define PEX_DIR_SEPARATOR_S "\\"
#else
#define PEX_DIR_SEPARATOR_S "/"
#endif

/* NULL-safe prefix test (was pex_str_has_prefix). */
static inline bool
pex_str_has_prefix (const char * str, const char * prefix)
{
  if (str == nullptr || prefix == nullptr)
    return false;
  return strncmp (str, prefix, strlen (prefix)) == 0;
}

/* NULL-safe strcmp (was pex_strcmp0). */
static inline int
pex_strcmp0 (const char * a, const char * b)
{
  if (a == nullptr)
    return b == nullptr ? 0 : -1;
  if (b == nullptr)
    return 1;
  return strcmp (a, b);
}

/* Last occurrence of needle in haystack (was pex_strrstr). */
static inline char *
pex_strrstr (const char * haystack, const char * needle)
{
  if (haystack == nullptr || needle == nullptr)
    return nullptr;
  size_t needle_len = strlen (needle);
  size_t haystack_len = strlen (haystack);
  if (needle_len == 0)
    return const_cast<char *> (haystack + haystack_len);
  if (needle_len > haystack_len)
    return nullptr;
  for (const char * p = haystack + haystack_len - needle_len; p >= haystack; p--) {
    if (strncmp (p, needle, needle_len) == 0)
      return const_cast<char *> (p);
  }
  return nullptr;
}

/* Opaque per-thread token, only ever formatted as a pointer in log lines (was
 * g_thread_self). */
static inline void *
pex_thread_self ()
{
  return (void *) (uintptr_t) std::hash<std::thread::id> () (std::this_thread::get_id ());
}

/* Atomic compare-and-exchange on an int-sized lvalue (was
 * g_atomic_int_compare_and_exchange). The template lets it accept the small
 * enum lvalues pexninja guards its popup state machine with. */
template <typename T>
static inline bool
atomic_cas_int (T * atomic, int oldval, int newval)
{
  static_assert (sizeof (T) == sizeof (int), "atomic_cas_int expects an int-sized target");
#if defined(_MSC_VER)
  return _InterlockedCompareExchange (reinterpret_cast<volatile long *> (atomic), (long) newval, (long) oldval) ==
         (long) oldval;
#else
  int expected = oldval;
  return __atomic_compare_exchange_n (reinterpret_cast<int *> (atomic), &expected, newval, false, __ATOMIC_SEQ_CST,
                                      __ATOMIC_SEQ_CST);
#endif
}

/* Small thread-safe queue of audio levels (was a GLib GAsyncQueue). */
struct AudioLevelQueue
{
  std::mutex mutex;
  std::deque<unsigned int> items;
};

#define DEFAULT_TX_KBPS (3 * 1024)
#define MAX_TX_KBPS (10 * 1024)

// The name of the IPC shared memory used for SSO
#define PEXNINJA_SSO_IPC_NAME "pexninja-pexip-auth"

#if defined(HOST_WINDOWS)
#define PexNinjaWindowHandle HWND
#define PexNinjaDisplayHandle HMONITOR
#elif defined(HOST_DARWIN)
#define PexNinjaWindowHandle uint32_t  /* CGWindowID */
#define PexNinjaDisplayHandle uint32_t /* CGDirectDisplayID */
#elif defined(HAVE_X11)
#define PexNinjaWindowHandle Window
#define PexNinjaDisplayHandle Window
#else
#define PexNinjaWindowHandle int
#define PexNinjaDisplayHandle int
#endif

/* Forward decls for the platform-specific window enumeration
 * helpers defined further down in this file. The Compositor
 * source rail (UI code that lives well above their definitions)
 * uses them to populate a per-source window picker, mirroring the
 * "Window capture" dropdown in the legacy presentation menu. */
static std::vector<PexNinjaWindowHandle> enumerate_desktop_windows ();
static std::string get_window_handle_name (PexNinjaWindowHandle handle);

/* Forward decl for the floor-take/release helper. The Compositor's
 * Start/Stop Presentation button calls it to signal the Pexip
 * platform that we want to open (or close) the outgoing
 * presentation stream — same call as the legacy presentation menu. */
struct PexNinja;
static void pexninja_set_presenting (PexNinja * application, bool presenting);

const ImVec4 clear_color = ImVec4 (0.45f, 0.55f, 0.60f, 1.00f);
const ImVec4 green_color = ImVec4 (0.0f, 1.0f, 0.0f, 1.00f);
const ImVec4 yellow_color = ImVec4 (1.0f, 1.0f, 0.0f, 1.00f);
const ImVec4 red_color = ImVec4 (1.0f, 0.0f, 0.0f, 1.00f);
static ImVec4 yellow_red_shift = ImVec4 (1.0f, 0.0f, 0.0f, 1.00f);
static ImVec4 yellow_green_shift = ImVec4 (1.0f, 1.0f, 0.0f, 1.00f);
static ImVec4 black_to_white_shift = ImVec4 (0.0f, 0.0f, 0.0f, 1.00f);
static ImVec4 white_to_black_shift = ImVec4 (1.0f, 1.0f, 1.0f, 1.00f);
static float fade_alpha = 1.0f;

// value from GetWindowHeight() in between BeginMainMenuBar/End().
const int mainmenubar_height = 19;
const int bottombar_height = 19;

const int audio_levels_window_size = 25;

static void
glfw_error_callback (int error, const char * description)
{
  PEX_LOG_ERROR ("GLFW ERROR %d: %s\n", error, description);
}

typedef enum
{
  POPUP_STATE_HIDDEN,
  POPUP_STATE_QUEUED,
  POPUP_STATE_SHOWING,
} PopupState;

typedef enum
{
  ASYNC_OP_NONE,
  ASYNC_OP_CONNECT,
  ASYNC_OP_DISCONNECT,
  ASYNC_OP_REGISTER,
  ASYNC_OP_DEREGISTER
} async_op_type;

struct async_op_data
{
  async_op_type op;
  bool done;
  PulseError err;
};

// clang-format off
#define ASYNC_OP_DATA_INIT {ASYNC_OP_NONE, false, PULSE_SUCCESS}
// clang-format on

typedef enum _stats_src
{
  STAT_SRC_AUDIO,
  STAT_SRC_VIDEO,
  STAT_SRC_SLIDES,
  __STAT_SRC_MAX__
} stats_src;

typedef enum _stats_dir
{
  STAT_DIR_RX,
  STAT_DIR_TX,
  __STATS_DIR_MAX__,
} stats_dir;

typedef enum _stats_entry
{
  STATS_ENTRY_BITRATE,
  STATS_ENTRY_PACKETLOSS,
  STATS_ENTRY_JITTER,
  STATS_ENTRY_RTX_RECEIVED,
  STATS_ENTRY_RTX_SUCCESS,
  STATS_ENTRY_RESOLUTION_X,
  STATS_ENTRY_RESOLUTION_Y,
  STATS_ENTRY_FPS,
  __STATS_ENTRY_MAX__,
} stats_entry;

struct PexNinjaMediaStatsEntry
{
  ImU64 rel_ts;
  ImU64 stats[__STAT_SRC_MAX__][__STATS_DIR_MAX__][__STATS_ENTRY_MAX__];
};

struct PexNinjaMediaStats
{
  std::time_t start_ts;
  std::vector<PexNinjaMediaStatsEntry> entries;
  bool has_stats[__STAT_SRC_MAX__][__STATS_DIR_MAX__][__STATS_ENTRY_MAX__];
};

struct PexNinjaConfigOptions
{
  bool disable_tls_hostname_verification;
  bool disable_tls_peer_verification;
  bool disable_stun_server_support;
  bool disable_turn_server_support;
  bool disable_turn_443_server_support;

  bool allow_direct_fqdn_connection;
  bool allow_direct_ip_connection;

  bool enable_fecc_support;
  bool use_pulse_internal_ptz;
  bool enable_agc;
  bool enable_denoise;
  bool enable_blur;
  bool enable_bg_replacement;
  bool enable_scrambler;
  bool mute_audio_on_startup;

  bool enable_direct_media_support;

  bool enable_proxy_server;
  char proxy_server[1024];
  uint16_t proxy_port;
  PulseProxyServerType proxy_type;
  bool enable_proxy_server_authentication;
  char proxy_username[1024];
  char proxy_password[1024];

  PexNinjaConfigOptions ()
    : disable_tls_hostname_verification (false)
    , disable_tls_peer_verification (false)
    , disable_stun_server_support (false)
    , disable_turn_server_support (false)
    , disable_turn_443_server_support (false)
    , allow_direct_fqdn_connection (false)
    , allow_direct_ip_connection (false)
    , enable_fecc_support (false)
    , use_pulse_internal_ptz (false)
    , enable_agc (true)
    , enable_denoise (true)
    , enable_blur (false)
    , enable_bg_replacement (false)
    , enable_scrambler (false)
    , mute_audio_on_startup (false)
    , enable_direct_media_support (false)
    , enable_proxy_server (false)
    , proxy_server ()
    , proxy_port (0)
    , proxy_type (PulseProxyServerType::PULSE_PROXY_SERVER_HTTPS)
    , enable_proxy_server_authentication (false)
    , proxy_username ()
    , proxy_password ()
  {
  }
};

struct PexNinjaConfig
{
  PexNinjaConfigOptions options;

  struct PexNinjaConfigRegistration
  {
    bool enabled;
    char host[1024];
    char alias[1024];
    char username[1024];
    char password[1024];
    bool use_sso;
  } registration;
  struct PexNinjaConfigConnection
  {
    char server[1024];
    char conference[1024];
    char displayName[1024];
    char pin[1024];
  } connection;
  struct PexNinjaConfigDevices
  {
    bool camera_set;
    uint32_t camera_id;
    char camera_name[1024];

    bool microphone_use_default;
    bool microphone_set;
    uint32_t microphone_id;
    char microphone_name[1024];

    bool speaker_use_default;
    bool speaker_set;
    uint32_t speaker_id;
    char speaker_name[1024];

  } devices;

  char auth_token[1024];
  char display_name[1024];
  char exchange_token_failure_count[1024];
};

struct AlarmEntry
{
  std::string msg;
  ImVec4 color;
  std::chrono::steady_clock::time_point valid_until;
  bool expires; /* false when display_seconds == 0 (display until cancelled) */
};

struct PexNinjaAlarms
{
  std::mutex mutex;
  /* Insertion-ordered list for rendering; the map provides O(1) lookup by msg for cancel */
  std::list<AlarmEntry> entries;
  std::unordered_map<std::string, std::list<AlarmEntry>::iterator> lookup;
};

struct PexNinjaState
{
  struct PexNinjaStateWindows
  {
    int width;
    int height;
    bool show_roster_list;
    bool show_self_view;
    bool show_camera_controls;
    bool show_config;
    bool show_pexninja_info;
    bool show_pulse_info;
    bool show_pmx_media_stats;
    bool show_metrics;
    bool show_registration;
    bool show_connection;
    bool show_chat_window;
    bool show_live_captions_window;
    bool show_add_participant_window;
    bool show_fecc_window;
    bool show_rtmp_input;
    bool show_paint_tools;
    /* Phase 2b.0+: frame-compositor authoring window. Holds two
     * VideoMixDocs per canvas (committed = on the wire, editing = the
     * sketchpad) so the user can lay out a new composition while the
     * outgoing video is untouched, then atomically promote it via
     * "Take" — the program/preview model used in broadcast switchers.
     * The standalone "Source Library" and "Patchbay" windows that
     * shipped in Phase 2a have been folded into the Compositor's
     * sources rail so there is exactly one place to author sources. */
    bool show_compositor;
    /* When true, the self-view ("what we send") is rendered as the big
     * background and the received MAIN video shrinks into the corner
     * window — i.e. the two are swapped. Crucial when testing patchbay
     * routing combinations: lets the user see *what is being sent* in
     * the large viewport rather than squinting at a thumbnail. */
    bool swap_video_views;
  } windows;

  struct PexNinjaStateRosterList
  {
    char search_filter[1024];
  } roster_list;

  struct PexNinjaStateAudioMixer
  {
    bool popup_new_flagged;
    bool prominent;
    std::string send_mix_name;
    std::string recv_mix_name;
  } audio_mixer;

  struct PexNinjaStateBreakoutRooms
  {
    bool popup_new_flagged;
    bool popup_participant_move_flagged;
    bool breakout_buzz = false;
  } breakout_rooms;

  struct PexNinjaStateLayout
  {
    std::string layout;
    bool enable_extended_ac = false;
    bool streaming_indicator = false;
    bool recording_indicator = false;
    bool transcribing_indicator = false;
    bool enable_active_speaker_indication = false;
    bool enable_overlay_text = false;
    bool plus_n_pip_enabled = false;
  } layout;

  struct PexNinjaDtmf
  {
    bool send_to_recepient;
    std::string participant_uuid;
    std::string participant_display_name;
    std::string digits;
  } dtmf;

  bool popup_send_dtmf_sequence;

  struct PexNinjaFecc
  {
    std::string participant_uuid;
    std::string participant_display_name;
  } fecc;

  /* Shared RTMP server: a single RTMP listener that fronts the one
   * Compositor RTMP source we currently support. The Compositor
   * lazy-starts it on first RTMP-source materialise (and lazy-stops
   * it on release); the standalone "RTMP Server" window controls
   * the same listener for explicit Connect / Disconnect plus the
   * listener-wide server settings (path, port, TLS, auth).
   *
   * The Pulse RTMP API today exposes a single `pexrtmpsrc` per
   * listener — i.e. one path per pexninja instance — so `path` is
   * held here as a single server-wide value (rather than per-source)
   * and the publish accept callback admits exactly the publisher
   * whose RTMP path matches it. Only one Compositor `kRtmp` source
   * can own the listener at a time; subsequent sources fail to
   * materialise until the owner releases. */
  struct PexNinjaRtmpServer
  {
    /* Server-wide settings, mutated only from the UI thread. */
    char path[128]; /* RTMP publish path (e.g. "live"); single value
                     * shared across the listener. Empty => unusable. */
    uint16_t listening_port;
    bool use_tls;
    bool support_audio;
    bool support_video;

    bool use_auth;
    char auth_username[128];
    char auth_password[128];

    bool use_tls_config;
    char tls_cert_file[512];
    char tls_key_file[512];
    char tls_ciphers[512];

    /* Hostname advertised in the displayed Publish URL. Defaulted to
     * gethostname() at construction; user-editable so a developer
     * behind NAT or on a non-resolvable hostname can paste in
     * something publishers can actually reach. */
    char advertised_host[256];

    /* Listener lifetime. `is_connected` flips true once the listener
     * is up; `lazy_started` records whether the Compositor (rather
     * than the user) brought it up, so we know to lazy-stop on the
     * RTMP source release. The listener is hard-wired to the
     * PRESENTATION media-content slot — this is internal plumbing
     * the user no longer sees. */
    bool is_connected;
    bool lazy_started;
    PulseMediaContent connected_media_content;

    /* Single-owner state. Only one Compositor `kRtmp` source can
     * own the listener at a time; the path it reads is the
     * server-wide `path` above. `owner_source_id == 0` means no
     * owner. `live_publishers` is the count of RTMP publishers
     * currently publishing on `path`, maintained from the publish
     * start/stop callbacks (which fire on a Pulse worker thread, so
     * all access goes through `mu`). */
    std::mutex mu;
    uint32_t owner_source_id;
    int live_publishers;

    PexNinjaRtmpServer ()
      : listening_port (1935)
      , use_tls (false)
      , support_audio (true)
      , support_video (true)
      , use_auth (false)
      , use_tls_config (false)
      , is_connected (false)
      , lazy_started (false)
      , connected_media_content (PULSE_MEDIA_CONTENT_PRESENTATION)
      , owner_source_id (0)
      , live_publishers (0)
    {
      snprintf (path, sizeof (path), "live");
      auth_username[0] = '\0';
      auth_password[0] = '\0';
      tls_cert_file[0] = '\0';
      tls_key_file[0] = '\0';
      snprintf (tls_ciphers, sizeof (tls_ciphers), "%s", "!eNULL:!aNULL:!EXP:!DES:!RC4:!RC2:!IDEA:!ADH:ALL@STRENGTH");
      advertised_host[0] = '\0';
#if !defined(HOST_WINDOWS)
      if (gethostname (advertised_host, sizeof (advertised_host)) != 0)
        advertised_host[0] = '\0';
      advertised_host[sizeof (advertised_host) - 1] = '\0';
#endif
      if (advertised_host[0] == '\0')
        snprintf (advertised_host, sizeof (advertised_host), "localhost");
    }
  } rtmp_server;

  struct PexNinjaVideoMix
  {
    bool active;
    PulseMediaContent media_content;
    PulseVideoMixInputID desktop_input;
    PulseVideoMixInputID camera_input;
    PulseVideoMixInputID annotation_input;
    /* True when annotation_input is BORROWED from a Compositor Gfx
     * source rather than allocated by this VideoMix. Borrowed
     * handles must NOT be released by us — the owning Compositor
     * source library is responsible for that. Set by the paint code
     * when it routes strokes to a Gfx region the user already added
     * to the active canvas. */
    bool annotation_input_borrowed;

    /* The "desktop_input" slot is polymorphic — it can hold a desktop
     * capture, a window capture, or an RTMP session, depending on which
     * acquire helper was used. The Patchbay UI needs to label it
     * accurately, so we record the kind alongside the input ID. Kept
     * in sync at every acquire / release site for desktop_input. */
    enum BackgroundKind
    {
      kBackgroundNone = 0,
      kBackgroundDesktop,
      kBackgroundWindow,
      kBackgroundRtmp,
    };
    BackgroundKind desktop_input_kind;

    struct InputLayout
    {
      int layer;
      double width_ratio;
      double height_ratio;
      double x_centrepoint;
      double y_centrepoint;
      PulseVideoProcessTypeMask videoproc_mask;
    };

    InputLayout desktop_layout;
    InputLayout camera_layout;
    /* Drawing overlay sits on a higher layer than camera so it
     * composites on top of the live video. */
    InputLayout annotation_layout;

    PexNinjaVideoMix ()
      : active (false)
      , media_content (PULSE_MEDIA_CONTENT_MAIN)
      , desktop_input (PULSE_VIDEO_MIX_INPUT_ID_NONE)
      , camera_input (PULSE_VIDEO_MIX_INPUT_ID_NONE)
      , annotation_input (PULSE_VIDEO_MIX_INPUT_ID_NONE)
      , annotation_input_borrowed (false)
      , desktop_input_kind (kBackgroundNone)
      , desktop_layout{0, 1.0, 1.0, 0.5, 0.5, PULSE_VIDEO_PROCESS_TYPE_NONE}
      , camera_layout{1, 1.0, 1.0, 0.5, 0.5, PULSE_VIDEO_PROCESS_TYPE_NONE}
      , annotation_layout{2, 1.0, 1.0, 0.5, 0.5, PULSE_VIDEO_PROCESS_TYPE_NONE}
    {
    }
  };
  // This client
  bool audio_temporarily_unmuted;
  bool audio_mute;
  bool video_mute;
  bool buzz;
  bool live_captions;

  struct async_op_data async_op;
  bool wait;
  PulseConnectionStatus conn_status;
  PulseConnectionStatus reg_status;
  bool registered;
  bool presenting;
  bool sharing_desktop_video;
  bool sharing_desktop_audio;
  bool quit;

  std::mutex status_lock;
  char conference_status[128];
  char registration_status[128];
  bool is_blocked;
  PulseConferenceServiceType current_service_type;
  float progress;

  std::mutex media_stats_lock;
  PulseMediaStats * media_stats;
  PexNinjaMediaStats media_stats_entries;

  int32_t media_stats_window_secs;

  PulseRegistrationAliasList * conference_alias_list;
  PulseRegistrationAliasList * device_alias_list;

  PulseMediaRotation rotation;

  AudioLevelQueue * mic_audio_levels;

  const char * error_msg;

  int connection_setup_type;
  std::string window_title;
  bool update_window_title;
  bool abort;

  PulseConferenceControlAvailableLayoutsResponse * available_layouts;
  PulseConferenceControlLayoutSvgsResponse * layout_svgs;

  bool enable_verbose_logging;

  int selected_cam_preflight_idx;
  PulseNetworkStatusInfo network_status;

  /* video mix sessions for MAIN_VIDEO and PRESENTATION */
  PexNinjaVideoMix video_mix;
  PexNinjaVideoMix preso_mix;

  /* Paint / annotation overlay state. The actual gfx input lives on
   * video_mix.annotation_input — this struct only carries UI / interaction
   * state. */
  struct PexNinjaPaint
  {
    enum Tool
    {
      kPencil = 0,
      kLine = 1,
      kRectangle = 2,
      kEllipse = 3,
    };

    /* Canvas dimensions chosen at acquire time. Matches the gfx-canvas
     * resolution Pulse will rasterise into. The numbers don't have to
     * match the on-screen widget size — see coord-mapping in the
     * paint-overlay handler. */
    int canvas_width = 1280;
    int canvas_height = 720;

    /* Drawing tools */
    ImVec4 color = ImVec4 (1.0f, 0.2f, 0.2f, 1.0f); /* opaque red default */
    int thickness = 4;                              /* canvas pixels */
    int tool = kPencil;
    bool drawing_mode = false; /* true = overlay
                                  intercepts mouse
                                  events */

    /* In-progress stroke. While the mouse is held we render this in
     * ImGui directly for instant feedback (the new requirement). On
     * release we ship the whole point list to Pulse and clear the
     * preview — the next gfx frame will paint the same line on the
     * outgoing video. */
    bool active_drag = false;
    /* Mouse-down position, used by the rubber-band tools (line, rect,
     * ellipse) to regenerate live_points_* on every move. Pencil
     * ignores this and just appends fresh samples. */
    ImVec2 drag_anchor_screen{0.0f, 0.0f};
    ImVec2 drag_anchor_canvas{0.0f, 0.0f};
    std::vector<ImVec2> live_points_canvas; /* canvas-space, what we send */
    std::vector<ImVec2> live_points_screen; /* screen-space, what we draw */
  } paint;

  /* ------------------------------------------------------------------
   * Source Library — the catalogue of user-configured video sources.
   *
   * A flat collection of Sources, each materialisable into a
   * PulseVideoMixInputID on demand. Sources are decoupled from any
   * mix slot: a single library entry can be referenced by multiple
   * Regions across multiple Compositor canvases. The standalone
   * "Source Library" window from Phase 2a has been retired in favour
   * of inline configuration in the Compositor sources rail; the data
   * model survives because the Compositor (and any future sink) needs
   * a stable handle for each source. */
  struct PexNinjaSourceLibrary
  {
    enum Kind
    {
      kCamera = 0,
      kMp4,
      kImage,
      kRtmp,
      kRtsp,
      kDesktop,
      kWindow,
      kGfx,
    };

    struct Source
    {
      uint32_t id; /* monotonic UI handle, NOT the pulse input id  */
      Kind kind;
      char name[64]; /* user-editable label; defaulted on creation   */

      /* Lazily populated when the user clicks "Materialise". Reset to
       * PULSE_VIDEO_MIX_INPUT_ID_NONE on Release / shutdown. */
      PulseVideoMixInputID input_id;

      /* Per-kind configuration. Kept as plain fields rather than a
       * union so ImGui input widgets can bind to them by reference
       * without juggling discriminants. Unused fields for a given kind
       * cost a few bytes per Source and are not surfaced in the UI. */

      /* Camera */
      int camera_idx; /* index into the global camera_devices vector  */
      /* Index of the camera_devices[] entry that the currently
       * materialised PulseVideoMixInputID was acquired against.
       * Lets us answer "did the user pick a different camera since
       * we last connected?" without having to re-query Pulse:
       *   camera_idx == materialised_camera_idx → in sync
       *   camera_idx != materialised_camera_idx → pending swap that
       *                                           Take needs to apply
       * -1 means "never materialised" (no swap pending). Updated by
       * source_library::_materialise on success and reset to -1 by
       * _release. Mirroring the Settings dialog's camera-hot-swap
       * model — release the old input, acquire a new one — but
       * deferred to the broadcast Take so the wire is only ever
       * mutated at an explicit user transition (eliminates the
       * deadlock you get if you try to release an input that the
       * compositor session is still holding open). */
      int materialised_camera_idx;

      /* Mp4 / Image */
      char file_path[512];
      bool loop; /* mp4 only — wired through to PmxMp4Session loop-count
                  * via pulse_video_mix_input_from_file_with_loop() at
                  * materialise time. Default true. Image kInputs ignore. */

      /* Rtmp — Compositor RTMP sources are fed by a single shared
       * RTMP listener owned by PexNinjaState::PexNinjaRtmpServer. The
       * publish path is a server-wide setting (one path per pexninja
       * instance) — see `PexNinjaRtmpServer::path`. There is no
       * per-source RTMP state worth carrying here today. */

      /* Rtsp — each Compositor RTSP source dials out to its own
       * camera URL with its own opaque PulseRtspSessionID, so all
       * configuration (and the resulting session handle) lives
       * per-source. RTSP is treated as a plain content source — the
       * mix-API places it onto whatever surfaces the user wires up.
       * Multiple kRtsp sources (i.e. multiple cameras) can be
       * materialised concurrently. */
      char rtsp_url[512];        /* Full RTSP URL, e.g. "rtsp://camera.local/stream1". */
      char rtsp_username[64];    /* Optional auth username — empty = anonymous. */
      char rtsp_password[64];    /* Optional auth password — empty = anonymous. */
      int rtsp_transport;        /* Cast to PulseRtspTransport; default TCP. */
      int rtsp_latency_ms;       /* RTP jitterbuffer latency (0 = rtspsrc default). */
      char rtsp_last_error[128]; /* Last connect error message, surfaced inline on the card. */

      /* Pulse RTSP session handle from pulse_rtsp_session_connect_input.
       * #PULSE_RTSP_SESSION_ID_NONE when not connected. */
      PulseRtspSessionID rtsp_session_id;

      /* Stream-picker state. After Connect we ask Pulse for the camera's
       * SDP-derived video stream list and cache it here so the UI can
       * surface a dropdown ("video-0 [H264]", "video-1 [H265]", …)
       * without re-querying Pulse every frame. The cache is owned by
       * the Source and lives only between Connect and Disconnect; it
       * is cleared in source_library::_release.
       *
       * @rtsp_selected_stream_id is the id the user (or auto-pick, when
       * the camera offers exactly one stream) has chosen as the video
       * leg to bind into the mixer. Until a selection is in place we
       * leave @input_id at NONE so the surface stays un-bound, which is
       * what cues the UI to render the picker instead of "● connected". */
      struct RtspStreamChoice
      {
        PulseRtspStreamID id;
        char name[32];  /* e.g. "video-0", as reported by pulse_rtsp_stream_get_name(). */
        char codec[24]; /* e.g. "H264", or empty if the SDP didn't advertise one. */
      };
      std::vector<RtspStreamChoice> rtsp_video_streams;
      PulseRtspStreamID rtsp_selected_stream_id;

      /* Desktop / Window */
      uint64_t desktop_handle;
      /* Cached human-readable label for desktop_handle (e.g.
       * "Firefox — github.com/pexip/media") so the picker combo
       * can show a meaningful preview without calling out to the
       * platform window-info API every frame. Populated when the
       * user picks a window from the Window-kind dropdown; never
       * displayed for kDesktop. */
      char desktop_handle_name[128];

      /* Gfx (annotation) — canvas size for the transparent overlay. */
      int gfx_width;
      int gfx_height;

      /* Whiteboard background fill for kGfx. When @gfx_bg_enabled is
       * true the annotation surface is filled with @gfx_bg_color
       * (RGBA, ImGui 0..1 floats) below all strokes — turning the
       * "transparent overlay" into a whiteboard / blackboard /
       * $colour-board. Default colour is opaque white. The value is
       * pushed to Pulse via pulse_annotation_set_background() at
       * materialise time and on every UI change. */
      bool gfx_bg_enabled;
      float gfx_bg_color[4];

      /* Per-source video processing mask — applied to every region
       * that references this source. Most useful flag is
       * `PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION` which makes the
       * background of a person transparent so PiPs can stack
       * naturally over other regions without a hard rectangle. The
       * mask is a bitfield: SEGMENTATION + BLUR can be on at once.
       *
       * The wire-side model is per-region (per-input-slot), not
       * per-source. We store the user's intent here on the source
       * for a friendly UX ("turn segmentation on for this Camera
       * once") and fan it out to all regions on the source at apply
       * time and on-toggle. */
      PulseVideoProcessTypeMask videoproc_mask;

      Source ()
        : id (0)
        , kind (kCamera)
        , input_id (PULSE_VIDEO_MIX_INPUT_ID_NONE)
        , camera_idx (0)
        , materialised_camera_idx (-1)
        , loop (true)
        , rtsp_transport (PULSE_RTSP_TRANSPORT_TCP)
        , rtsp_latency_ms (0)
        , rtsp_session_id (PULSE_RTSP_SESSION_ID_NONE)
        , rtsp_selected_stream_id (PULSE_RTSP_STREAM_ID_NONE)
        , desktop_handle (0)
        , gfx_width (1280)
        , gfx_height (720)
        , gfx_bg_enabled (false)
        , gfx_bg_color{1.0f, 1.0f, 1.0f, 1.0f}
        , videoproc_mask (PULSE_VIDEO_PROCESS_TYPE_NONE)
      {
        name[0] = '\0';
        file_path[0] = '\0';
        rtsp_url[0] = '\0';
        rtsp_username[0] = '\0';
        rtsp_password[0] = '\0';
        rtsp_last_error[0] = '\0';
        desktop_handle_name[0] = '\0';
      }
    };

    std::vector<Source> sources;
    uint32_t next_id = 1;        /* never reused, even after deletion          */
    int add_kind = (int)kCamera; /* selection in the "+ Add Source" combo      */
  };
  /* NOTE: the source library is OWNED PER-CANVAS by the Compositor
   * below (see PexNinjaCompositor::Canvas::library). There is no
   * single global source_library on PexNinjaState — Main and Preso
   * each get their own rail so users can craft two independent
   * shows. */

  /* ------------------------------------------------------------------
   * Phase 2b.0: Compositor (frame-compositor authoring surface).
   *
   * The Compositor is the eventual replacement for the slot-based
   * camera_layout / desktop_layout / annotation_layout fields above.
   * It models each output canvas as an ordered set of Regions, where
   * each Region references a SourceLibrary entry and carries its own
   * geometry + videoproc mask. A Region is the primitive that ends up
   * as one PulseVideoMixInput entry in the mixer config.
   *
   * Program / Preview split (PGM/PVW) — the broadcast-switcher model:
   *   committed_doc — the layout currently on the wire. Drives
   *                   _build_mix_config_from_doc + pulse_video_mix_connect
   *                   when the user presses Take. PGM.
   *   editing_doc   — the layout the user is sketching. Drives only
   *                   the in-app preview. PVW.
   *
   * A "Take" button atomically copies editing_doc -> committed_doc
   * and calls pulse_video_mix_connect once. Until then, the outgoing
   * video is unchanged — exactly like a hardware switcher. The
   * `auto_take` checkbox flips this back to "edits go live each
   * frame" for users who don't want the explicit-commit discipline.
   *
   * Eager source materialisation: when a Region is added to
   * editing_doc, the bound SourceLibrary entry is materialised
   * immediately (pulse_video_mix_input_from_*) so the preview can show
   * real frames — even though the on-the-wire mix doesn't reference
   * the source until Take. Cost is decode CPU for sources not yet on
   * PGM; this is the conscious choice made in the design Q&A.
   *
   * Phase 2b scope today: doc model, sources rail with inline
   * configuration, region rectangles + layer reorder, and a working
   * Take that rebuilds the mix from committed_doc and calls
   * pulse_video_mix_connect. Drag/resize on the canvas, the
   * per-region inspector and the thumbnail-collage live preview land
   * in 2b.1 / 2b.2 / 2b.3 respectively. */
  struct PexNinjaCompositor
  {
    enum CanvasIdx
    {
      kCanvasMain = 0,
      kCanvasPreso,
      kCanvasCount,
    };

    /* A single composed input on a canvas. Mirrors the per-input
     * geometry that _build_mix_config eventually serialises into a
     * PulseVideoMixInput, plus a back-reference to the SourceLibrary
     * entry that owns the PulseVideoMixInputID. */
    struct Region
    {
      uint32_t id;            /* monotonic UI handle, stable across reorders */
      uint32_t source_lib_id; /* SourceLibrary::Source.id; 0 = unbound       */
      int layer;              /* z-order, higher = on top                    */
      double width_ratio;     /* 0..1, fraction of canvas width              */
      double height_ratio;    /* 0..1, fraction of canvas height             */
      double x_centrepoint;   /* 0..1, normalised position                   */
      double y_centrepoint;   /* 0..1, normalised position                   */
      PulseVideoProcessTypeMask videoproc_mask;

      Region ()
        : id (0)
        , source_lib_id (0)
        , layer (0)
        , width_ratio (1.0)
        , height_ratio (1.0)
        , x_centrepoint (0.5)
        , y_centrepoint (0.5)
        , videoproc_mask (PULSE_VIDEO_PROCESS_TYPE_NONE)
      {
      }
    };

    /* A snapshot of one canvas's composition. The diff between
     * committed_doc and editing_doc lights up the "On Air" indicator. */
    struct VideoMixDoc
    {
      std::vector<Region> regions;

      bool
      equal_to (const VideoMixDoc & other) const
      {
        if (regions.size () != other.regions.size ())
          return false;
        for (size_t i = 0; i < regions.size (); ++i) {
          const Region & a = regions[i];
          const Region & b = other.regions[i];
          if (a.source_lib_id != b.source_lib_id || a.layer != b.layer || a.width_ratio != b.width_ratio ||
              a.height_ratio != b.height_ratio || a.x_centrepoint != b.x_centrepoint ||
              a.y_centrepoint != b.y_centrepoint || a.videoproc_mask != b.videoproc_mask) {
            return false;
          }
        }
        return true;
      }
    };

    struct Canvas
    {
      VideoMixDoc committed; /* PGM — drives the wire on Take                */
      VideoMixDoc editing;   /* PVW — the sketchpad                          */

      /* Per-canvas source library. Main and Preso have completely
       * independent source rails so the user can compose two
       * different shows in parallel — e.g. a "Camera + slides" mix
       * on Main and a "Slides + speaker headshot" mix on Preso —
       * without source-list cross-talk between them. Source IDs
       * are unique only within a canvas; lookups always go through
       * the owning canvas. */
      PexNinjaSourceLibrary library;
    };

    Canvas canvases[kCanvasCount];
    uint32_t next_region_id = 1; /* never reused, even after deletion        */

    /* Per-canvas: did the most recent Take successfully push the
     * committed_doc onto the wire via pulse_video_mix_connect? Used
     * at shutdown to know whether to call pulse_video_mix_disconnect
     * for the canvas's media_content even though the legacy `vm` /
     * `pm` structures don't think they're active. */
    bool connected_on_wire[kCanvasCount] = {false, false};

    /* True after the first Compositor open seeded Main with a default
     * "Camera fills canvas" region. Kept on state (not a static local)
     * so a fresh PexNinja launch will re-seed even if the previous
     * session deleted the default region. */
    bool default_seeded = false;

    /* Auto-take: bypass the explicit Take step and copy editing_doc ->
     * committed_doc every frame. Off by default — broadcast convention
     * is explicit commit. */
    bool auto_take = false;

    /* Which canvas tab is currently active in the UI (drives the
     * Sources rail's "add to active canvas" button). */
    int active_canvas = (int)kCanvasMain;

    /* Selected region in the active canvas (for highlight + reorder
     * keyboard affordances later). 0 = none. */
    uint32_t selected_region_id = 0;

    /* Live drag/resize state. While `dragging_region_id` is non-zero
     * we're in the middle of a mouse-driven manipulation of a region
     * on the active canvas; ImGui doesn't surface "mouse held since
     * last frame" cleanly across multiple invisible-buttons, so we
     * cache the region we grabbed plus the mode (move vs. which edge
     * we caught) and the original geometry at grab time. Geometry
     * deltas are then computed from `mouse_at_grab` -> current mouse.
     *
     * `drag_mode` encodes the corner/edge: 0=move, 1=N, 2=E, 3=S,
     * 4=W, 5=NE, 6=SE, 7=SW, 8=NW. Corners resize two axes; edges
     * resize one. */
    enum DragMode
    {
      kDragNone = -1,
      kDragMove = 0,
      kDragN,
      kDragE,
      kDragS,
      kDragW,
      kDragNE,
      kDragSE,
      kDragSW,
      kDragNW,
    };
    uint32_t dragging_region_id = 0;
    int drag_mode = (int)kDragNone;
    /* Geometry snapshot at grab time. We snapshot the pixel rect
     * (left, top, right, bottom) on the canvas — the math for
     * "edge X is pinned, opposite edge follows the mouse" is
     * trivially expressed in pixel space, and converts cleanly
     * back to (cx, cy, w, h) using the anchor-with-edge-clamp
     * semantics that Pulse expects (top = cy * (canvas_h - h)).
     *
     * We deliberately do NOT snapshot the normalised cx/cy at grab
     * time: with anchor semantics, cx maps onto a SHRINKING range
     * as w grows (free_w = canvas_w - w), so a constant cx during
     * a resize would silently drag the rect sideways. Pixel-space
     * is the source of truth during a drag. */
    float drag_start_left_px = 0.0f, drag_start_top_px = 0.0f;
    float drag_start_right_px = 0.0f, drag_start_bot_px = 0.0f;
    /* Where the cursor was, relative to the rect's top-left, at
     * the moment the user grabbed. Used by kDragMove so the rect
     * moves with the cursor instead of teleporting its top-left
     * to the cursor on first frame. */
    float drag_grab_offset_x = 0.0f, drag_grab_offset_y = 0.0f;
    /* Mouse position at grab time, in canvas-local pixels. */
    ImVec2 drag_start_mouse{0, 0};
  } compositor;
};

struct PexNinjaAudioUnmute
{
  PopupState popup_state;
  bool accept;
};

struct PexNinjaDisconnected
{
  PopupState popup_state;
  std::string reason;
};

struct PexNinjaTLSDegrade
{
  const char * host;
  const char * reason;
  PopupState popup_state;
  bool accept;
};

struct PexNinjaReferalRequest
{
  PopupState popup_state;
  std::string breakout_name;
  uint32_t room_id;
  uint32_t timeout;
};

struct PexNinjaPinCodeRequest
{
  PopupState popup_state;
  bool guest_pin_required;
  char pin_code[1024];
  bool needs_pin_input_set;
  bool needs_pin_input;
  bool accept;
};

struct PexNinjaConferenceExtensionRequest
{
  PopupState popup_state;
  char conference_extension[1024];
  bool accept;
};

struct PexNinjaIncomingCall
{
  PopupState popup_state;
  const PulseRegistrationsEventIncoming * event;
  bool accept;
};

struct PexNinjaIncomingCallCancelled
{
  PopupState popup_state;
  std::string origin;
};

struct PexNinjaSSOProvider
{
  PopupState popup_state;
  const PulseSSOProviderList * list;
  int chosen_index;
};

struct PexNinjaRoomConferenceStatus
{
  bool locked;
  bool guests_muted;
  bool guests_can_unmute;
  bool set_guests_can_unmute;
  bool all_muted;
  bool presentation_allowed;
  bool started;
  bool live_captions_available;
  bool direct_media;
  bool breakout_rooms_supported;

  struct
  {
    bool enabled;
    std::string name;
    std::string description;
    PulseConferenceStatusBreakoutRoomEndAction end_action;
    uint64_t end_time;
    bool guests_allowed_to_leave;
    bool buzz;
    uint64_t buzz_time;
  } breakout_rooms;

  std::string pinning_config;

  struct
  {
    std::string text;
    std::string set_time;
  } message_text;

  struct
  {
    bool configured;
    size_t current_level;
    std::map<size_t, std::string> levels;
  } classification;
};

struct PexNinjaRoomRosterList
{
  uint32_t active_participants;
  PulseConferenceEventParticipantList * data;
  PulseConferenceEventParticipantList * filtered_data;

  PexNinjaRoomRosterList ()
    : active_participants (0)
    , data (nullptr)
    , filtered_data (nullptr)
  {
    PEX_LOG_DEBUG ("INITIALIZE PexNinjaRoomRosterList");
  }

  ~PexNinjaRoomRosterList ()
  {
    PEX_LOG_DEBUG ("DESTROY PexNinjaRoomRosterList");
    pulse_conference_control_free_participant_list (data);
    pulse_conference_control_free_participant_list (filtered_data);
  }
};

struct PexNinjaRoomChatMessages
{
  std::string sync_join_messages;
  std::string chat_messages;
  std::string concat_chat_messages;

  uint32_t chat_messages_unread;

  char * selected_message_recepient_uuid = nullptr;
  const char * selected_message_recepient_name = nullptr;

  PexNinjaRoomChatMessages ()
    : sync_join_messages ()
    , chat_messages ()
    , concat_chat_messages ()
    , chat_messages_unread (0)
    , selected_message_recepient_uuid (nullptr)
    , selected_message_recepient_name (nullptr)
  {
  }
};

struct PexNinjaRoomPresentation
{
  char presenter_name[128];
  bool preso_started;
  bool preso_render;

  PexNinjaRoomPresentation ()
    : presenter_name ("")
    , preso_started (false)
    , preso_render (false)
  {
  }
};

struct PexNinjaRoomLiveCaptions
{
  std::string live_caption_final;
  std::string live_caption_display;

  PexNinjaRoomLiveCaptions ()
    : live_caption_final ()
    , live_caption_display ()
  {
  }
};

struct PexNinjaRoomStateLayout
{
  std::string layout;
  bool enable_overlay_text;

  PexNinjaRoomStateLayout ()
    : layout ()
    , enable_overlay_text (false)
  {
  }
};

struct PexNinjaRoomAudioMixersList
{
  PulseConferenceAudioMixersList * data;

  PexNinjaRoomAudioMixersList ()
    : data (nullptr)
  {
  }
};

struct PexNinjaRoom
{
  struct PexNinjaRoomConferenceStatus conference_status;
  struct PexNinjaRoomRosterList roster_list;
  struct PexNinjaRoomChatMessages chat_messages;
  struct PexNinjaRoomPresentation presentation;
  struct PexNinjaRoomLiveCaptions live_captions;
  struct PexNinjaRoomStateLayout layout;
  struct PexNinjaRoomAudioMixersList audio_mixers_list;

  PexNinjaRoom ()
  {
    PEX_LOG_DEBUG ("INITIALIZE PexNinjaRoom");
    conference_status.set_guests_can_unmute = true;
  }

  ~PexNinjaRoom ()
  {
    PEX_LOG_DEBUG ("DESTROY PexNinjaRoom");
    conference_status.set_guests_can_unmute = true;
  }
};

struct PexNinjaRoomList
{
  std::mutex mutex;
  uint32_t current_room_id;
  PexNinjaRoom * current_room;
  std::map<uint32_t, PexNinjaRoom *> room_map;

  uint32_t transfer_cancel_flagged;
  uint32_t transfer_cancel_id;

  PexNinjaRoomList ()
  {
    // Initialize our "main" room.
    current_room_id = PULSE_ROOM_ID_MAIN;
    current_room = new PexNinjaRoom{};
    room_map[PULSE_ROOM_ID_MAIN] = current_room;
  }
};

struct PexNinja
{
  Pulse * client;
  const char * config_file;
  GLFWwindow * window;
  PexNinjaConfig config;
  PexNinjaConfig config_last_flush;
  PexNinjaState state;
  PexNinjaAudioUnmute audio_unmute;
  PexNinjaDisconnected disconnected;
  PexNinjaTLSDegrade tls_degrade;
  PexNinjaReferalRequest referal_request;
  PexNinjaPinCodeRequest pin_code_request;
  PexNinjaConferenceExtensionRequest conference_extension_request;
  PexNinjaIncomingCall incoming_call;
  PexNinjaIncomingCallCancelled incoming_call_cancelled;
  PexNinjaSSOProvider sso_provider;
  PexNinjaRoomList room_list;
  PexNinjaAlarms alarms;
  PexNinja ()
    : client ()
    , config ()
    , state ()
    , audio_unmute ()
    , disconnected ()
    , tls_degrade ()
    , referal_request ()
    , pin_code_request ()
    , conference_extension_request ()
    , incoming_call ()
    , incoming_call_cancelled ()
    , sso_provider ()
    , room_list ()
    , alarms ()
  {
    state.media_stats_window_secs = 10;
    state.audio_mixer.send_mix_name = std::string ("main");
    state.audio_mixer.recv_mix_name = std::string ("main");
    state.selected_cam_preflight_idx = -1;
    state.video_mix.media_content = PULSE_MEDIA_CONTENT_MAIN;
    state.preso_mix.media_content = PULSE_MEDIA_CONTENT_PRESENTATION;
  }
};

/* Maximum number of inputs a single video mix carries: desktop +
 * camera + annotation. All callers stack-allocate an array of this
 * size before invoking _build_mix_config(). */
static constexpr size_t kPexNinjaVideoMixMaxInputs = 3;

static PulseVideoMixConfig
_build_mix_config (PexNinjaState::PexNinjaVideoMix & vm, PulseVideoMixInput inputs[kPexNinjaVideoMixMaxInputs])
{
  auto & dl = vm.desktop_layout;
  auto & cl = vm.camera_layout;

  /* Each input is optional. We pack only acquired inputs (input_id !=
   * NONE) into the array; the per-input `layer` field carries the
   * compositing z-order, so the array index is irrelevant to the mixer.
   * This lets us build valid configs for any subset:
   *   - desktop + camera             (background replacement)
   *   - camera + annotation          (drawing only, no bg replacement)
   *   - desktop + camera + annotation (both)
   * The gfx (annotation) layer is additive — the layer index in
   * vm.annotation_layout is set higher than the camera so strokes
   * composite on top of the live video. See pulse_annotation.h. */
  size_t n = 0;
  if (vm.desktop_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    inputs[n++] = {
      .input_id = vm.desktop_input,
      .layer = dl.layer,
      .width_ratio = dl.width_ratio,
      .height_ratio = dl.height_ratio,
      .x_centrepoint = dl.x_centrepoint,
      .y_centrepoint = dl.y_centrepoint,
      .videoproc_mask = dl.videoproc_mask,
    };
  }
  if (vm.camera_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    inputs[n++] = {
      .input_id = vm.camera_input,
      .layer = cl.layer,
      .width_ratio = cl.width_ratio,
      .height_ratio = cl.height_ratio,
      .x_centrepoint = cl.x_centrepoint,
      .y_centrepoint = cl.y_centrepoint,
      .videoproc_mask = cl.videoproc_mask,
    };
  }
  if (vm.annotation_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    auto & gl = vm.annotation_layout;
    inputs[n++] = {
      .input_id = vm.annotation_input,
      .layer = gl.layer,
      .width_ratio = gl.width_ratio,
      .height_ratio = gl.height_ratio,
      .x_centrepoint = gl.x_centrepoint,
      .y_centrepoint = gl.y_centrepoint,
      .videoproc_mask = gl.videoproc_mask,
    };
  }

  return PulseVideoMixConfig{.num_inputs = n, .inputs = inputs};
}

// GLFW callbacks has no userdata, so we need to keep a pointer to our application context globally :/
static PexNinja * g_glfw_app_context;

typedef struct
{
  GLuint texture;
  PulseMediaContent media_content;
} GLTextureContext;

static void _update_config_copy (PexNinja * a);
static void _write_config_if_needed (PexNinja * a);
static void register_alarm (PexNinja * application, const char * msg, uint32_t display_seconds, ImVec4 color);
static bool cancel_alarm (PexNinja * application, const char * msg, uint32_t display_seconds = 0);
static void paint_overlay_handle (PexNinja * application, ImVec2 image_origin, ImVec2 image_size);
static void _paint_cancel_active_drag (PexNinja * application);

static const char *
get_config_file_name ()
{
  const char * prefix = NULL;
  const char * env_config_path = getenv ("CONFIG_PATH");
  if (env_config_path) {
    return env_config_path;
  }

  static char buffer[4092] = "pexninja-config.txt";
#if defined(HOST_LINUX)
  prefix = getenv ("HOME");
#elif defined(HOST_WINDOWS)
  prefix = getenv ("LOCALAPPDATA");
#endif

  if (prefix) {
    snprintf (buffer, 4092, "%s%spexninja-config.txt", prefix, PEX_DIR_SEPARATOR_S);
  }

  PEX_LOG_INFO ("Using config file: %s", buffer);

  return buffer;
}

static void
default_config (PexNinjaConfig * config)
{
  config->options.disable_tls_hostname_verification = false;
  config->options.disable_tls_peer_verification = false;
  config->options.disable_stun_server_support = false;
  config->options.disable_turn_server_support = false;
  config->options.disable_turn_443_server_support = false;
  config->options.allow_direct_fqdn_connection = false;
  config->options.allow_direct_ip_connection = false;
  config->options.enable_fecc_support = false;
  config->options.enable_agc = true;
  config->options.enable_denoise = false;
  config->options.mute_audio_on_startup = false;
  config->options.enable_blur = false;
  config->options.enable_bg_replacement = false;
  config->options.enable_scrambler = false;
  config->options.enable_direct_media_support = false;
  config->options.enable_proxy_server = false;
  config->registration.enabled = false;
  snprintf (config->registration.host, 1024, "nightly.pexip.com");
  snprintf (config->connection.server, 1024, "nightly.pexip.com");
}

static int
read_config (const char * config_file, PexNinjaConfig * config)
{
  assert (config_file);
  assert (config);
  std::ifstream ifs (config_file);
  if (!ifs.good ()) {
    PEX_LOG_DEBUG ("No config file found at '%s', set default values.\n", config_file);
    default_config (config);
    return 0;
  }
  PEX_LOG_DEBUG ("Reading configuration from file '%s'\n", config_file);

  for (;;) {
    std::string line;
    std::getline (ifs, line);
    if (!ifs)
      break;

    std::istringstream iss (line);
    std::string key;
    std::string value;
    std::getline (iss, key, '=');
    std::getline (iss, value);

    if (key.length () == 0) {
      PEX_LOG_DEBUG ("Unparsable config line: '%s'\n", line.c_str ());
      break;
    }

    if (key.compare ("options.disable_tls_hostname_verification") == 0) {
      config->options.disable_tls_hostname_verification = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.disable_tls_peer_verification") == 0) {
      config->options.disable_tls_peer_verification = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.disable_stun_server_support") == 0) {
      config->options.disable_stun_server_support = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.disable_turn_server_support") == 0) {
      config->options.disable_turn_server_support = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.disable_turn_443_server_support") == 0) {
      config->options.disable_turn_443_server_support = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.allow_direct_fqdn_connection") == 0) {
      config->options.allow_direct_fqdn_connection = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.allow_direct_ip_connection") == 0) {
      config->options.allow_direct_ip_connection = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.enable_fecc_support") == 0) {
      config->options.enable_fecc_support = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.use_pulse_internal_ptz") == 0) {
      config->options.use_pulse_internal_ptz = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.mute_audio_on_startup") == 0) {
      config->options.mute_audio_on_startup = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.enable_direct_media_support") == 0) {
      config->options.enable_direct_media_support = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.enable_proxy_server") == 0) {
      config->options.enable_proxy_server = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.proxy_server") == 0) {
      std::size_t length = value.copy (config->options.proxy_server, 1024, 0);
      config->options.proxy_server[length] = '\0';
      continue;
    }

    if (key.compare ("options.proxy_port") == 0) {
      const uint16_t id{(uint16_t)std::strtoul (value.c_str (), nullptr, 10)};
      config->options.proxy_port = id;
      continue;
    }

    if (key.compare ("options.proxy_type") == 0) {
      const uint16_t proxy_type{(uint16_t)std::strtoul (value.c_str (), nullptr, 10)};
      config->options.proxy_type = (PulseProxyServerType)proxy_type;
      continue;
    }

    if (key.compare ("options.enable_proxy_server_authentication") == 0) {
      config->options.enable_proxy_server_authentication = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("options.proxy_username") == 0) {
      std::size_t length = value.copy (config->options.proxy_username, 1024, 0);
      config->options.proxy_username[length] = '\0';
      continue;
    }

    if (key.compare ("options.proxy_password") == 0) {
      std::size_t length = value.copy (config->options.proxy_password, 1024, 0);
      config->options.proxy_password[length] = '\0';
      continue;
    }

    if (key.compare ("registration.enabled") == 0) {
      config->registration.enabled = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("registration.host") == 0) {
      std::size_t length = value.copy (config->registration.host, 1024, 0);
      config->registration.host[length] = '\0';
      continue;
    }

    if (key.compare ("registration.alias") == 0) {
      std::size_t length = value.copy (config->registration.alias, 1024, 0);
      config->registration.alias[length] = '\0';
      continue;
    }

    if (key.compare ("registration.username") == 0) {
      std::size_t length = value.copy (config->registration.username, 1024, 0);
      config->registration.username[length] = '\0';
      continue;
    }

    if (key.compare ("registration.password") == 0) {
      std::size_t length = value.copy (config->registration.password, 1024, 0);
      config->registration.password[length] = '\0';
      continue;
    }

    if (key.compare ("registration.use_sso") == 0) {
      config->registration.use_sso = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("connection.server") == 0) {
      std::size_t length = value.copy (config->connection.server, 1024, 0);
      config->connection.server[length] = '\0';
      continue;
    }

    if (key.compare ("connection.conference") == 0) {
      std::size_t length = value.copy (config->connection.conference, 1024, 0);
      config->connection.conference[length] = '\0';
      continue;
    }

    if (key.compare ("connection.displayName") == 0) {
      std::size_t length = value.copy (config->connection.displayName, 1024, 0);
      config->connection.displayName[length] = '\0';
      continue;
    }

    if (key.compare ("connection.pin") == 0) {
      std::size_t length = value.copy (config->connection.pin, 1024, 0);
      config->connection.pin[length] = '\0';
      continue;
    }

    if (key.compare ("devices.camera_id") == 0) {
      const uint32_t id{(uint32_t)std::strtoul (value.c_str (), nullptr, 10)};
      config->devices.camera_set = true;
      config->devices.camera_id = id;
      continue;
    }

    if (key.compare ("devices.camera_name") == 0) {
      std::size_t length = value.copy (config->devices.camera_name, 1024, 0);
      config->devices.camera_name[length] = '\0';
      continue;
    }

    if (key.compare ("devices.microphone_use_default") == 0) {
      config->devices.microphone_use_default = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("devices.microphone_id") == 0) {
      const uint32_t id{(uint32_t)std::strtoul (value.c_str (), nullptr, 10)};
      config->devices.microphone_set = true;
      config->devices.microphone_id = (uint32_t)id;
      continue;
    }

    if (key.compare ("devices.microphone_name") == 0) {
      std::size_t length = value.copy (config->devices.microphone_name, 1024, 0);
      config->devices.microphone_name[length] = '\0';
      continue;
    }

    if (key.compare ("devices.speaker_use_default") == 0) {
      config->devices.speaker_use_default = (value.compare ("true") == 0 ? true : false);
      continue;
    }

    if (key.compare ("devices.speaker_id") == 0) {
      const uint32_t id{(uint32_t)std::strtoul (value.c_str (), nullptr, 10)};
      config->devices.speaker_set = true;
      config->devices.speaker_id = (uint32_t)id;
      continue;
    }

    if (key.compare ("devices.speaker_name") == 0) {
      std::size_t length = value.copy (config->devices.speaker_name, 1024, 0);
      config->devices.speaker_name[length] = '\0';
      continue;
    }

    if (key.compare ("storage.auth_token") == 0) {
      std::size_t length = value.copy (config->auth_token, 1024, 0);
      config->auth_token[length] = '\0';
      continue;
    }

    if (key.compare ("storage.display_name") == 0) {
      std::size_t length = value.copy (config->display_name, 1024, 0);
      config->display_name[length] = '\0';
      continue;
    }

    PEX_LOG_DEBUG ("Unknown config key: '%s'\n", key.c_str ());
  }

  int err = 0;
  if (!ifs.eof ()) {
    PEX_LOG_DEBUG ("Failed to read config file to EOF!\n");
    err = 1;
  }
  ifs.close ();
  return err;
}

static int
write_config (const char * config_file, PexNinjaConfig * config)
{
  assert (config_file);
  assert (config);

  std::ofstream ofs (config_file, std::ios::out | std::ios::trunc);
  if (!ofs.good ()) {
    PEX_LOG_DEBUG ("Unable to open ouputfile '%s' for writing\n", config_file);
    return 1;
  }

  ofs << "options.disable_tls_hostname_verification="
      << (config->options.disable_tls_hostname_verification ? "true" : "false") << "\n";
  ofs << "options.disable_tls_peer_verification=" << (config->options.disable_tls_peer_verification ? "true" : "false")
      << "\n";
  ofs << "options.disable_stun_server_support=" << (config->options.disable_stun_server_support ? "true" : "false")
      << "\n";
  ofs << "options.disable_turn_server_support=" << (config->options.disable_turn_server_support ? "true" : "false")
      << "\n";
  ofs << "options.disable_turn_443_server_support="
      << (config->options.disable_turn_443_server_support ? "true" : "false") << "\n";
  ofs << "options.allow_direct_fqdn_connection=" << (config->options.allow_direct_fqdn_connection ? "true" : "false")
      << "\n";
  ofs << "options.allow_direct_ip_connection=" << (config->options.allow_direct_ip_connection ? "true" : "false")
      << "\n";
  ofs << "options.enable_fecc_support=" << (config->options.enable_fecc_support ? "true" : "false") << "\n";
  ofs << "options.use_pulse_internal_ptz=" << (config->options.use_pulse_internal_ptz ? "true" : "false") << "\n";
  ofs << "options.mute_audio_on_startup=" << (config->options.mute_audio_on_startup ? "true" : "false") << "\n";
  ofs << "options.enable_direct_media_support=" << (config->options.enable_direct_media_support ? "true" : "false")
      << "\n";
  ofs << "options.enable_proxy_server=" << (config->options.enable_proxy_server ? "true" : "false") << "\n";
  ofs << "options.proxy_server=" << config->options.proxy_server << "\n";
  ofs << "options.proxy_port=" << config->options.proxy_port << "\n";
  ofs << "options.proxy_type=" << (int)config->options.proxy_type << "\n";
  ofs << "options.enable_proxy_server_authentication="
      << (config->options.enable_proxy_server_authentication ? "true" : "false") << "\n";
  ofs << "options.proxy_username=" << config->options.proxy_username << "\n";
  ofs << "options.proxy_password=" << config->options.proxy_password << "\n";
  ofs << "registration.enabled=" << (config->registration.enabled ? "true" : "false") << "\n";
  ofs << "registration.host=" << config->registration.host << "\n";
  ofs << "registration.alias=" << config->registration.alias << "\n";
  ofs << "registration.username=" << config->registration.username << "\n";
  ofs << "registration.password=" << config->registration.password << "\n";
  ofs << "registration.use_sso=" << (config->registration.use_sso ? "true" : "false") << "\n";
  ofs << "connection.server=" << config->connection.server << "\n";
  ofs << "connection.conference=" << config->connection.conference << "\n";
  ofs << "connection.displayName=" << config->connection.displayName << "\n";
  ofs << "connection.pin=" << config->connection.pin << "\n";
  if (config->devices.camera_set) {
    ofs << "devices.camera_id=" << config->devices.camera_id << "\n";
    ofs << "devices.camera_name=" << config->devices.camera_name << "\n";
  }
  ofs << "devices.microphone_use_default=" << (config->devices.microphone_use_default ? "true" : "false") << "\n";
  if (config->devices.microphone_set) {
    ofs << "devices.microphone_id=" << config->devices.microphone_id << "\n";
    ofs << "devices.microphone_name=" << config->devices.microphone_name << "\n";
  }
  ofs << "devices.speaker_use_default=" << (config->devices.speaker_use_default ? "true" : "false") << "\n";
  if (config->devices.speaker_set) {
    ofs << "devices.speaker_id=" << config->devices.speaker_id << "\n";
    ofs << "devices.speaker_name=" << config->devices.speaker_name << "\n";
  }
  if (strlen (config->auth_token) > 0) {
    ofs << "storage.auth_token=" << config->auth_token << "\n";
  }
  if (strlen (config->display_name) > 0) {
    ofs << "storage.display_name=" << config->display_name << "\n";
  }

  ofs.close ();

  PEX_LOG_DEBUG ("Successfully updated configuration file '%s'\n", config_file);

  return 0;
}

static PulseDataSessionConfig *
pulse_data_session_config_video_new ()
{
  PulseDataSessionConfig * cfg;
  cfg = pulse_data_session_config_new (PULSE_DATA_SESSION_VIDEO_FROM_CAPS);
  pulse_data_session_config_video_from_caps (cfg, "video/x-raw, format=RGBA");
  return cfg;
}

static void
imgui_begin_disabled_state ()
{
  ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
  ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
}

static void
imgui_end_disabled_state ()
{
  ImGui::PopItemFlag ();
  ImGui::PopStyleVar ();
}

static void
_set_window_title (PexNinja * application)
{
  if (application->state.window_title.length () > 0) {
    glfwSetWindowTitle (application->window, application->state.window_title.c_str ());
    application->state.window_title.clear ();
  } else {
    PulseSessionInfo * info = NULL;
    if (pulse_session_get_conference_info (application->client, &info) == PULSE_SUCCESS) {
      std::stringstream ss;
      ss << "PexNinja : " << info->conference_name;
      if (info->breakout_name)
        ss << " (" << info->breakout_name << ")";
      ss << " [" << info->conference_alias << "]";
      pulse_session_free_conference_info (info);
      glfwSetWindowTitle (application->window, ss.str ().c_str ());
    }
  }
}

static void
_update_config_copy (PexNinja * a)
{
  memcpy (&a->config_last_flush, &a->config, sizeof (PexNinjaConfig));
}

static void
_write_config_if_needed (PexNinja * a)
{
  if (memcmp (&a->config_last_flush, &a->config, sizeof (PexNinjaConfig)) != 0) {
    if (write_config (a->config_file, &a->config) == 0) {
      _update_config_copy (a);
    }
  }
}

static void
_configure_proxy_server (PexNinja * application)
{
  PulseError err = PULSE_SUCCESS;
  if (application->config.options.enable_proxy_server) {
    PulseProxyServerConfig proxy_config = {
      .proxy_server = application->config.options.proxy_server,
      .proxy_port = application->config.options.proxy_port,
      .proxy_type = application->config.options.proxy_type,
      .proxy_username = NULL,
      .proxy_password = NULL,
    };
    if (application->config.options.enable_proxy_server_authentication) {
      proxy_config.proxy_username = application->config.options.proxy_username;
      proxy_config.proxy_password = application->config.options.proxy_password;
    }
    err = pulse_options_set_proxy_server (application->client, &proxy_config);
  } else {
    err = pulse_options_set_proxy_server (application->client, NULL);
  }
  if (err != PULSE_SUCCESS) {
    PEX_LOG_ERROR ("Failed to configure proxy server: %s", pulse_strerror (err));
  }
}

static void
_update_conference_status_msg (PexNinja * application, const char * msg)
{
  std::lock_guard<std::mutex> lock (application->state.status_lock);
  snprintf (application->state.conference_status, sizeof (application->state.conference_status), "%s", msg);
}

static void
_update_registration_status_msg (PexNinja * application, const char * msg)
{
  std::lock_guard<std::mutex> lock (application->state.status_lock);
  snprintf (application->state.registration_status, sizeof (application->state.registration_status), "%s", msg);
}

static void
_progress_callback_conference (const PulseOperationProgressInfo * progress_info, void * user_context)
{
  assert (progress_info);
  assert (progress_info->desc);
  assert (user_context);
  PexNinja * application = (PexNinja *)user_context;

  application->state.progress = progress_info->progress;
  if (application->state.abort)
    _update_conference_status_msg (application, "Attempting to abort connection...");
  else
    _update_conference_status_msg (application, progress_info->desc);

  PEX_LOG_DEBUG ("PROGESS: '%s'", progress_info->desc);
}

static void
_progress_callback_registration (const PulseOperationProgressInfo * progress_info, void * user_context)
{
  assert (progress_info);
  assert (progress_info->desc);
  assert (user_context);
  PexNinja * application = (PexNinja *)user_context;

  application->state.progress = progress_info->progress;
  if (application->state.abort)
    _update_registration_status_msg (application, "Attempting to abort registration...");
  else
    _update_registration_status_msg (application, progress_info->desc);

  PEX_LOG_DEBUG ("PROGESS: '%s'", progress_info->desc);
}

static int
_sso_selection_callback (PulseSSOProviderList * list, void * user_context)
{
  assert (list);
  assert (user_context);
  PexNinja * application = (PexNinja *)user_context;

  application->sso_provider.popup_state = POPUP_STATE_QUEUED;
  application->sso_provider.list = list;

  // Busywait for accept.
  while (application->sso_provider.popup_state != POPUP_STATE_HIDDEN) {
    std::this_thread::sleep_for (100ms);
  }
  return application->sso_provider.chosen_index;
}

static void
_sso_selection_callback_complete (PexNinja * application, int chosen_index, bool abort)
{
  if (abort)
    application->sso_provider.chosen_index = -1;
  else
    application->sso_provider.chosen_index = chosen_index;

  application->sso_provider.list = NULL;
  application->sso_provider.popup_state = POPUP_STATE_HIDDEN;
}

#ifdef HOST_WINDOWS
static void
_pulse_native_debug_fprintf (FILE * file, const char * format, ...)
{
  va_list args;
  va_start (args, format);
  va_list args_copy;
  va_copy (args_copy, args);
  int length = vsnprintf (nullptr, 0, format, args);
  va_end (args);
  if (length <= 0) {
    va_end (args_copy);
    return;
  }

  char * str = (char *) malloc ((size_t) length + 1);
  if (!str) {
    va_end (args_copy);
    return;
  }
  vsnprintf (str, (size_t) length + 1, format, args_copy);
  va_end (args_copy);

  fwrite (str, 1, (size_t) length, file);
  fflush (file);
  free (str);
}

static bool
_sso_request_callback (PulseSSOProviderRequest * request, PulseSSOProviderSetToken * set_sso_token, void * user_context)
{
  PexNinja * application = (PexNinja *)user_context;

  assert (request);
  assert (set_sso_token);
  assert (user_context);

  if (!request->url) {
    PEX_LOG_ERROR ("No SSO request URL given!");
    return false;
  }

  /* create the IPC handle */
  PulseIPCHandle * handle = pulse_ipc_new (PEXNINJA_SSO_IPC_NAME, 16 * 1024);

  /* spawn the browser */
  ShellExecute (NULL, "open", request->url, NULL, NULL, SW_SHOWNORMAL);

  /* and wait for the token for 60 seconds, otherwise, timeout! */
  char * token = NULL;
  PulseError err = pulse_ipc_read_line (handle, &token, NULL, 1 * 1000, 60 * 1000);
  if (err == PULSE_SUCCESS && token) {
    set_sso_token->func (set_sso_token->context, (const char *)token);
  }

  pulse_ipc_free (handle);

  return err == PULSE_SUCCESS;
}

/* copied from PULSE */
static char *
_pexninja_extract_sso_token (const char * token_arg)
{
  assert (token_arg);
  assert (pex_str_has_prefix (token_arg, "pexip-auth://"));
  printf ("DEBUG: Attempting to extract token arg: %s\n", token_arg);

  /* Split on the first '=' into method and value. */
  const char * eq = strchr (token_arg, '=');
  if (eq == NULL) {
    printf ("ERROR: Failed to parse token arg, could not find '=' separator to split on.\n");
    return NULL;
  }

  std::string method (token_arg, (size_t) (eq - token_arg));
  const char * value = eq + 1;

  char * token = NULL;
  if (method.find ("saml") == std::string::npos || method.find ("token") == std::string::npos) {
    printf ("ERROR: Failed to parse token arg, unexpected method %s.\n", method.c_str ());
  } else {
    token = strdup (value);
    printf ("DEBUG: Token: %s\n", token);
  }

  return token;
}

static HRESULT
_pexninja_register_app_url_handler (const char * exe_path)
{
  HRESULT hr;
  HKEY key;
  TCHAR tmp[MAX_PATH];

  hr = RegCreateKey (HKEY_CURRENT_USER, (LPCTSTR) "Software\\Classes\\pexip-auth", &key);
  if (!SUCCEEDED (hr)) {
    return hr;
  }

  wsprintf (tmp, TEXT ("URL:Pexip Authentication Protocol"));
  hr = RegSetValueEx (key, TEXT (""), 0L, REG_SZ, (CONST BYTE *)tmp, sizeof (TCHAR) * (lstrlen (tmp) + 1));
  if (!SUCCEEDED (hr)) {
    return hr;
  }

  wsprintf (tmp, TEXT (""));
  hr = RegSetValueEx (key, TEXT ("URL Protocol"), 0L, REG_SZ, (CONST BYTE *)tmp, sizeof (TCHAR) * (lstrlen (tmp) + 1));
  RegCloseKey (key);
  if (!SUCCEEDED (hr)) {
    return hr;
  }

  hr = RegCreateKey (HKEY_CURRENT_USER, (LPCTSTR) "Software\\Classes\\pexip-auth\\DefaultIcon", &key);
  if (!SUCCEEDED (hr)) {
    return hr;
  }

  hr = RegSetValueEx (key, TEXT (""), 0L, REG_SZ, (CONST BYTE *)exe_path, sizeof (TCHAR) * (lstrlen (exe_path) + 1));
  RegCloseKey (key);
  if (!SUCCEEDED (hr)) {
    return hr;
  }

  hr = RegCreateKey (HKEY_CURRENT_USER, (LPCTSTR) "Software\\Classes\\pexip-auth\\shell\\open\\command", &key);
  if (!SUCCEEDED (hr)) {
    return hr;
  }

  wsprintf (tmp, TEXT ("\"%s\" %%1"), exe_path);
  hr = RegSetValueEx (key, TEXT (""), 0L, REG_SZ, (CONST BYTE *)tmp, sizeof (TCHAR) * (lstrlen (tmp) + 1));
  RegCloseKey (key);
  return hr;
}

static HRESULT
_pexninja_deregister_app_url_handler ()
{
  HRESULT hr;
  HKEY key;

  hr = RegCreateKey (HKEY_CURRENT_USER, (LPCTSTR) "Software\\Classes\\pexip-auth", &key);
  if (!SUCCEEDED (hr)) {
    return hr;
  }

  hr = RegDeleteTreeA (key, NULL);
  RegCloseKey (key);
  if (!SUCCEEDED (hr)) {
    return hr;
  }

  hr = RegCreateKey (HKEY_CURRENT_USER, (LPCTSTR) "Software\\Classes", &key);
  if (!SUCCEEDED (hr)) {
    return hr;
  }

  hr = RegDeleteKey (key, "pexip-auth");
  RegCloseKey (key);

  return hr;
}

#endif /* defined (HOST_WINDOWS) */

static void
_logger_callback (void * user_context, PulseDebugLevel level, const char * category, int64_t wall_time_us,
                  [[maybe_unused]] int64_t elapsed_nano, unsigned int pid, const char * file, const char * function,
                  int line, const char * object_debug_str, const char * message)
{
  FILE * log_file = user_context ? (FILE *)user_context : stdout;

#define CAT_FMT "%20s %s:%d:%s:%s"
#define PTR_FMT "%14p"

#ifdef HOST_WINDOWS
#define FPRINTF_DEBUG _pulse_native_debug_fprintf
#define FFLUSH_DEBUG(f) ((void)(f))
#else
#define FPRINTF_DEBUG fprintf
#define FFLUSH_DEBUG(f) fflush (f)
#endif

#define PRINT_FORMAT "02d:%02d:%02d.%06d %5u " PTR_FMT " %s " CAT_FMT " %s\n"

  /* Break the microsecond wall-clock timestamp down into local time. */
  time_t secs = (time_t) (wall_time_us / 1000000);
  int microseconds = (int) (wall_time_us % 1000000);
  struct tm tm_local;
#if defined(HOST_WINDOWS)
  localtime_s (&tm_local, &secs);
#else
  localtime_r (&secs, &tm_local);
#endif

  /* no color, all platforms */
  FPRINTF_DEBUG (log_file, "%" PRINT_FORMAT, tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec, microseconds, pid,
                 pex_thread_self (), pulse_type_mapping_debug_level_to_string (level), category, file, line, function,
                 object_debug_str ? object_debug_str : "", message);
  FFLUSH_DEBUG (log_file);
}

static int
_pexninja_storage_set_callback (const char * key, const char * value, void * user_context)
{
  assert (key);
  assert (user_context);
  PexNinja * application = (PexNinja *)user_context;

  /* These is a very quick and dirty implementation solely for testig that this feature works.
     We should encrypt the data before storing, and we shold honor the key.
  */

  char * ptr = NULL;
  if (pex_str_has_prefix (key, "reg/auth_key/"))
    ptr = application->config.auth_token;
  else if (pex_str_has_prefix (key, "reg/display_name/"))
    ptr = application->config.display_name;
  else if (pex_str_has_prefix (key, "reg/exchange_token_failure_count/"))
    ptr = application->config.exchange_token_failure_count;
  if (ptr == NULL) {
    PEX_LOG_ERROR ("Unhandled key prefix: %s", key);
    assert (false);
  }

  int ret = 0;
  if (value == NULL) {
    ptr[0] = 0;
  } else if (strlen (value) >= 1024) {
    /* This size check is here solely because of the way pexninja is implementing storage (which is horrible) */
    ret = -1;
  } else {
    strncpy (ptr, value, 1024);
  }

  _write_config_if_needed (application);
  return ret;
}

static int
_pexninja_storage_get_callback (const char * key, char * buf, int buf_len, void * user_context)
{
  assert (key);
  assert (user_context);
  PexNinja * application = (PexNinja *)user_context;

  /* These is a very quick and dirty implementation solely for testig that this feature works.
     We should encrypt the data before storing, and we shold honor the key.
  */

  char * ptr = NULL;
  if (pex_str_has_prefix (key, "reg/auth_key/"))
    ptr = application->config.auth_token;
  else if (pex_str_has_prefix (key, "reg/display_name/"))
    ptr = application->config.display_name;
  else if (pex_str_has_prefix (key, "reg/exchange_token_failure_count/"))
    ptr = application->config.exchange_token_failure_count;
  if (ptr == NULL) {
    PEX_LOG_ERROR ("Unhandled key prefix: %s", key);
    assert (false);
  }

  int ret = 0; // Signal not found
  int ptr_len = (int)strlen (ptr);
  if (ptr_len > 0) {
    ret = ptr_len;
    if (buf != NULL) {
      if (buf_len <= ret) {
        ret = -1; // Signal buffer too short.
      } else {
        strncpy (buf, ptr, buf_len);
      }
    }
  }

  return ret;
}

static bool
_version_callback (uint64_t server_major_version, uint64_t server_minor_version, void * user_context)
{
  assert (user_context);
  PEX_LOG_INFO ("Pulse reports that we're connecting to a server with version (%" PRIu64 ".%" PRIu64
            ").",
            server_major_version, server_minor_version);
  return true;
}

static void
_conference_status_info_callback (const PulseConferenceStatusInfo * status_info, void * user_context)
{
  assert (status_info);
  assert (user_context);
  PexNinja * application = (PexNinja *)user_context;

  if (application->state.conn_status != status_info->status) {
    application->state.update_window_title = true;
  }

  application->state.is_blocked = status_info->is_blocked;
  application->state.conn_status = status_info->status;
  application->state.current_service_type = status_info->current_service_type;

  if (status_info->status == PULSE_CONNECTION_STATUS_CONNECTED) {
    PEX_LOG_DEBUG ("CONFERENCE_STATE: '%s' blocked:%s, service_type:%s",
               pulse_type_mapping_connection_status_to_string (status_info->status),
               status_info->is_blocked ? "true" : "false",
               pulse_type_mapping_service_type_to_string (status_info->current_service_type));
  } else {
    PEX_LOG_DEBUG ("CONFERENCE_STATE: '%s' blocked:%s service_type:(n/a)",
               pulse_type_mapping_connection_status_to_string (status_info->status),
               status_info->is_blocked ? "true" : "false");
  }

  const char * reconnecting_msg = "Attempting to reconnect to the conference";
  const char * reconnected_msg = "Successfully reconnected to the conference";
  if (status_info->status == PULSE_CONNECTION_STATUS_RECONNECTING) {
    register_alarm (application, reconnecting_msg, 0, red_color);
  } else {
    if (cancel_alarm (application, reconnecting_msg, 0)) {
      register_alarm (application, reconnected_msg, 3, green_color);
    }
  }
}

static void
_registration_status_info_callback (const PulseRegistrationStatusInfo * status_info, void * user_context)
{
  assert (status_info);
  assert (user_context);
  PexNinja * application = (PexNinja *)user_context;
  assert (application);

  application->state.reg_status = status_info->status;
  if (status_info->status == PULSE_CONNECTION_STATUS_RECONNECTING && status_info->next_reconnect_secs > 0) {
    PEX_LOG_DEBUG ("REGISTRATION_STATE: '%s' next reconnect in %" PRIu64 " seconds",
               pulse_type_mapping_connection_status_to_string (status_info->status), status_info->next_reconnect_secs);
  } else {
    PEX_LOG_DEBUG ("REGISTRATION_STATE: '%s'", pulse_type_mapping_connection_status_to_string (status_info->status));
  }

  const char * reconnecting_msg = "Attempting to reconnect to the registration server";
  const char * reconnected_msg = "Successfully reconnected to the registration server";
  if (status_info->status == PULSE_CONNECTION_STATUS_RECONNECTING) {
    register_alarm (application, reconnecting_msg, 0, red_color);
  } else {
    if (cancel_alarm (application, reconnecting_msg, 0)) {
      register_alarm (application, reconnected_msg, 3, green_color);
    }
  }
}

static void
_network_status_info_callback (const PulseNetworkStatusInfo * status_info, void * user_context)
{
  assert (status_info);
  assert (user_context);
  PexNinja * application = (PexNinja *)user_context;
  assert (application);

  const char * ipv4_address = (status_info->has_ipv4_address ? status_info->ipv4_address : "<unconfigured>");
  const char * ipv6_address = (status_info->has_ipv6_address ? status_info->ipv6_address : "<unconfigured>");

  PEX_LOG_DEBUG ("Got networkstatusinfo cb, state:%d (%s) metered:%d routing:%d dns:%d ip4:%s ip6:%s",
             status_info->connectivity, status_info->connectivity_desc, status_info->is_metered,
             status_info->routing_alive, status_info->dns_alive, ipv4_address, ipv6_address);

  memcpy (&application->state.network_status, status_info, sizeof (PulseNetworkStatusInfo));

  static const char * msg_curr = NULL;
  bool msg_removed = cancel_alarm (application, msg_curr, 0);
  msg_curr = NULL;

  ImVec4 color = clear_color;
  uint32_t display_seconds = 5;
  bool do_register_alarm = true;

  switch (status_info->connectivity) {
    case PULSE_NETWORK_CONNECTIVITY_UNKNOWN:
    case PULSE_NETWORK_CONNECTIVITY_LOCAL:
      msg_curr = "Network connectivity was lost!";
      color = red_color;
      display_seconds = 0;
      break;
    case PULSE_NETWORK_CONNECTIVITY_LIMITED:
    case PULSE_NETWORK_CONNECTIVITY_PORTAL:
      msg_curr = "Network connectivity is limited!";
      color = yellow_color;
      display_seconds = 0;
      break;
    case PULSE_NETWORK_CONNECTIVITY_FULL:
      do_register_alarm = msg_removed;
      msg_curr = "Network connectivity restored.";
      color = green_color;
      break;
  }

  if (do_register_alarm) {
    register_alarm (application, msg_curr, display_seconds, color);
  }
}

static void
_audio_unmute_approval_callback_complete (PexNinja * application, bool accept)
{
  application->audio_unmute.accept = accept;
  application->audio_unmute.popup_state = POPUP_STATE_HIDDEN;
}

static bool
_audio_unmute_approval_callback (void * user_context)
{
  assert (user_context);

  PexNinja * application = (PexNinja *)user_context;
  application->audio_unmute.popup_state = POPUP_STATE_QUEUED;

  while (application->audio_unmute.popup_state != POPUP_STATE_HIDDEN) {
    std::this_thread::sleep_for (100ms);
  }

  return application->audio_unmute.accept;
}

static void
_audio_mute_state_changed_callback (bool client_mute_state, bool server_mute_state, void * user_context)
{
  assert (user_context);

  PexNinja * application = (PexNinja *)user_context;
  (void)application;

  PEX_LOG_DEBUG ("Mute state change: client_muted:%s server_muted:%s", client_mute_state ? "true" : "false",
             server_mute_state ? "true" : "false");
}

static void
_tls_degrade_approval_callback_complete (PexNinja * application, bool accept)
{
  application->tls_degrade.accept = accept;
  application->tls_degrade.popup_state = POPUP_STATE_HIDDEN;
}

static bool
_tls_degrade_approval_callback (const char * host, const char * reason, void * user_context)
{
  assert (host);
  assert (reason);
  assert (user_context);

  PexNinja * application = (PexNinja *)user_context;
  application->tls_degrade.host = host;
  application->tls_degrade.reason = reason;
  application->tls_degrade.popup_state = POPUP_STATE_QUEUED;

  while (application->tls_degrade.popup_state != POPUP_STATE_HIDDEN) {
    std::this_thread::sleep_for (100ms);
  }

  return application->tls_degrade.accept;
}

static void
_pin_code_request_callback_complete (PexNinja * application, bool accept)
{
  application->pin_code_request.accept = accept;
  application->pin_code_request.popup_state = POPUP_STATE_HIDDEN;
}

static bool
_pin_code_request_callback (bool guest_pin_required, const PulseSetPinCode * pin_set_pin_code, void * user_context)
{
  assert (pin_set_pin_code);
  assert (pin_set_pin_code->func);
  assert (user_context);

  PexNinja * application = (PexNinja *)user_context;
  memset (&application->pin_code_request, 0, sizeof (struct PexNinjaPinCodeRequest));
  application->pin_code_request.guest_pin_required = guest_pin_required;
  application->pin_code_request.popup_state = POPUP_STATE_QUEUED;

  while (application->pin_code_request.popup_state != POPUP_STATE_HIDDEN) {
    std::this_thread::sleep_for (100ms);
  }

  if (application->pin_code_request.accept) {
    const char * pin_code =
      (strlen (application->pin_code_request.pin_code) == 0) ? NULL : application->pin_code_request.pin_code;
    pin_set_pin_code->func (pin_set_pin_code->context, pin_code);
  }

  return application->pin_code_request.accept;
}

static void
_conference_extension_request_callback_complete (PexNinja * application, bool accept)
{
  application->conference_extension_request.accept = accept;
  application->conference_extension_request.popup_state = POPUP_STATE_HIDDEN;
}

static bool
_conference_extension_request_callback (const PulseSetConferenceExtension * set_conference_extension,
                                        void * user_context)
{
  assert (set_conference_extension);
  assert (set_conference_extension->func);
  assert (user_context);

  PexNinja * application = (PexNinja *)user_context;

  memset (&application->conference_extension_request, 0, sizeof (struct PexNinjaConferenceExtensionRequest));
  application->conference_extension_request.popup_state = POPUP_STATE_QUEUED;

  while (application->conference_extension_request.popup_state != POPUP_STATE_HIDDEN) {
    while (application->conference_extension_request.popup_state != POPUP_STATE_HIDDEN) {
      std::this_thread::sleep_for (100ms);
    }

    if (application->conference_extension_request.accept) {
      const char * conference_extension = (strlen (application->conference_extension_request.conference_extension) == 0)
                                            ? NULL
                                            : application->conference_extension_request.conference_extension;
      if (set_conference_extension->func (set_conference_extension->context, conference_extension) == false) {
        // We attempted to set an invalid value!
        application->conference_extension_request.accept = false;
        application->conference_extension_request.popup_state = POPUP_STATE_QUEUED;
      }
    }
  }

  return application->conference_extension_request.accept;
}

static void
_registrations_event_incoming_async_result_callback (PulseError err, void * user_context)
{
  PexNinja * application = (PexNinja *)user_context;
  if (err == PULSE_SUCCESS) {
    application->state.update_window_title = true;
  } else {
    if (application->state.async_op.err != PULSE_ERROR_PROCESS_ABORTED)
      application->state.error_msg = pulse_strerror (err);
    PEX_LOG_DEBUG ("Incoming setup error: %s", pulse_strerror (err));
  }

  application->state.wait = false;
}

static void
_registrations_event_incoming_callback_complete (PexNinja * application, bool accept)
{
  application->incoming_call.accept = accept;
  application->incoming_call.event = NULL;
  application->incoming_call.popup_state = POPUP_STATE_HIDDEN;
}

static bool
_registrations_event_incoming_callback (const PulseRegistrationsEventIncoming * event, void * user_context,
                                        PulseAsyncOperationResultCallbackConfig * result_callback_config,
                                        PulseOperationProgressCallbackConfig * progress_callback_config)
{
  assert (event);
  PEX_LOG_DEBUG ("INCOMING_CALLBACK: source_type:%d conference:%s remote_dn:%s local_alias:%s remote_alias:%s",
             event->source_type, event->conference_alias, event->remote_display_name, event->local_alias,
             event->remote_alias);

  PexNinja * application = (PexNinja *)user_context;
  application->incoming_call.event = event;
  application->incoming_call.popup_state = POPUP_STATE_QUEUED;

  while (application->incoming_call.popup_state != POPUP_STATE_HIDDEN) {
    std::this_thread::sleep_for (100ms);
  }

  if (application->incoming_call.accept == true) {
    result_callback_config->func = _registrations_event_incoming_async_result_callback;
    result_callback_config->user_context = user_context;

    progress_callback_config->func = _progress_callback_conference;
    progress_callback_config->user_context = user_context;

    /* If we accept an incoming call, reset and return other connection related callbacks */
    _sso_selection_callback_complete (application, -1, true);
    _pin_code_request_callback_complete (application, false);
    _conference_extension_request_callback_complete (application, false);
    _tls_degrade_approval_callback_complete (application, false);
    _audio_unmute_approval_callback_complete (application, false);
  }
  return application->incoming_call.accept;
}

static void
_registrations_event_incoming_cancelled_callback (const PulseRegistrationsEventIncomingCancelled * event,
                                                  void * user_context)
{
  assert (event);
  PEX_LOG_DEBUG ("INCOMING_CANCELLED_CALLBACK: source_type:%d conference:%s remote_dn:%s local_alias:%s "
             "remote_alias:%s\n",
             event->source_type, event->conference_alias, event->remote_display_name, event->local_alias,
             event->remote_alias);
  PexNinja * application = (PexNinja *)user_context;
  application->incoming_call_cancelled.origin = std::string (event->remote_alias);
  application->incoming_call_cancelled.popup_state = POPUP_STATE_QUEUED;
}

static void
_server_event_conference_update_callback (PulseRoomId room_id, const PulseConferenceEventConferenceUpdate * event,
                                          void * user_context)
{
  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;
  std::lock_guard<std::mutex> lock (application->room_list.mutex);

  PexNinjaRoom * room = application->room_list.room_map[room_id];
  if (room == nullptr) {
    PEX_LOG_ERROR ("OH NOES, NO ROOM MATCHING %u", room_id);
    return;
  }

  room->conference_status.locked = event->locked;
  room->conference_status.guests_muted = event->guests_muted;
  room->conference_status.guests_can_unmute = event->guests_can_unmute;
  room->conference_status.all_muted = event->all_muted;
  room->conference_status.presentation_allowed = event->presentation_allowed;
  room->conference_status.started = event->started;
  room->conference_status.live_captions_available = event->live_captions_available;
  room->conference_status.direct_media = event->direct_media;

  room->conference_status.breakout_rooms_supported = event->breakout_rooms_supported;

  if (event->breakout_room) {
    room->conference_status.breakout_rooms.enabled = true;
    room->conference_status.breakout_rooms.name =
      event->breakout_room->name ? std::string (event->breakout_room->name) : std::string ();
    room->conference_status.breakout_rooms.description =
      event->breakout_room->description ? std::string (event->breakout_room->description) : std::string ();
    room->conference_status.breakout_rooms.end_action = event->breakout_room->end_action;
    room->conference_status.breakout_rooms.end_time = event->breakout_room->end_time;
    room->conference_status.breakout_rooms.guests_allowed_to_leave = event->breakout_room->guests_allowed_to_leave;
    room->conference_status.breakout_rooms.buzz = event->breakout_room->buzz;
    room->conference_status.breakout_rooms.buzz_time = event->breakout_room->buzz_time;
  } else {
    room->conference_status.breakout_rooms.enabled = false;
    room->conference_status.breakout_rooms.name = std::string ();
    room->conference_status.breakout_rooms.description = std::string ();
    room->conference_status.breakout_rooms.end_action = BREAKOUT_ROOM_END_ACTION_TRANSFER;
    room->conference_status.breakout_rooms.end_time = 0;
    room->conference_status.breakout_rooms.guests_allowed_to_leave = false;
    room->conference_status.breakout_rooms.buzz = false;
    room->conference_status.breakout_rooms.buzz_time = 0;
  }

  room->conference_status.pinning_config = event->pinning_config ? std::string (event->pinning_config) : std::string ();
  if (event->message_text) {
    room->conference_status.message_text.text =
      event->message_text->text ? std::string (event->message_text->text) : std::string ();
    room->conference_status.message_text.set_time =
      event->message_text->set_time ? std::string (event->message_text->set_time) : std::string ();
  }

  if (event->classification) {
    room->conference_status.classification.configured = true;
    room->conference_status.classification.current_level = event->classification->current_level->index;
    for (size_t i = 0; i < event->classification->levels.list_size; i++) {
      PulseConferenceClassificationLevel * l = &event->classification->levels.list[i];
      room->conference_status.classification.levels[l->index] = std::string (l->description);
    }
  } else {
    room->conference_status.classification.configured = false;
    room->conference_status.classification.current_level = 0;
    room->conference_status.classification.levels.clear ();
  }

  std::stringstream ss;
  ss << "Processing conference_update callback (room:" << room_id << ")";
  ss << "locked:" << (room->conference_status.locked ? "true" : "false") << " ";
  ss << "guests_muted:" << (room->conference_status.guests_muted ? "true" : "false") << " ";
  ss << "guests_can_unmute:" << (room->conference_status.guests_can_unmute ? "true" : "false") << " ";
  ss << "all_muted:" << (room->conference_status.all_muted ? "true" : "false") << " ";
  ss << "presentation_allowed:" << (room->conference_status.presentation_allowed ? "true" : "false") << " ";
  ss << "started:" << (room->conference_status.started ? "true" : "false") << " ";
  ss << "live_captions_available:" << (room->conference_status.live_captions_available ? "true" : "false") << " ";
  ss << "direct_media:" << (room->conference_status.direct_media ? "true" : "false") << " ";
  ss << "breakout_rooms_supported:" << (room->conference_status.breakout_rooms_supported ? "true" : "false") << " ";

  if (room->conference_status.breakout_rooms.enabled) {
    ss << "breakout_rooms: [ ";
    ss << "name:'" << room->conference_status.breakout_rooms.name << "' ";
    ss << "description:'" << room->conference_status.breakout_rooms.description << "' ";
    ss << "end_action:'"
       << pulse_type_mapping_breakout_room_end_action_to_string (room->conference_status.breakout_rooms.end_action)
       << "' ";
    ss << "end_time:" << room->conference_status.breakout_rooms.end_time << " ";
    ss << "guests_allowed_to_leave:"
       << (room->conference_status.breakout_rooms.guests_allowed_to_leave ? "true" : "false") << " ";
    ss << "buzz:" << (room->conference_status.breakout_rooms.buzz ? "true" : "false") << " ";
    ss << "buzz_time:" << room->conference_status.breakout_rooms.buzz_time << " ";
    ss << "] ";
  } else {
    ss << "breakout_rooms:false ";
  }
  ss << "pinning_config:'" << room->conference_status.pinning_config << "' ";
  ss << "message_text: [text:'" << room->conference_status.message_text.text << "' set_time:'"
     << room->conference_status.message_text.set_time << "'] ";
  if (room->conference_status.classification.configured) {
    ss << "levels: [ ";

    std::map<size_t, std::string>::iterator it = room->conference_status.classification.levels.begin ();
    while (it != room->conference_status.classification.levels.end ()) {
      ss << it->first << ":'" << it->second << "' ";
      ++it;
    }
    ss << "] current_level:" << room->conference_status.classification.current_level;
  } else {
    ss << "classification: <unset>";
  }

  ss << std::endl;
  PEX_LOG_DEBUG ("%s", ss.str ().c_str ());
}

static void
_server_event_participant_update_callback (PulseRoomId room_id, PulseConferenceEventParticipantList * list,
                                           void * user_context)
{
  (void)room_id;
  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  if (room == nullptr) {
    PEX_LOG_ERROR ("OH NOES, NO ROOM MATCHING %u", room_id);
    return;
  }

  // First update our own internal state, in case someone has tweaked our mute/buzz state
  // remotely.

  for (int i = 0; i < (int)list->participant_list_size; i++) {
    PulseConferenceControlParticipantEntry * participant = list->participant_list[i];
    if (participant->is_local_participant) {
      if (application->state.audio_temporarily_unmuted == false)
        application->state.audio_mute = participant->is_muted | participant->is_client_muted;
      application->state.video_mute = participant->is_video_muted;
      application->state.buzz = participant->buzz_time != 0;
      break;
    }
  }

  // Find number of active participants in this room.
  uint32_t active_participants = 0;
  for (int i = 0; i < (int)list->participant_list_size; i++) {
    if (list->participant_list[i]->is_active_participant == false)
      continue;
    active_participants++;
  }

  // Update messaging recepient
  if (application->room_list.current_room &&
      application->room_list.current_room->chat_messages.selected_message_recepient_uuid) {
    bool found = false;
    for (int i = 0; i < (int)list->participant_list_size; i++) {
      if (pex_strcmp0 (list->participant_list[i]->uuid,
                     application->room_list.current_room->chat_messages.selected_message_recepient_uuid) == 0) {
        found = true;
        application->room_list.current_room->chat_messages.selected_message_recepient_name =
          list->participant_list[i]->active_display_name;
        break;
      }
    }
    if (!found) {
      free (application->room_list.current_room->chat_messages.selected_message_recepient_uuid);
      application->room_list.current_room->chat_messages.selected_message_recepient_uuid = NULL;
      application->room_list.current_room->chat_messages.selected_message_recepient_name = NULL;
    }
  }

  pulse_conference_control_free_participant_list (room->roster_list.data);
  room->roster_list.data = list;
  room->roster_list.active_participants = active_participants;
  PEX_LOG_DEBUG ("Updated rosterlist (serial:%d) for room %u with %u active participants (%d total participants)",
             list->serial, room_id, active_participants, (int)list->participant_list_size);

  if (room->roster_list.filtered_data) {
    pulse_conference_control_free_participant_list (room->roster_list.filtered_data);

    if (room->roster_list.data && application->state.roster_list.search_filter[0] != 0) {
      room->roster_list.filtered_data =
        pulse_participant_list_search (room->roster_list.data, application->state.roster_list.search_filter, NULL);
    } else {
      room->roster_list.filtered_data = NULL;
    }
  }
}

static void
_server_event_participant_create_callback (PulseRoomId room_id, const PulseConferenceEventParticipantCreate * event,
                                           void * user_context)
{
  // TODO: handle room_id properly in ALL callbacks!
  (void)room_id;
  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  if (room == nullptr) {
    PEX_LOG_ERROR ("OH NOES, NO ROOM MATCHING %u", room_id);
    return;
  }
  const char * display_name = NULL;
  uint64_t start_time = 0;
  for (int i = 0; i < (int)room->roster_list.data->participant_list_size; i++) {
    if (event->uuid && strcmp (room->roster_list.data->participant_list[i]->uuid, event->uuid) == 0) {
      display_name = room->roster_list.data->participant_list[i]->display_name;
      start_time = room->roster_list.data->participant_list[i]->start_time;
    }
  }

  if (display_name) {
    std::time_t ts;
    if (start_time) {
      ts = static_cast<std::time_t> (start_time);
    } else {
      ts = std::time (nullptr);
    }
    const std::tm calendar_time = *std::localtime (std::addressof (ts));

    std::stringstream ss;
    ss << "[" << std::setfill ('0') << std::setw (2) << calendar_time.tm_hour << ":" << std::setfill ('0')
       << std::setw (2) << calendar_time.tm_min << ":" << std::setfill ('0') << std::setw (2) << calendar_time.tm_sec
       << "] " << "** " << display_name << " has joined ** " << std::endl;

    if (event->is_state_sync) {
      room->chat_messages.sync_join_messages.append (ss.str ());
    } else {
      room->chat_messages.chat_messages.append (ss.str ());
    }
    room->chat_messages.concat_chat_messages =
      room->chat_messages.sync_join_messages + "\n" + room->chat_messages.chat_messages;
  }
  PEX_LOG_DEBUG ("Added join message for participant:%s in room %u", display_name, room_id);
}

static void
_server_event_participant_delete_callback (PulseRoomId room_id, const PulseConferenceEventParticipantDelete * event,
                                           void * user_context)
{
  // TODO: handle room_id properly in ALL callbacks!
  (void)room_id;
  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  if (room == nullptr) {
    PEX_LOG_ERROR ("OH NOES, NO ROOM MATCHING %u", room_id);
    return;
  }
  const char * display_name = NULL;
  for (int i = 0; i < (int)room->roster_list.data->participant_list_size; i++) {
    if (event->uuid && strcmp (room->roster_list.data->participant_list[i]->uuid, event->uuid) == 0) {
      display_name = room->roster_list.data->participant_list[i]->display_name;
    }
  }

  if (display_name) {
    const std::time_t now = std::time (nullptr);
    const std::tm calendar_time = *std::localtime (std::addressof (now));

    std::stringstream ss;
    ss << "[" << std::setfill ('0') << std::setw (2) << calendar_time.tm_hour << ":" << std::setfill ('0')
       << std::setw (2) << calendar_time.tm_min << ":" << std::setfill ('0') << std::setw (2) << calendar_time.tm_sec
       << "] " << "** " << display_name << " has left **" << std::endl;
    room->chat_messages.chat_messages.append (ss.str ());
    room->chat_messages.concat_chat_messages =
      room->chat_messages.sync_join_messages + "\n" + room->chat_messages.chat_messages;
  }
  PEX_LOG_DEBUG ("Added leave message for participant:%s in room %u", display_name, room_id);
}

static void
_server_event_message_received_callback (PulseRoomId room_id, const PulseConferenceEventMessageReceived * event,
                                         void * user_context)
{
  (void)room_id;
  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  if (room == nullptr) {
    PEX_LOG_ERROR ("OH NOES, NO ROOM MATCHING %u", room_id);
    return;
  }

  const std::time_t now = std::time (nullptr);
  const std::tm calendar_time = *std::localtime (std::addressof (now));

  std::stringstream ss;
  ss << "[" << std::setfill ('0') << std::setw (2) << calendar_time.tm_hour << ":" << std::setfill ('0')
     << std::setw (2) << calendar_time.tm_min << ":" << std::setfill ('0') << std::setw (2) << calendar_time.tm_sec
     << "] " << event->origin << "(" << (event->direct ? "direct" : "broadcast") << ") : " << event->payload
     << std::endl;
  room->chat_messages.chat_messages.append (ss.str ());
  room->chat_messages.concat_chat_messages =
    room->chat_messages.sync_join_messages + "\n" + room->chat_messages.chat_messages;
  room->chat_messages.chat_messages_unread++;
  PEX_LOG_DEBUG ("Added chat message in room %u", room_id);
}

static void
_server_event_presentation_start_callback (PulseRoomId room_id, const PulseConferenceEventPresentationStart * event,
                                           void * user_context)
{
  // TODO: handle room_id properly in ALL callbacks!
  (void)room_id;
  assert (user_context != NULL);

  PEX_LOG_DEBUG ("*** PRESENTATION START: name:'%s' uri:'%s' uuid:'%s'\n", event->presenter_name, event->presenter_uri,
             event->presenter_uuid);

  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  if (room == nullptr) {
    PEX_LOG_ERROR ("OH NOES, NO ROOM MATCHING %u", room_id);
    return;
  }

  room->presentation.preso_started = true;
  room->presentation.preso_render = true;
  snprintf (room->presentation.presenter_name, sizeof (room->presentation.presenter_name), "%s", event->presenter_name);
}

static void
_server_event_presentation_stop_callback (PulseRoomId room_id, void * user_context)
{
  // TODO: handle room_id properly in ALL callbacks!
  (void)room_id;
  assert (user_context != NULL);

  PEX_LOG_DEBUG ("*** PRESENTATION STOP\n");

  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  if (room == nullptr) {
    PEX_LOG_ERROR ("OH NOES, NO ROOM MATCHING %u", room_id);
    return;
  }
  room->presentation.preso_started = false;
}

static void
_server_event_remote_disconnect_callback (const char * reason, void * user_context)
{
  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;

  application->disconnected.reason = std::string (reason);
  application->disconnected.popup_state = POPUP_STATE_QUEUED;

  application->state.window_title = std::string ("PexNinja (Build " VERSION ")");
  application->state.update_window_title = true;

  PEX_LOG_DEBUG ("*** REMOTE DISCONNECT: reason:'%s'\n", reason);
}

static void
_server_event_layout_callback (PulseRoomId room_id, const PulseConferenceEventLayout * event, void * user_context)
{
  (void)room_id;
  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;

  application->state.layout.layout = event->layout;
  application->state.layout.enable_overlay_text = event->overlay_text_enabled;

  PEX_LOG_DEBUG ("*** LAYOUT:'%s' request_layout:[primary_screen:[chair:'%s' "
             "guest:'%s']] overlay_text_enabled:%s participants:%u\n",
             event->layout, event->requested_layout.primary_screen.chair, event->requested_layout.primary_screen.guest,
             event->overlay_text_enabled ? "true" : "false", event->participants_num);
  for (uint32_t i = 0; i < event->participants_num; i++) {
    PEX_LOG_DEBUG ("*** LAYOUT PARTICIPANT uuid:%s\n", event->participants[i]);
  }
}

static void
_server_event_stage_callback (PulseRoomId room_id, const PulseConferenceEventStage * event, void * user_context)
{
  assert (user_context != NULL);

  PEX_LOG_DEBUG ("*** STAGE: room_id:%u speakers:%u\n", room_id, event->speakers_num);
  for (uint32_t i = 0; i < event->speakers_num; i++) {
    PEX_LOG_DEBUG ("*** STAGE stage_index:%u vad:%u participant_uuid:'%s'\n", event->speakers[i]->stage_index,
               event->speakers[i]->vad, event->speakers[i]->participant_uuid);
  }
}

static void
_server_event_live_captions_callback (PulseRoomId room_id, const PulseConferenceEventLiveCaptions * event,
                                      void * user_context)
{
  PEX_LOG_DEBUG ("*** Live captions: room_id:%u '%s' [final:%s]\n", room_id, event->data,
             event->is_final ? "True" : "False");

  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  if (room == nullptr) {
    PEX_LOG_ERROR ("OH NOES, NO ROOM MATCHING %u", room_id);
    return;
  }

  if (event->is_final) {
    room->live_captions.live_caption_final.append (event->data);
    room->live_captions.live_caption_final.append ("\n");
    room->live_captions.live_caption_display.assign (room->live_captions.live_caption_final);
  } else {
    room->live_captions.live_caption_display.assign (room->live_captions.live_caption_final);
    room->live_captions.live_caption_display.append (event->data);
  }
}

static void
_server_event_audio_mixer_list_callback (PulseRoomId room_id, PulseConferenceAudioMixersList * list,
                                         void * user_context)
{
  assert (list != NULL);
  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  if (room == nullptr) {
    PEX_LOG_ERROR ("OH NOES, NO ROOM MATCHING %u", room_id);
    return;
  }

  pulse_conference_control_free_audio_mixers_list (room->audio_mixers_list.data);
  room->audio_mixers_list.data = list;
}

static void
_server_event_breakout_room_pre_transfer_callback (PulseRoomId room_id, const char * breakout_room_name,
                                                   void * user_context)
{
  assert (user_context != NULL);
  PexNinja * application = (PexNinja *)user_context;

  application->referal_request.breakout_name = std::string (breakout_room_name);
  application->referal_request.room_id = room_id;
  application->referal_request.popup_state = POPUP_STATE_QUEUED;

  for (int i = 15; i > 0; i--) {
    application->referal_request.timeout = i;
    if (application->referal_request.popup_state == POPUP_STATE_HIDDEN) {
      PEX_LOG_DEBUG ("Stopping countdown, user accepted");
      break;
    }

    if (application->room_list.transfer_cancel_flagged) {
      application->room_list.transfer_cancel_flagged = false;
      std::lock_guard<std::mutex> lock (application->room_list.mutex);
      if (application->room_list.transfer_cancel_id == room_id) {
        application->room_list.transfer_cancel_id = (uint32_t)-1;
        application->referal_request.popup_state = POPUP_STATE_HIDDEN;
        PEX_LOG_ERROR ("Stopping coutdown, transfer was cancelled!");
        return;
      }
    }
    std::this_thread::sleep_for (std::chrono::seconds (1));
  }

  application->referal_request.popup_state = POPUP_STATE_HIDDEN;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  assert (room);

  // Call whatever function needed here, to inform aobut the imminent transfer.

  application->room_list.current_room_id = room_id;
  application->room_list.current_room = room;

  PEX_LOG_DEBUG ("*** _server_event_breakout_room_pre_transfer_callback: room_id:%u name:%s", room_id, breakout_room_name);
}

static void
_server_event_breakout_room_post_transfer_callback (PulseRoomId room_id, void * user_context)
{
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  PexNinjaRoom * room = application->room_list.room_map[room_id];
  assert (room);
  assert (application->room_list.current_room == room);
  assert (application->room_list.current_room_id == room_id);

  PulseSessionInfo * info = NULL;
  if (pulse_session_get_conference_info (application->client, &info) == PULSE_SUCCESS) {
    std::stringstream ss;
    ss << "PexNinja : " << info->conference_name;
    if (info->breakout_name)
      ss << " (" << info->breakout_name << ")";
    ss << " [" << info->conference_alias << "]";
    pulse_session_free_conference_info (info);

    application->state.window_title = ss.str ();
    application->state.update_window_title = true;
  }
  PEX_LOG_DEBUG ("*** _server_event_breakout_room_post_transfer_callback: room_id:%u", room_id);
}

static void
_server_event_breakout_room_transfer_cancelled_callback (PulseRoomId room_id, void * user_context)
{
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);

  application->room_list.transfer_cancel_flagged = true;
  application->room_list.transfer_cancel_id = room_id;

  PEX_LOG_ERROR ("Transfer to room_id %u was cancelled!", room_id);
}

static void
_server_event_breakout_room_created_callback (PulseRoomId room_id, void * user_context)
{
  PEX_LOG_ERROR ("*** _server_event_breakout_room_created_callback: room_id:%u", room_id);
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  assert (application->room_list.room_map.count (room_id) == 0);
  application->room_list.room_map[room_id] = new PexNinjaRoom{};

  PEX_LOG_ERROR ("*** _server_event_breakout_room_created_callback: room_id:%u", room_id);
}

static void
_server_event_breakout_room_destroyed_callback (PulseRoomId room_id, void * user_context)
{
  PexNinja * application = (PexNinja *)user_context;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  assert (application->room_list.room_map.count (room_id) == 1);

  delete application->room_list.room_map[room_id];
  application->room_list.room_map.erase (room_id);

  PEX_LOG_ERROR ("*** _server_event_breakout_room_destroyed_callback: room_id:%u", room_id);
}

static void
_server_event_fecc_callback (PulseRoomId room_id, const PulseConferenceEventFecc * event, void * user_context)
{
  (void)room_id;
  PexNinja * application = (PexNinja *)user_context;

  PEX_LOG_ERROR (" Got FECC action:%u movement:%u timeout_ms:%u", event->action, event->movement_direction_mask,
             event->timeout_ms);

  if (application->config.options.use_pulse_internal_ptz) {
    pulse_device_session_control_ptz (application->client, PULSE_MEDIA_CONTENT_MAIN, event->action,
                                      event->movement_direction_mask, event->timeout_ms);
  }
}

static void
init_gl_texture_ctx (GLTextureContext * ctx, PulseMediaContent media_content)
{
  glGenTextures (1, &ctx->texture);
  glBindTexture (GL_TEXTURE_2D, ctx->texture);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  ctx->media_content = media_content;
}

static std::vector<PulseDevice *> camera_devices;
static std::vector<PulseDevice *> speaker_devices;
static std::vector<PulseDevice *> microphone_devices;
static std::mutex devices_mutex;

static void
append_device (const PulseDevice * device, void * user_context)
{
  auto & devices = *reinterpret_cast<std::vector<PulseDevice *> *> (user_context);
  devices.push_back (pulse_device_copy (device));
}

typedef void (*PulseDeviceIteratorFunc) (const PulseDevice * device, void * user_context);

static std::vector<PulseDevice *>
enumerate_devices (Pulse * client, PulseMediaType media_type, PulseMediaDirection media_direction)
{
  std::vector<PulseDevice *> devices;
  PulseDeviceIterator * it = nullptr;
  pulse_device_iterator_new (client, media_type, media_direction, &it);
  if (it) {
    pulse_device_iterator_foreach (it, append_device, &devices);
    pulse_device_iterator_free (it);
  }
  return devices;
}

static std::vector<PulseDevice *>
clone_devices (std::vector<PulseDevice *> devices)
{
  std::vector<PulseDevice *> ret;
  for (auto device : devices)
    ret.push_back (pulse_device_copy (device));
  return ret;
}

const char *
get_default_system_device (std::vector<PulseDevice *> devices)
{
  const char * device_name = NULL;
  int max = (int)devices.size ();
  for (int i = 0; i < max; i++) {
    if (pulse_device_is_system_default (devices[i])) {
      device_name = pulse_device_get_name (devices[i]);
      break;
    }
  }
  return device_name;
}

static void
cleanup_devices (std::vector<PulseDevice *> devices)
{
  for (auto * device : devices) {
    pulse_device_free (device);
  }
  devices.clear ();
}

static void
on_devices_changed (PulseMediaType media_type, void * user_context)
{
  Pulse * client = reinterpret_cast<Pulse *> (user_context);
  std::lock_guard<std::mutex> lock (devices_mutex);

  if (media_type == PULSE_MEDIA_VIDEO) {
    cleanup_devices (camera_devices);
    camera_devices = enumerate_devices (client, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT);
  } else {
    cleanup_devices (speaker_devices);
    cleanup_devices (microphone_devices);

    speaker_devices = enumerate_devices (client, PULSE_MEDIA_AUDIO, PULSE_MEDIA_OUTPUT);
    microphone_devices = enumerate_devices (client, PULSE_MEDIA_AUDIO, PULSE_MEDIA_INPUT);
  }
}

static void
_window_resize_callback (GLFWwindow * window, int width, int height)
{
  assert (window);
  PEX_LOG_DEBUG ("Window resized to %dx%d", width, height);

  if (g_glfw_app_context) {
    g_glfw_app_context->state.windows.width = width;
    g_glfw_app_context->state.windows.height = height;
    if (g_glfw_app_context->client && g_glfw_app_context->state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
      pulse_participant_control_preferred_aspect_ratio_from_size (g_glfw_app_context->client, NULL, width, height);
    }
  }
}

static GLFWwindow *
create_window (PexNinja * application)
{
  GLFWwindow * window = nullptr;

  glfwSetErrorCallback (glfw_error_callback);
  if (!glfwInit ()) {
    PEX_LOG_ERROR ("Unable to initialize GLFW");
    exit (EXIT_FAILURE);
  }

  // GL 3.3 + GLSL 150
  const char * glsl_version = NULL;
#if defined(HOST_DARWIN)
  glsl_version = "#version 150";
  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
  // Create window with graphics context
  application->state.windows.width = 1280;
  application->state.windows.height = 720;
  window = glfwCreateWindow (application->state.windows.width, application->state.windows.height,
                             "PexNinja (Build " VERSION ")", NULL, NULL);
  if (window == NULL) {
    PEX_LOG_ERROR ("Unable to create Window\n");
    exit (EXIT_FAILURE);
  }

  glfwSetWindowSizeCallback (window, _window_resize_callback);

  glfwMakeContextCurrent (window);
  glfwSwapInterval (1); // Enable vsync

  // Initialize OpenGL loader
  if (gl3wInit () != 0) {
    PEX_LOG_ERROR ("Failed to initialize OpenGL loader\n");
    exit (EXIT_FAILURE);
  }
  glEnable (GL_DEPTH_TEST);
  glDepthFunc (GL_NEVER);

  // create_window Dear ImGui context
  IMGUI_CHECKVERSION ();
  ImGui::CreateContext ();
  ImPlot::CreateContext ();
  ImGuiIO & io = ImGui::GetIO ();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // do not save imgui.ini
  io.IniFilename = NULL;

  ImGui::StyleColorsDark ();

  // Pexip brand styling: Inter + the @pexip/components token palette, so this
  // client reads like the rest of the Pexip product family. Applied on top of
  // StyleColorsDark() so anything the theme doesn't set keeps a sane dark value.
  {
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale (window, &xscale, &yscale);

    PexipTheme::LoadFonts (io, PEXNINJA_FONT_DIR, xscale);
    PexipTheme::Apply ();
    PexipTheme::ApplyImPlot ();
  }

  // create_window Platform / Renderer bindings
  ImGui_ImplGlfw_InitForOpenGL (window, true);
  ImGui_ImplOpenGL3_Init (glsl_version);

  return window;
}

static void
render_gl_ctx_image (Pulse * client, GLTextureContext * ctx)
{
  // window padding
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2{0, 0});

  // and disable tab bar for the video panel:
  ImGuiWindowClass window_class;
  window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
  ImGui::SetNextWindowClass (&window_class);

  PulseDataSessionFrameData * frame = nullptr;
  PulseError err = pulse_data_session_pull_frame_data (client, PULSE_MEDIA_VIDEO, &frame, ctx->media_content, 0);

  if (frame) {
    int width = 0;
    int height = 0;
    if (pulse_frame_data_get_resolution (frame, &width, &height)) {
      glBindTexture (GL_TEXTURE_2D, ctx->texture);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame->data);
    }
    pulse_data_session_frame_data_free (frame);
  } else {
    (void)err;
    // PEX_LOG_DEBUG ("Error fetching data failed, err:%s frame:%p\n", pulse_strerror (err),
    // frame);
  }

  ImGui::Image ((ImTextureID)((uintptr_t)ctx->texture), ImVec2 (200, 113));
  ImGui::PopStyleVar ();
}

static void
render_gl_ctx_background (Pulse * client, GLTextureContext * ctx, int w, int h)
{
  PulseDataSessionFrameData * frame = nullptr;
  PulseError err = pulse_data_session_pull_frame_data (client, PULSE_MEDIA_VIDEO, &frame, ctx->media_content, 0);

  if (frame) {
    int width = 0;
    int height = 0;
    if (pulse_frame_data_get_resolution (frame, &width, &height)) {
      glBindTexture (GL_TEXTURE_2D, ctx->texture);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame->data);
    }

    pulse_data_session_frame_data_free (frame);

  } else {
    (void)err;
    // PEX_LOG_ERROR ("Error fetching data failed, err:%s frame:%p", pulse_strerror (err),
    // frame);
  }

  ImGui::GetBackgroundDrawList ()->AddImage (              //
    (ImTextureID)((uintptr_t)ctx->texture),                //
    ImVec2 (0, (float)mainmenubar_height),                 //
    ImVec2 ((float)w, (float)h - (float)bottombar_height), //
    ImVec2 (0, 0),                                         //
    ImVec2 (1, 1));
}

static inline void
network_menu (Pulse * client)
{
  static int max_tx_kbps = DEFAULT_TX_KBPS;

  ImGui::Text ("Bandwidth usage");
  ImGui::SliderInt ("Max RX/TX kbps", &max_tx_kbps, 64, MAX_TX_KBPS);
  ImGuiUtils::HelpToolTip ("CTRL + Click to input a value.");
  if (ImGui::IsItemDeactivatedAfterEdit ()) {
    pulse_set_max_bitrate (client, max_tx_kbps * 1000);
  }
}

static bool
get_pulse_device_display_name (void * data, int n, const char ** out_str)
{
  const auto & devices = *reinterpret_cast<std::vector<PulseDevice *> *> (data);
  const PulseDevice * device = devices[n];
  *out_str = pulse_device_get_name (device);
  return true;
}

static float
_lerp (float a, float b, float t)
{
  return (1.0f - t) * a + b * t;
}

static float
_inv_lerp (float a, float b, float v)
{
  return (v - a) / (b - a);
}

static float
_remap (float imin, float imax, float omin, float omax, float v)
{
  float t = _inv_lerp (imin, imax, v);
  return _lerp (omin, omax, t);
}

static void
render_microphone_audio_levels (PexNinja * application)
{
  const float mindb = -127.f;
  const float maxdb = 0.f;

  unsigned int dbi = 0;
  {
    std::lock_guard<std::mutex> lock (application->state.mic_audio_levels->mutex);
    if (!application->state.mic_audio_levels->items.empty ()) {
      dbi = application->state.mic_audio_levels->items.front ();
      application->state.mic_audio_levels->items.pop_front ();
    }
  }
  float dbf = dbi != 0 ? (float)dbi * -1.f : mindb;
  float v = _inv_lerp (mindb, maxdb, dbf);

  ImU32 r = (ImU32)_remap (mindb, maxdb, 0, 255, dbf);
  ImU32 g = (ImU32)_remap (mindb, maxdb, 255, 53, dbf);
  ImU32 b = (ImU32)_remap (mindb, maxdb, 0, 73, dbf);

  ImGui::PushStyleColor (ImGuiCol_PlotHistogram, IM_COL32 (r, g, b, 255));
  ImGui::ProgressBar (v, ImVec2 (0.0f, 10.0f), "");
  ImGui::PopStyleColor ();
}

static PulseError
_camera_hot_swap (PexNinja * application, PexNinjaState::PexNinjaVideoMix & vm, PulseDevice * camera)
{
  /* Release any existing camera input before acquiring a new one */
  if (vm.camera_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    pulse_video_mix_input_release (application->client, vm.camera_input);
    vm.camera_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
  }
  /* Acquire new one */
  PulseError err = pulse_video_mix_input_from_device (application->client, camera, &vm.camera_input);
  return err;
}
static inline void
render_device_selection (PexNinja * application)
{
  std::vector<PulseDevice *> cameras_vec;
  std::vector<PulseDevice *> mics_vec;
  std::vector<PulseDevice *> speakers_vec;

  /* work on a copy of these collections, so we don't hold the device_mutex lock when calling
   * pulse_device_session_connect_* */
  {
    std::lock_guard<std::mutex> lock (devices_mutex);
    cameras_vec = clone_devices (camera_devices);
    mics_vec = clone_devices (microphone_devices);
    speakers_vec = clone_devices (speaker_devices);
  }

  int selected_cam_idx = -1;
  int selected_mic_idx = -1;
  int selected_spk_idx = -1;

  if (application->config.devices.camera_set) {
    int max = (int)cameras_vec.size ();
    for (int i = 0; i < max; i++) {
      if (pulse_device_get_id (cameras_vec[i]) == application->config.devices.camera_id) {
        selected_cam_idx = i;
      }
    }
  }

  if (application->config.devices.microphone_set) {
    int max = (int)mics_vec.size ();
    for (int i = 0; i < max; i++) {
      if (pulse_device_get_id (mics_vec[i]) == application->config.devices.microphone_id) {
        selected_mic_idx = i;
      }
    }
  }

  if (application->config.devices.speaker_set) {
    int max = (int)speakers_vec.size ();
    for (int i = 0; i < max; i++) {
      if (pulse_device_get_id (speakers_vec[i]) == application->config.devices.speaker_id) {
        selected_spk_idx = i;
      }
    }
  }

  bool cam_changed = false;
  bool spk_changed = false;
  bool mic_changed = false;

  int prev_mic_idx = selected_mic_idx;
  int prev_spk_idx = selected_spk_idx;

  static int selected_cam_preflight_idx = -1;
  auto & vm = application->state.video_mix;
  auto & pm = application->state.preso_mix;
  /* display preflight selection in the combo if it was selected */
  if (selected_cam_idx != -1 && application->state.selected_cam_preflight_idx == -1) {
    selected_cam_preflight_idx = selected_cam_idx;
    application->state.selected_cam_preflight_idx = selected_cam_idx;

    /* connect the current camera for preflight, right away */
    pulse_device_session_connect_device (application->client, cameras_vec[selected_cam_idx],
                                         PULSE_MEDIA_CONTENT_PREFLIGHT);
  }

  cam_changed = ImGui::Combo ("Camera", &selected_cam_preflight_idx, &get_pulse_device_display_name,
                              (void *)&cameras_vec, (int)cameras_vec.size ());
  ImGui::SameLine ();

  /* only enable the save button if a change was made and it is different from the current camera */
  bool enable_save = selected_cam_idx != selected_cam_preflight_idx && selected_cam_preflight_idx != -1;
  if (!enable_save)
    imgui_begin_disabled_state ();

  if (ImGui::Button ("Save")) {
    /* this operation should not fail! camera was alrady selected for preflight so this operation doesn't change the
     * camera state */
    selected_cam_idx = selected_cam_preflight_idx;

    if (pulse_device_session_connect_device (application->client, cameras_vec[selected_cam_idx],
                                             PULSE_MEDIA_CONTENT_MAIN) == PULSE_SUCCESS) {
      application->config.devices.camera_set = true;
      application->config.devices.camera_id = pulse_device_get_id (cameras_vec[selected_cam_idx]);
      strncpy (application->config.devices.camera_name, pulse_device_get_name (cameras_vec[selected_cam_idx]),
               sizeof (application->config.devices.camera_name) - 1);
      application->config.devices.camera_name[sizeof (application->config.devices.camera_name) - 1] = 0;
      _write_config_if_needed (application);
    }
  }

  if (!enable_save)
    imgui_end_disabled_state ();

  const char * default_mic_name = get_default_system_device (mics_vec);
  const char * default_speaker_name = get_default_system_device (speakers_vec);

  if (ImGui::Checkbox ("Use system default microphone", &application->config.devices.microphone_use_default)) {
    if (application->config.devices.microphone_use_default) {
      pulse_device_session_connect_system_default (application->client, PULSE_MEDIA_CONTENT_MAIN, PULSE_MEDIA_AUDIO,
                                                   PULSE_MEDIA_INPUT);
      application->config.devices.microphone_set = false;
    }
    _write_config_if_needed (application);
  }
  ImGui::Text ("[%s]", default_mic_name);

  if (application->config.devices.microphone_use_default == false) {
    mic_changed = ImGui::Combo ("Microphone", &selected_mic_idx, &get_pulse_device_display_name, (void *)&mics_vec,
                                (int)mics_vec.size ());
  }

  render_microphone_audio_levels (application);

  if (ImGui::Checkbox ("Use system default speaker", &application->config.devices.speaker_use_default)) {
    if (application->config.devices.speaker_use_default) {
      pulse_device_session_connect_system_default (application->client, PULSE_MEDIA_CONTENT_MAIN, PULSE_MEDIA_AUDIO,
                                                   PULSE_MEDIA_OUTPUT);
      application->config.devices.speaker_set = false;
    }
    _write_config_if_needed (application);
  }
  ImGui::Text ("[%s]", default_speaker_name);

  if (application->config.devices.speaker_use_default == false) {
    spk_changed = ImGui::Combo ("Speaker", &selected_spk_idx, &get_pulse_device_display_name, (void *)&speakers_vec,
                                (int)speakers_vec.size ());
  }

  if (cam_changed && selected_cam_preflight_idx != -1) {
    pulse_device_session_connect_device (application->client, cameras_vec[selected_cam_preflight_idx],
                                         PULSE_MEDIA_CONTENT_PREFLIGHT);

    /*hot swap*/
    _camera_hot_swap (application, vm, cameras_vec[selected_cam_preflight_idx]);
    _camera_hot_swap (application, pm, cameras_vec[selected_cam_preflight_idx]);
  }

  if (mic_changed) {
    if (pulse_device_session_connect_device (application->client, mics_vec[selected_mic_idx],
                                             PULSE_MEDIA_CONTENT_MAIN) == PULSE_SUCCESS) {
      application->config.devices.microphone_set = true;
      application->config.devices.microphone_id = pulse_device_get_id (mics_vec[selected_mic_idx]);
      strncpy (application->config.devices.microphone_name, pulse_device_get_name (mics_vec[selected_mic_idx]),
               sizeof (application->config.devices.microphone_name) - 1);

      application->config.devices.microphone_name[sizeof (application->config.devices.microphone_name) - 1] = 0;
      _write_config_if_needed (application);
    } else if (prev_mic_idx != -1) {
      pulse_device_session_connect_device (application->client, mics_vec[prev_mic_idx], PULSE_MEDIA_CONTENT_MAIN);
    } else {
      /* rollback the selection if we got an error */
      selected_mic_idx = -1;
    }
  }

  if (spk_changed) {
    if (pulse_device_session_connect_device (application->client, speakers_vec[selected_spk_idx],
                                             PULSE_MEDIA_CONTENT_MAIN) == PULSE_SUCCESS) {
      application->config.devices.speaker_set = true;
      application->config.devices.speaker_id = pulse_device_get_id (speakers_vec[selected_spk_idx]);
      strncpy (application->config.devices.speaker_name, pulse_device_get_name (speakers_vec[selected_spk_idx]),
               sizeof (application->config.devices.speaker_name) - 1);
      application->config.devices.speaker_name[sizeof (application->config.devices.speaker_name) - 1] = 0;
      _write_config_if_needed (application);
    } else if (prev_spk_idx != -1) {
      pulse_device_session_connect_device (application->client, speakers_vec[prev_spk_idx], PULSE_MEDIA_CONTENT_MAIN);
    } else {
      /* rollback the selection if we got an error */
      selected_spk_idx = -1;
    }
  }
}

static void
_pulse_async_operation_result_cb (const PulseError err, void * user_context)
{
  struct async_op_data * res = (struct async_op_data *)user_context;
  res->done = true;
  res->err = err;
}

static inline bool
handle_async_operation (PexNinja * application)
{
  bool completed = (application->state.async_op.op != ASYNC_OP_NONE && application->state.async_op.done);
  if (completed) {
    application->state.abort = false;
    if (application->state.async_op.op == ASYNC_OP_CONNECT) {
      if (application->state.async_op.err != PULSE_SUCCESS) {
        PEX_LOG_DEBUG ("pulse_connect_with_rest failed: %s\n", pulse_strerror (application->state.async_op.err));
        _update_conference_status_msg (application, "Failed to connect");
        if (application->state.async_op.err != PULSE_ERROR_PROCESS_ABORTED)
          application->state.error_msg = pulse_strerror (application->state.async_op.err);
      } else {
        _set_window_title (application);
        pulse_participant_control_preferred_aspect_ratio_from_size (
          application->client, NULL, application->state.windows.width, application->state.windows.height);
        if (application->state.available_layouts) {
          pulse_conference_control_free_available_layouts_response (application->state.available_layouts);
          application->state.available_layouts = NULL;
        }
        pulse_conference_control_available_layouts (application->client, &application->state.available_layouts);

        if (application->state.layout_svgs) {
          pulse_conference_control_free_layout_svgs_response (application->state.layout_svgs);
          application->state.layout_svgs = NULL;
        }
        pulse_conference_control_layout_svgs (application->client, &application->state.layout_svgs);
      }
    } else if (application->state.async_op.op == ASYNC_OP_DISCONNECT) {
      if (application->state.async_op.err != PULSE_SUCCESS) {
        PEX_LOG_DEBUG ("pulse_disconnect failed: %s\n", pulse_strerror (application->state.async_op.err));
        _update_conference_status_msg (application, "Failed to disconnect");
        if (application->state.async_op.err != PULSE_ERROR_PROCESS_ABORTED)
          application->state.error_msg = pulse_strerror (application->state.async_op.err);
      }
      glfwSetWindowTitle (application->window, "PexNinja (Build " VERSION ")");

      std::lock_guard<std::mutex> lock (application->room_list.mutex);
      for (auto it = application->room_list.room_map.begin (); it != application->room_list.room_map.end (); it++) {
        PEX_LOG_ERROR ("Deleting room %u", it->first);
        delete it->second;
      }
      application->room_list.room_map.clear ();
      assert (application->room_list.room_map.size () == 0);
      // Recreate main room.
      application->room_list.current_room_id = PULSE_ROOM_ID_MAIN;
      application->room_list.current_room = new PexNinjaRoom{};
      application->room_list.room_map[PULSE_ROOM_ID_MAIN] = application->room_list.current_room;
    } else if (application->state.async_op.op == ASYNC_OP_REGISTER) {
      if (application->state.async_op.err != PULSE_SUCCESS) {
        PEX_LOG_DEBUG ("pulse_register failed: %s\n", pulse_strerror (application->state.async_op.err));
        _update_registration_status_msg (application, "Failed to register");
        if (application->state.async_op.err != PULSE_ERROR_PROCESS_ABORTED)
          application->state.error_msg = pulse_strerror (application->state.async_op.err);
      } else {
        application->state.registered = true;
        application->state.connection_setup_type = 0;
        // Pull alias lists immediately.
        PulseRegistrationAliasList * result = NULL;
        PulseError err = pulse_registrations_query_alias (application->client, "", 1, 1, &result);
        if (err != PULSE_SUCCESS) {
          PEX_LOG_ERROR ("Failed to pull alias list: %s", pulse_strerror (err));
        }
        pulse_registration_alias_list_free (result);
      }
    } else if (application->state.async_op.op == ASYNC_OP_DEREGISTER) {
      if (application->state.async_op.err != PULSE_SUCCESS) {
        PEX_LOG_DEBUG ("pulse_deregister failed: %s\n", pulse_strerror (application->state.async_op.err));
        _update_registration_status_msg (application, "Failed to deregister");
        if (application->state.async_op.err != PULSE_ERROR_PROCESS_ABORTED)
          application->state.error_msg = pulse_strerror (application->state.async_op.err);
      } else {
        application->state.registered = false;
      }
    } else {
      assert (0);
    }
    application->state.async_op = ASYNC_OP_DATA_INIT;
  }
  return completed;
}

static void
pulseimgui_perform_registration (PexNinja * application)
{
  _configure_proxy_server (application);

  PulseRegistrationRequest registration_params;
  registration_params.host = application->config.registration.host;
  registration_params.alias = application->config.registration.alias;
  registration_params.username =
    application->config.registration.use_sso ? NULL : application->config.registration.username;
  registration_params.password =
    application->config.registration.use_sso ? NULL : application->config.registration.password;
  registration_params.use_sso = application->config.registration.use_sso;

  PulseRegistrationsEventCallbackConfig cb_config = {
    .registrations_event_incoming_callback = _registrations_event_incoming_callback,
    .registrations_event_incoming_callback_user_context = application,
    .registrations_event_incoming_cancelled_callback = _registrations_event_incoming_cancelled_callback,
    .registrations_event_incoming_cancelled_callback_user_context = application};

  PulseAsyncOperationResultCallbackConfig async_op_cb_config = {.func = _pulse_async_operation_result_cb,
                                                                .user_context = &application->state.async_op};
  PulseOperationProgressCallbackConfig progress_config = {.func = _progress_callback_registration,
                                                          .user_context = application};

  pulse_options_set_registrations_events_callbacks (application->client, &cb_config);
  PulseError err =
    pulse_register_async (application->client, &registration_params, &async_op_cb_config, &progress_config);
  if (err != PULSE_SUCCESS) {
    PEX_LOG_DEBUG ("pulse_register failed: %s\n", pulse_strerror (err));
    if (err != PULSE_ERROR_PROCESS_ABORTED)
      application->state.error_msg = pulse_strerror (err);
    application->state.registered = false;
  } else {
    application->state.async_op.op = ASYNC_OP_REGISTER;
    application->state.windows.show_registration = false;
  }
}

static int
_update_conference_alias_list_cb (ImGuiInputTextCallbackData * data)
{
  assert (data);
  assert (data->UserData);
  PexNinja * application = (PexNinja *)data->UserData;

  PulseRegistrationAliasList * conference_alias_list = NULL;
  if (data->BufTextLen > 0) {
    PulseError err = pulse_registrations_query_alias (application->client, data->Buf, 0, -1, &conference_alias_list);
    if (err != PULSE_SUCCESS) {
      PEX_LOG_ERROR ("Failed to fetch alias list: %s", pulse_strerror (err));
    }
  }
  pulse_registration_alias_list_free (application->state.conference_alias_list);
  application->state.conference_alias_list = conference_alias_list;

  return 0;
}

static int
_update_device_alias_list_cb (ImGuiInputTextCallbackData * data)
{
  assert (data);
  assert (data->UserData);
  PexNinja * application = (PexNinja *)data->UserData;

  PulseRegistrationAliasList * device_alias_list = NULL;
  if (data->BufTextLen > 0) {
    PulseError err = pulse_registrations_query_alias (application->client, data->Buf, -1, 0, &device_alias_list);
    if (err != PULSE_SUCCESS) {
      PEX_LOG_ERROR ("Failed to fetch alias list: %s", pulse_strerror (err));
    }
  }
  pulse_registration_alias_list_free (application->state.device_alias_list);
  application->state.device_alias_list = device_alias_list;

  return 0;
}

static void
_init_conference_alias_list (PexNinja * application)
{
  if (application->state.registered) {
    PulseRegistrationAliasList * conference_alias_list = NULL;
    PulseError err = pulse_registrations_query_alias (application->client, NULL, 0, -1, &conference_alias_list);
    if (err != PULSE_SUCCESS) {
      PEX_LOG_ERROR ("Failed to fetch conference alias list: %s", pulse_strerror (err));
    }
    pulse_registration_alias_list_free (application->state.conference_alias_list);
    application->state.conference_alias_list = conference_alias_list;
  }
}

static void
_init_device_alias_list (PexNinja * application)
{
  if (application->state.registered) {
    PulseRegistrationAliasList * device_alias_list = NULL;
    PulseError err = pulse_registrations_query_alias (application->client, NULL, -1, 0, &device_alias_list);
    if (err != PULSE_SUCCESS) {
      PEX_LOG_ERROR ("Failed to fetch device alias list: %s", pulse_strerror (err));
    }
    pulse_registration_alias_list_free (application->state.device_alias_list);
    application->state.device_alias_list = device_alias_list;
  }
}

/* Renders a `GLTextureContext` into a small floating ImGui window. By
 * default this is used to host the self-view ("Me! Me! Me!") in the
 * corner; when the user toggles "Swap big/small video views" the same
 * window instead shows the received MAIN video and the self-view takes
 * over the GLFW background.
 *
 * `is_selfview` controls whether the paint / annotation overlay is
 * attached here — paint always belongs on the self-view content, so
 * when the selfview has been promoted to the background the overlay is
 * drawn separately as a fullscreen ImGui layer (see the call site in
 * main()) and this function must NOT also attach it. */
static inline void
configure_window_self_view (PexNinja * application, GLTextureContext * ctx, const char * title, bool is_selfview)
{
  ImGui::SetNextWindowSize (ImVec2 (0, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin (title, &application->state.windows.show_self_view,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
  render_gl_ctx_image (application->client, ctx);
  if (is_selfview) {
    /* Drawing overlay (no-op when paint drawing-mode is disabled). The
     * image has just been laid out — its bounds are the last item. */
    ImVec2 img_min = ImGui::GetItemRectMin ();
    ImVec2 img_size = ImGui::GetItemRectSize ();
    paint_overlay_handle (application, img_min, img_size);
  }
  ImGui::End ();
}

static inline void
configure_window_roster_list (PexNinja * application)
{
  // ImGui::SetNextWindowSize (ImVec2 (0, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin ("Roster list", &application->state.windows.show_roster_list,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

  {
    std::lock_guard<std::mutex> lock (application->room_list.mutex);

    PulseConferenceEventParticipantList * participants = application->room_list.current_room->roster_list.filtered_data;
    if (participants == NULL)
      participants = application->room_list.current_room->roster_list.data;
    if (participants != NULL) {
      PulseConferenceRole role;

      PulseError err = pulse_session_get_role (application->client, &role);
      if (err != PULSE_SUCCESS) {
        if (err != PULSE_ERROR_NOT_CONNECTED) {
          PEX_LOG_DEBUG ("Failed to call pulse_session_get_role: %s\n", pulse_strerror (err));
        }
        role = PULSE_CONFERENCE_ROLE_GUEST;
      }

      // Initial spin through of participant list, to filter out a few things.
      bool has_non_default_send_to_audio_mix = false;
      bool has_non_default_receive_from_audio_mix = false;
      for (int i = 0; i < (int)participants->participant_list_size; i++) {
        if (participants->participant_list[i]->is_active_participant == false)
          continue;
        for (size_t j = 0; j < participants->participant_list[i]->send_to_audio_mixes.list_size; j++) {
          if (pex_strcmp0 (participants->participant_list[i]->send_to_audio_mixes.entries[j].mix_name, "main") != 0) {
            has_non_default_send_to_audio_mix = true;
          }
        }

        if (pex_strcmp0 (participants->participant_list[i]->receive_from_audio_mix, "main") != 0) {
          has_non_default_receive_from_audio_mix = true;
        }
      }

      int columns = 6 + (role == PULSE_CONFERENCE_ROLE_HOST ? 1 : 0) + (has_non_default_send_to_audio_mix ? 1 : 0) +
                    (has_non_default_receive_from_audio_mix ? 1 : 0);

      if (ImGui::BeginTable ("Participants", columns)) {
        ImGui::TableHeader ("Superlist");
        ImGui::TableNextRow ();
        ImGui::TableNextColumn ();
        ImGui::Text ("Participant");
        if (role == PULSE_CONFERENCE_ROLE_HOST) {
          ImGui::TableNextColumn ();
          ImGui::Text ("Pulse");
        }
        ImGui::TableNextColumn ();
        ImGui::Text ("Role");
        ImGui::TableNextColumn ();
        ImGui::Text ("Visible");
        ImGui::TableNextColumn ();
        ImGui::Text ("Audible");
        if (has_non_default_send_to_audio_mix) {
          ImGui::TableNextColumn ();
          ImGui::Text ("Send-mix");
        }
        if (has_non_default_receive_from_audio_mix) {
          ImGui::TableNextColumn ();
          ImGui::Text ("Recv-mix");
        }
        ImGui::TableNextColumn ();
        ImGui::Text ("Status");

        ImGui::TableNextColumn ();
        ImGui::Text ("Commands");

        for (int row = 0; row < (int)participants->participant_list_size; row++) {
          if (participants->participant_list[row]->is_active_participant == false)
            continue;

          ImVec4 color = clear_color;
          if (participants->participant_list[row]->is_local_participant) {
            color = green_color;
          }

          ImGui::TableNextRow ();
          ImGui::TableNextColumn ();
          ImGui::TextColored (color, "%s", participants->participant_list[row]->active_display_name);
          if (ImGui::IsItemHovered ()) {
            ImGui::BeginTooltip ();
            ImGui::Text ("Full info for participant '%s'", participants->participant_list[row]->active_display_name);
            if (ImGui::BeginTable ("Participants", 2)) {
              ImGui::TableNextRow ();
              ImGui::TableNextColumn ();

              ImGui::Separator ();
              ImGui::Text ("buzz_time: %" PRIu64, participants->participant_list[row]->buzz_time);
              ImGui::Text ("call_direction: %s",
                           participants->participant_list[row]->call_direction == 0 ? "in" : "out");
              ImGui::Text ("call_tag: %s", participants->participant_list[row]->call_tag);
              ImGui::Text ("disconnect_supported: %s",
                           participants->participant_list[row]->disconnect_supported ? "yes" : "no");
              ImGui::Text ("encryption: %s", participants->participant_list[row]->encryption ? "yes" : "no");
              ImGui::Text ("external_node_uuid: %s", participants->participant_list[row]->external_node_uuid);
              ImGui::Text ("fecc_supported: %s", participants->participant_list[row]->fecc_supported ? "yes" : "no");
              ImGui::Text ("has_media: %s", participants->participant_list[row]->has_media ? "yes" : "no");
              ImGui::Text ("is_audio_only_call: %s",
                           participants->participant_list[row]->is_audio_only_call ? "yes" : "no");
              ImGui::Text ("is_external: %s", participants->participant_list[row]->is_external ? "yes" : "no");
              ImGui::Text ("is_main_video_dropped_out: %s",
                           participants->participant_list[row]->is_main_video_dropped_out ? "yes" : "no");
              ImGui::Text ("is_muted: %s", participants->participant_list[row]->is_muted ? "yes" : "no");
              ImGui::Text ("is_client_muted: %s", participants->participant_list[row]->is_client_muted ? "yes" : "no");
              ImGui::Text ("is_presenting: %s", participants->participant_list[row]->is_presenting ? "yes" : "no");
              ImGui::Text ("is_streaming_conference: %s",
                           participants->participant_list[row]->is_streaming_conference ? "yes" : "no");
              ImGui::Text ("is_video_call: %s", participants->participant_list[row]->is_video_call ? "yes" : "no");
              ImGui::Text ("is_video_muted: %s", participants->participant_list[row]->is_video_muted ? "yes" : "no");
              ImGui::Text ("is_video_silent: %s", participants->participant_list[row]->is_video_silent ? "yes" : "no");
              ImGui::Text ("is_idp_authenticated: %s",
                           participants->participant_list[row]->is_idp_authenticated ? "yes" : "no");
              ImGui::Text ("needs_presentation_in_mix: %s",
                           participants->participant_list[row]->needs_presentation_in_mix ? "yes" : "no");
              ImGui::Text ("show_live_captions: %s",
                           participants->participant_list[row]->show_live_captions ? "yes" : "no");
              ImGui::Text ("is_conjoined: %s", participants->participant_list[row]->is_conjoined ? "yes" : "no");
              ImGui::Text ("room_id: %d", (int)participants->participant_list[row]->is_conjoined);
              ImGui::Text ("local_alias: %s", participants->participant_list[row]->local_alias);
              ImGui::Text ("mute_supported: %s", participants->participant_list[row]->mute_supported ? "yes" : "no");
              ImGui::Text ("overlay_text: %s", participants->participant_list[row]->overlay_text);
              ImGui::Text ("presentation_supported: %s",
                           participants->participant_list[row]->presentation_supported ? "yes" : "no");
              ImGui::Text ("protocol: %s",
                           pulse_type_mapping_protocol_to_string (participants->participant_list[row]->protocol));
              ImGui::Text ("role: %s", pulse_type_mapping_role_to_string (participants->participant_list[row]->role));
              ImGui::Text ("rx_presentation_policy: %s",
                           pulse_type_mapping_rx_presentation_policy_to_string (
                             participants->participant_list[row]->rx_presentation_policy));

              ImGui::Text ("service_type: %s", pulse_type_mapping_service_type_to_string (
                                                 participants->participant_list[row]->service_type));

              ImGui::TableNextColumn ();

              ImGui::Text ("spotlight: %" PRIu64, participants->participant_list[row]->spotlight);
              ImGui::Text ("start_time: %" PRIu64, participants->participant_list[row]->start_time);
              ImGui::Text ("transfer_supported: %s",
                           participants->participant_list[row]->transfer_supported ? "yes" : "no");
              ImGui::Text ("uuid: %s", participants->participant_list[row]->uuid);
              ImGui::Text ("uri: %s", participants->participant_list[row]->uri);
              ImGui::Text ("vendor: %s", participants->participant_list[row]->vendor);

              ImGui::Text ("api_url: %s", participants->participant_list[row]->api_url);
              ImGui::Text ("receive_from_audio_mix: %s", participants->participant_list[row]->receive_from_audio_mix);
              for (size_t i = 0; i < participants->participant_list[row]->send_to_audio_mixes.list_size; i++) {
                ImGui::Text (
                  "send_to_audio_mix: %s%s",
                  participants->participant_list[row]->send_to_audio_mixes.entries[i].mix_name,
                  participants->participant_list[row]->send_to_audio_mixes.entries[i].prominent ? " (prominent)" : "");
              }
              ImGui::Text ("layout_group: %s", participants->participant_list[row]->layout_group);
              ImGui::Text ("is_transferring: %s", participants->participant_list[row]->is_transferring ? "yes" : "no");
              ImGui::Text ("is_local_participant: %s",
                           participants->participant_list[row]->is_local_participant ? "yes" : "no");
              ImGui::Text ("is_active_participant: %s",
                           participants->participant_list[row]->is_active_participant ? "yes" : "no");
              ImGui::Text ("is_breakout_manageable: %s",
                           participants->participant_list[row]->is_breakout_manageable ? "yes" : "no");
              ImGui::Text ("is_pulse_client: %s", participants->participant_list[row]->is_pulse_client ? "yes" : "no");

              ImGui::Text ("active_display_name: %s", participants->participant_list[row]->active_display_name);
              ImGui::Text ("is_on_stage: %s", participants->participant_list[row]->is_on_stage ? "yes" : "no");
              ImGui::Text ("is_speaking: %s", participants->participant_list[row]->is_speaking ? "yes" : "no");
              ImGui::Text ("vad: %u", participants->participant_list[row]->vad);
              ImGui::Text ("is_in_layout: %s", participants->participant_list[row]->is_in_layout ? "yes" : "no");

              ImGui::Text ("is_ai_enabled: %s", participants->participant_list[row]->is_ai_enabled ? "yes" : "no");
              ImGui::Text ("is_on_hold: %s", participants->participant_list[row]->is_on_hold ? "yes" : "no");
              ImGui::Text ("is_public_streaming: %s",
                           participants->participant_list[row]->is_public_streaming ? "yes" : "no");
              ImGui::Text ("is_recording: %s", participants->participant_list[row]->is_recording ? "yes" : "no");
              ImGui::Text ("is_transcribing: %s", participants->participant_list[row]->is_transcribing ? "yes" : "no");
              ImGui::Text ("supports_direct_chat: %s",
                           participants->participant_list[row]->supports_direct_chat ? "yes" : "no");
              ImGui::Text ("studiosound_enabled: %s",
                           participants->participant_list[row]->studiosound_enabled ? "yes" : "no");
              ImGui::Text ("is_tx_muted: %s", participants->participant_list[row]->is_tx_muted ? "yes" : "no");
              ImGui::Text ("can_receive_personal_mix: %s",
                           participants->participant_list[row]->can_receive_personal_mix ? "yes" : "no");
              if (participants->participant_list[row]->receive_from_video_mix)
                ImGui::Text ("receive_from_video_mix name: %s ",
                             participants->participant_list[row]->can_receive_personal_mix ? "yes" : "no");

              ImGui::EndTable ();
            }
            ImGui::EndTooltip ();
          }

          if (role == PULSE_CONFERENCE_ROLE_HOST) {
            ImGui::TableNextColumn (); // Rulse?
            if (participants->participant_list[row]->is_pulse_client)
              ImGui::TextColored (color, "*");
          }

          ImGui::TableNextColumn (); // Role
          ImGui::TextColored (color, "%s",
                              pulse_type_mapping_role_to_string (participants->participant_list[row]->role));

          ImGui::TableNextColumn (); // Visible
          if (participants->participant_list[row]->is_in_layout) {
            ImGui::TextColored (color, "*");
          }

          ImGui::TableNextColumn (); // Audible
          if (participants->participant_list[row]->is_speaking) {
            ImGui::TextColored (color, "*");
          }

          if (has_non_default_send_to_audio_mix) {
            ImGui::TableNextColumn (); // send to audio mixer
            for (size_t i = 0; i < participants->participant_list[row]->send_to_audio_mixes.list_size; i++) {
              ImGui::TextColored (
                color, "%s%s", participants->participant_list[row]->send_to_audio_mixes.entries[i].mix_name,
                participants->participant_list[row]->send_to_audio_mixes.entries[i].prominent ? "[prominent]" : "");
            }
          }

          if (has_non_default_receive_from_audio_mix) {
            ImGui::TableNextColumn (); // send to audio mixer
            ImGui::TextColored (color, "%s", participants->participant_list[row]->receive_from_audio_mix);
          }

          ImGui::TableNextColumn (); // Status
          std::string buf = std::string ("");
          if (participants->participant_list[row]->service_type == PULSE_CONFERENCE_SERVICE_TYPE_WAITING_ROOM)
            buf.append (buf.length () ? ",in-waiting-room" : "in-waiting-room");

          if (participants->participant_list[row]->is_muted || participants->participant_list[row]->is_client_muted) {
            buf.append (buf.length () ? ",audio-muted(" : "audio-muted(");

            if (participants->participant_list[row]->is_client_muted)
              buf.append ("client");
            if (participants->participant_list[row]->is_muted && participants->participant_list[row]->is_client_muted)
              buf.append (",");
            if (participants->participant_list[row]->is_muted)
              buf.append ("server");
            buf.append (")");
          }

          if (participants->participant_list[row]->is_video_muted)
            buf.append (buf.length () ? ",video-muted" : "video-muted");
          if (participants->participant_list[row]->buzz_time)
            buf.append (buf.length () ? ",raised-hand" : "raised-hand");
          if (participants->participant_list[row]->spotlight)
            buf.append (buf.length () ? ",spotlighted" : "spotlighted");
          ImGui::TextColored (color, "%s", buf.c_str ());

          ImGui::TableNextColumn (); // commands
          char menu_name[1024] = "\0";
          snprintf (menu_name, sizeof (menu_name), "##controls-%d", row);
          if (ImGui::BeginMenu (menu_name)) {
            if (role == PULSE_CONFERENCE_ROLE_HOST) {
              if (participants->participant_list[row]->service_type == PULSE_CONFERENCE_SERVICE_TYPE_WAITING_ROOM) {
                if (ImGui::MenuItem ("Allow paricipant into conference", NULL)) {
                  participants->participant_list[row]->service_type = PULSE_CONFERENCE_SERVICE_TYPE_CONFERENCE;
                  pulse_participant_control_unlock (application->client, participants->participant_list[row]->uuid);
                }
              } else {
                if (!participants->participant_list[row]->is_local_participant) {
                  if (participants->participant_list[row]->is_muted) {
                    if (ImGui::MenuItem ("Unmute audio", NULL)) {
                      participants->participant_list[row]->is_muted = false;
                      pulse_participant_control_remote_audio_mute (application->client,
                                                                   participants->participant_list[row]->uuid, false);
                    }
                  } else if (participants->participant_list[row]->mute_supported) {
                    if (ImGui::MenuItem ("Mute audio", NULL)) {
                      participants->participant_list[row]->is_muted = true;
                      pulse_participant_control_remote_audio_mute (application->client,
                                                                   participants->participant_list[row]->uuid, true);
                    }
                  }

                  if (participants->participant_list[row]->is_video_muted) {
                    if (ImGui::MenuItem ("Unmute video", NULL)) {
                      participants->participant_list[row]->is_video_muted = false;
                      pulse_participant_control_remote_video_mute (application->client,
                                                                   participants->participant_list[row]->uuid, false);
                    }
                  } else if (participants->participant_list[row]->mute_supported) {
                    if (ImGui::MenuItem ("Mute video", NULL)) {
                      participants->participant_list[row]->is_video_muted = true;
                      pulse_participant_control_remote_video_mute (application->client,
                                                                   participants->participant_list[row]->uuid, true);
                    }
                  }
                }

                if (participants->participant_list[row]->spotlight) {
                  if (ImGui::MenuItem ("Spotlight off", NULL)) {
                    participants->participant_list[row]->spotlight = false;
                    pulse_participant_control_spotlight_off (application->client,
                                                             participants->participant_list[row]->uuid);
                  }
                } else {
                  if (ImGui::MenuItem ("Spotlight on", NULL)) {
                    participants->participant_list[row]->spotlight = true;
                    pulse_participant_control_spotlight_on (application->client,
                                                            participants->participant_list[row]->uuid);
                  }
                }
                if (participants->participant_list[row]->role == PULSE_CONFERENCE_ROLE_GUEST) {
                  if (ImGui::MenuItem ("Make host", NULL)) {
                    pulse_participant_control_role (application->client, participants->participant_list[row]->uuid,
                                                    PULSE_CONFERENCE_ROLE_HOST);
                  }
                } else {
                  if (ImGui::MenuItem ("Make guest", NULL)) {
                    pulse_participant_control_role (application->client, participants->participant_list[row]->uuid,
                                                    PULSE_CONFERENCE_ROLE_GUEST);
                  }
                }

                if (!participants->participant_list[row]->is_local_participant) {
                  if (participants->participant_list[row]->buzz_time != 0) {
                    if (ImGui::MenuItem ("Lower participant hand", NULL)) {
                      participants->participant_list[row]->buzz_time = 0;
                      pulse_participant_control_clearbuzz (application->client,
                                                           participants->participant_list[row]->uuid);
                    }
                  }
                }
                if (application->room_list.room_map.size () > 1) {
                  if (ImGui::BeginMenu ("Move to a different room")) {
                    for (auto it = application->room_list.room_map.begin ();
                         it != application->room_list.room_map.end (); it++) {
                      if (it->first == application->room_list.current_room_id)
                        continue;
                      const char * name = it->second->conference_status.breakout_rooms.enabled
                                            ? it->second->conference_status.breakout_rooms.name.c_str ()
                                            : "Main room";
                      if (ImGui::MenuItem (name, NULL)) {
                        PulseConferenceControlBreakoutsTransferRequest req;
                        memset (&req, 0, sizeof (PulseConferenceControlBreakoutsTransferRequest));
                        req.source_room_id = application->room_list.current_room_id;
                        req.destination_room_id = it->first;
                        req.transfer_participants_num = 1;
                        const char * transfer_participants[1] = {participants->participant_list[row]->uuid};
                        req.transfer_participants = transfer_participants;
                        pulse_conference_control_breakouts_transfer_participants (application->client, &req);
                      }
                    }
                    ImGui::EndMenu ();
                  }
                }
              }
              if (participants->participant_list[row]->is_local_participant == false &&
                  ImGui::MenuItem ("Disconnect paricipant", NULL)) {
                pulse_participant_control_disconnect (application->client, participants->participant_list[row]->uuid);
              }
              if (participants->participant_list[row]->fecc_supported) {
                if (ImGui::MenuItem ("Control remote camera", NULL)) {
                  application->state.windows.show_fecc_window = true;
                  application->state.fecc.participant_display_name =
                    std::string (participants->participant_list[row]->display_name);
                  application->state.fecc.participant_uuid = std::string (participants->participant_list[row]->uuid);
                }
              }
            }

            if (ImGui::MenuItem ("Send DTMF sequence", NULL)) {
              application->state.dtmf.send_to_recepient = true;
              application->state.dtmf.participant_display_name =
                std::string (participants->participant_list[row]->display_name);
              application->state.dtmf.participant_uuid = std::string (participants->participant_list[row]->uuid);
              application->state.popup_send_dtmf_sequence = true;
            }
            ImGui::EndMenu ();
          }
        }
        ImGui::EndTable ();
      }
    }
  }
  ImGui::Separator ();
  ImGui::Text ("Filter:");
  ImGui::SameLine ();
  if (ImGui::InputText ("##roster_filter", application->state.roster_list.search_filter,
                        sizeof (application->state.roster_list.search_filter) - 1, ImGuiInputTextFlags_None)) {
    pulse_conference_control_free_participant_list (application->room_list.current_room->roster_list.filtered_data);

    if (strlen (application->state.roster_list.search_filter) > 0) {
      application->room_list.current_room->roster_list.filtered_data = pulse_participant_list_search (
        application->room_list.current_room->roster_list.data, application->state.roster_list.search_filter, NULL);
    } else {
      application->room_list.current_room->roster_list.filtered_data = NULL;
    }
  }

  if (application->state.roster_list.search_filter[0] != 0) {
    ImGui::SameLine ();
    if (ImGui::Button ("Clear")) {
      pulse_conference_control_free_participant_list (application->room_list.current_room->roster_list.filtered_data);
      application->room_list.current_room->roster_list.filtered_data = NULL;
      application->state.roster_list.search_filter[0] = 0;
    }
  }

  if (application->room_list.current_room->roster_list.filtered_data)
    imgui_begin_disabled_state ();

  ImGui::Separator ();
  static PulseParticipantListSortingConfig sorting;
  ImGui::Text ("Order by:");
  ImGui::SameLine ();
  if (ImGui::RadioButton ("Join time", (int *)&sorting.order, PULSE_PARTICIPANT_LIST_SORTING_BY_JOIN_TIME)) {
    pulse_options_set_participant_list_sorting (application->client, &sorting);
  }
  ImGui::SameLine ();
  if (ImGui::RadioButton ("Alphabetically", (int *)&sorting.order, PULSE_PARTICIPANT_LIST_SORTING_ALPHABETICALLY)) {
    pulse_options_set_participant_list_sorting (application->client, &sorting);
  }
  if (ImGui::Checkbox ("Reverse order", &sorting.reverse_order)) {
    pulse_options_set_participant_list_sorting (application->client, &sorting);
  }
  ImGui::SameLine ();
  if (ImGui::Checkbox ("Self on top", &sorting.self_on_top)) {
    pulse_options_set_participant_list_sorting (application->client, &sorting);
  }
  ImGui::SameLine ();
  if (ImGui::Checkbox ("Waiting room on top", &sorting.waiting_room_on_top)) {
    pulse_options_set_participant_list_sorting (application->client, &sorting);
  }

  if (application->room_list.current_room->roster_list.filtered_data)
    imgui_end_disabled_state ();

  ImGui::End ();
}

static inline void
configure_window_chat_window (PexNinja * application)
{
  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  application->room_list.current_room->chat_messages.chat_messages_unread = 0;
  bool send = false;
  static char send_message[1024] = "\0";

  ImGui::SetNextWindowSize (ImVec2 (400, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin ("Chat", &application->state.windows.show_chat_window, ImGuiWindowFlags_None);
  ImGui::BeginChild ("ChatMessages", ImVec2 (0, 300));
  ImGui::TextWrapped ("%s", application->room_list.current_room->chat_messages.concat_chat_messages.c_str ());
  ImGui::SetScrollHereY (1.0f);
  ImGui::EndChild ();
  ImGui::Separator ();

  PexNinjaRoom * room = application->room_list.current_room;
  assert (room);

  if (room->chat_messages.selected_message_recepient_name == NULL) {
    room->chat_messages.selected_message_recepient_name = "<broadcast>";
  }

  if (ImGui::BeginCombo ("recepient", room->chat_messages.selected_message_recepient_name, ImGuiComboFlags_None)) {
    bool is_selected = (pex_strcmp0 (room->chat_messages.selected_message_recepient_name, "<broadcast>") == 0);
    if (ImGui::Selectable ("<broadcast>", is_selected)) {
      room->chat_messages.selected_message_recepient_name = "<broadcast>";
      free (room->chat_messages.selected_message_recepient_uuid);
      room->chat_messages.selected_message_recepient_uuid = NULL;
    }
    if (is_selected)
      ImGui::SetItemDefaultFocus ();

    for (int i = 0; i < (int)room->roster_list.data->participant_list_size; i++) {
      if (room->roster_list.data->participant_list[i]->supports_direct_chat == false)
        continue;
      if (room->roster_list.data->participant_list[i]->is_active_participant == false)
        continue;
      if (room->roster_list.data->participant_list[i]->is_local_participant)
        continue;
      const char * name = room->roster_list.data->participant_list[i]->display_name;

      is_selected = (pex_strcmp0 (room->chat_messages.selected_message_recepient_name, name) == 0);
      if (ImGui::Selectable (name, is_selected)) {
        room->chat_messages.selected_message_recepient_name = name;
        free (room->chat_messages.selected_message_recepient_uuid);
        room->chat_messages.selected_message_recepient_uuid =
          strdup (room->roster_list.data->participant_list[i]->uuid);
      }
      if (is_selected)
        ImGui::SetItemDefaultFocus ();
    }
    ImGui::EndCombo ();
  }

  if (ImGui::InputText ("##chat_message", send_message, 1023, ImGuiInputTextFlags_EnterReturnsTrue)) {
    send = true;
  }
  ImGui::SameLine ();
  if (ImGui::Button ("Send")) {
    send = true;
  }
  ImGui::End ();

  if (send) {
    if (strlen (send_message) > 0) {
      PEX_LOG_DEBUG ("Sending msg: %s", send_message);
      PulseMessageRequest msg;
      msg.content_type = PULSE_MESSAGE_CONTENT_TYPE_PLAIN;
      msg.payload = send_message;
      pulse_send_message (application->client, room->chat_messages.selected_message_recepient_uuid, &msg);

      const std::time_t now = std::time (nullptr);
      const std::tm calendar_time = *std::localtime (std::addressof (now));

      std::stringstream ss;
      ss << "[" << std::setfill ('0') << std::setw (2) << calendar_time.tm_hour << ":" << std::setfill ('0')
         << std::setw (2) << calendar_time.tm_min << ":" << std::setfill ('0') << std::setw (2) << calendar_time.tm_sec
         << "] " << application->config.connection.displayName << "("
         << (room->chat_messages.selected_message_recepient_uuid ? room->chat_messages.selected_message_recepient_uuid
                                                                 : "broadcast")
         << ") : " << send_message << std::endl;
      application->room_list.current_room->chat_messages.chat_messages.append (ss.str ());
      room->chat_messages.concat_chat_messages =
        room->chat_messages.sync_join_messages + "\n" + room->chat_messages.chat_messages;

      send_message[0] = '\0';
    }
  }
}

static inline void
configure_window_live_captions_window (PexNinja * application)
{
  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  ImGui::SetNextWindowSize (ImVec2 (400, 300), ImGuiCond_FirstUseEver);
  ImGui::Begin ("Live Captions", &application->state.windows.show_live_captions_window, ImGuiWindowFlags_None);

  ImGui::TextWrapped ("%s", application->room_list.current_room->live_captions.live_caption_display.c_str ());
  ImGui::SetScrollHereY (1.0f);
  ImGui::End ();
}

static inline void
configure_window_add_participant_window (PexNinja * application)
{
  ImGui::SetNextWindowSize (ImVec2 (0, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin ("Add participant", &application->state.windows.show_add_participant_window,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);

  ImGui::Text ("Enter participant alias:");
  static char alias[1024];
  bool acknowledged = ImGui::InputTextWithHint ("Participant", "Participant alias", alias, sizeof (alias) - 1,
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackEdit,
                                                _update_device_alias_list_cb, application);
  const bool is_input_text_active = ImGui::IsItemActive ();
  const bool is_input_text_activated = ImGui::IsItemActivated ();

  if (is_input_text_activated)
    ImGui::OpenPopup ("##popup_add_participant");

  if (application->state.device_alias_list && application->state.device_alias_list->size > 0) {
    ImGui::SetNextWindowPos (ImVec2 (ImGui::GetItemRectMin ().x, ImGui::GetItemRectMax ().y));
    ImGui::SetNextWindowSizeConstraints (ImVec2 (0, 0), ImVec2 (1000, 300));
    if (ImGui::BeginPopup ("##popup_add_participant", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_ChildWindow)) {
      for (size_t i = 0; i < application->state.device_alias_list->size; i++) {
        if (ImGui::Selectable (application->state.device_alias_list->list[i]->alias)) {
          ImGui::ClearActiveID ();
          strncpy (alias, application->state.device_alias_list->list[i]->alias, 1023);
        }
      }

      if (acknowledged || (!is_input_text_active && !ImGui::IsWindowFocused ()))
        ImGui::CloseCurrentPopup ();

      ImGui::EndPopup ();
    }
  }

  if (ImGui::Button ("OK")) {
    if (strlen (alias)) {
      acknowledged = true;
    }
  }

  ImGui::SameLine ();

  if (ImGui::Button ("Cancel")) {
    acknowledged = false;
    application->state.windows.show_add_participant_window = false;
    alias[0] = '\0';
  }

  if (acknowledged) {
    PulseConferenceControlDialRequest req = {
      .role = PULSE_CONFERENCE_ROLE_HOST,
      .destination = alias,
      .protocol = PULSE_CONFERENCE_PROTOCOL_AUTO,
      .presentation_url = NULL,
      .streaming = false,
      .dtmf_sequence = NULL,
      .source_display_name = NULL,
      .source = NULL,
      .call_type = PULSE_CONFERENCE_CALL_TYPE_VIDEO,
      .keep_alive_type = PULSE_CONFERENCE_KEEP_ALIVE,
      .remote_display_name = NULL,
      .text = NULL,
    };
    PulseConferenceControlDialResponse * res = NULL;

    pulse_conference_control_dial (application->client, &req, &res);
    pulse_conference_control_free_dial_participant_response (res); // HANDLE LATER!
    application->state.windows.show_add_participant_window = false;
    alias[0] = '\0';
  }

  ImGui::End ();
}

static inline void
configure_window_show_pexninja_info (PexNinja * application)
{
  // ImGui::SetNextWindowSize (ImVec2 (300, 450), ImGuiCond_FirstUseEver);
  ImGui::Begin ("PexNinja info", &application->state.windows.show_pexninja_info, ImGuiWindowFlags_NoResize);
  ImGui::Text ("Version: %s", VERSION);
  ImGui::End ();
}

/* ----------------------------------------------------------------------------
 * Shared RTMP server (Compositor-facing).
 *
 * A single RTMP listener fronts the one Compositor RTMP source we
 * currently support. The standalone "RTMP Server" window can bring it
 * up explicitly with specific listener settings (path, port, TLS,
 * auth), and the Compositor lazy-starts it on kRtmp materialise /
 * lazy-stops it on release.
 *
 * The Pulse RTMP API today exposes one `pexrtmpsrc` per listener,
 * which means one path per pexninja instance. We pass that path
 * (`state.rtmp_server.path`) through to the listener, and the publish
 * accept callback admits exactly the publisher whose RTMP path
 * matches it — anything else is rejected at the protocol level. The
 * publish_start/stop callbacks maintain a `live_publishers` counter so
 * the source card can render a live/waiting status chip.
 *
 * Because the publish callbacks fire on a Pulse worker thread, every
 * read or write of the live counter and owner state goes through
 * `state.rtmp_server.mu`. */
namespace rtmp_server
{

/* The RTMP listener is hard-wired to the PRESENTATION media-content
 * slot. The Pulse RTMP API is keyed on PulseMediaContent; the choice
 * is internal plumbing the user no longer sees on the per-source UI. */
static constexpr PulseMediaContent kListenerSlot = PULSE_MEDIA_CONTENT_PRESENTATION;

static bool
_publish_accept_cb (int client_id, const char * path, const char * params, void * user_context)
{
  PexNinja * application = (PexNinja *)user_context;
  (void)application;

  const std::string p = path != NULL ? std::string (path) : std::string ();
  bool accepted = true;

  PEX_LOG_DEBUG ("RTMP_INPUT_PUBLISH_ACCEPT: client_id=%d path='%s' params='%s' uc=%p -> %s", client_id,
             path ? path : "(null)", params ? params : "(null)", user_context, accepted ? "accept" : "REJECT");
  return accepted;
}

static void
_publish_start_cb (int client_id, const char * path, const char * params, void * user_context)
{
  PexNinja * application = (PexNinja *)user_context;

  PEX_LOG_DEBUG ("RTMP_INPUT_PUBLISH_START: client_id=%d path='%s' params='%s' uc=%p", client_id, path ? path : "(null)",
             params ? params : "(null)", user_context);

  std::lock_guard<std::mutex> lock (application->state.rtmp_server.mu);
  /* Only count publishers on the configured path: a stray publisher
   * on a different path was rejected by accept-cb but defensively
   * we still ignore it here. */
  if (path != NULL && std::string (path) == application->state.rtmp_server.path)
    application->state.rtmp_server.live_publishers++;
}

static void
_publish_stop_cb (int client_id, const char * path, const char * params, uint32_t server_status, void * user_context)
{
  PexNinja * application = (PexNinja *)user_context;

  PEX_LOG_DEBUG ("RTMP_INPUT_PUBLISH_STOP: client_id=%d path='%s' params='%s' status=%u uc=%p", client_id,
             path ? path : "(null)", params ? params : "(null)", server_status, user_context);

  std::lock_guard<std::mutex> lock (application->state.rtmp_server.mu);
  if (path != NULL && std::string (path) == application->state.rtmp_server.path &&
      application->state.rtmp_server.live_publishers > 0)
    application->state.rtmp_server.live_publishers--;
}

/* Build the user-facing Publish URL:
 * `rtmp[s]://<host>:<port>/app/<path>`. The `/app/` segment is the
 * RTMP application name our listener registers under — publishers
 * (OBS / ffmpeg / …) won't be admitted without it, even if the
 * trailing path matches. The host is the Pulse-reported IPv4
 * address (the same one shown in the bottom-right network status
 * chip) when available — that's the address publishers need to
 * point at, and it works without DNS — falling back to the
 * configured `advertised_host` and finally to "localhost" for fully
 * offline runs. The trailing path is trimmed of leading slashes
 * (publishers commonly type "live" but humans often paste "/live"). */
static std::string
_publish_url (PexNinja * application)
{
  const auto & srv = application->state.rtmp_server;
  const char * scheme = srv.use_tls ? "rtmps" : "rtmp";
  const char * host = NULL;
  if (application->state.network_status.has_ipv4_address && application->state.network_status.ipv4_address[0] != '\0')
    host = application->state.network_status.ipv4_address;
  else if (srv.advertised_host[0] != '\0')
    host = srv.advertised_host;
  else
    host = "localhost";
  const char * p = srv.path;
  while (*p == '/')
    p++;
  std::ostringstream ss;
  ss << scheme << "://" << host << ':' << srv.listening_port << "/app/" << p;
  return ss.str ();
}

/* Bring the listener up with the current server settings. Idempotent:
 * if the listener is already up, returns PULSE_SUCCESS without
 * touching it. `lazy` records whether the Compositor (rather than the
 * user) brought it up, which controls whether the last-source release
 * tears it back down. The two PulseRtmp*Config structs are stored on
 * the stack — Pulse copies what it needs synchronously. */
static PulseError
_start (PexNinja * application, bool lazy)
{
  auto & srv = application->state.rtmp_server;
  if (srv.is_connected)
    return PULSE_SUCCESS;

  if (srv.path[0] == '\0') {
    PEX_LOG_WARNING ("rtmp_server::_start: empty server path; configure one in Menu → RTMP Server");
    return PULSE_ERROR_INVALID_PARAMETER;
  }

  PulseRtmpAuthConfig auth_config = {.username = srv.auth_username, .password = srv.auth_password};
  PulseRtmpTlsInputConfig tls_input_config = {
    .cert_file = srv.tls_cert_file,
    .key_file = srv.tls_key_file,
    .ciphers = strlen (srv.tls_ciphers) > 0 ? srv.tls_ciphers : NULL,
  };

  /* Pulse only exposes one `pexrtmpsrc` per listener and that
   * `pexrtmpsrc` filters server callbacks by its configured path —
   * passing an empty path here would silently drop every frame
   * regardless of what publish_accept_cb returns. So we wire the
   * server-wide `srv.path` straight through. */
  PulseRtmpInputConfig config = {
    .path = srv.path,
    .listening_port = srv.listening_port,
    .use_tls = srv.use_tls,
    .support_audio = srv.support_audio,
    .support_video = srv.support_video,
    .callbacks =
      {
        .publish_accept_cb = _publish_accept_cb,
        .publish_start_cb = _publish_start_cb,
        .publish_stop_cb = _publish_stop_cb,
        .publish_uc = application,
        .play_accept_cb = NULL,
        .play_start_cb = NULL,
        .play_stop_cb = NULL,
        .play_uc = NULL,
      },
    .auth_config = srv.use_auth ? &auth_config : NULL,
    .tls_config = srv.use_tls_config ? &tls_input_config : NULL,
  };

  PulseError err = pulse_rtmp_session_connect_input (application->client, kListenerSlot, &config);
  if (err != PULSE_SUCCESS) {
    PEX_LOG_WARNING ("rtmp_server::_start: pulse_rtmp_session_connect_input failed: %s", pulse_strerror (err));
    return err;
  }

  PEX_LOG_DEBUG ("rtmp_server::_start: listener up on slot=%s port=%u tls=%d (%s)",
             pulse_media_content_to_string (kListenerSlot), srv.listening_port, srv.use_tls,
             lazy ? "lazy" : "explicit");
  srv.is_connected = true;
  srv.lazy_started = lazy;
  srv.connected_media_content = kListenerSlot;
  return PULSE_SUCCESS;
}

/* Tear down the listener. Idempotent. */
static PulseError
_stop (PexNinja * application)
{
  auto & srv = application->state.rtmp_server;
  if (!srv.is_connected)
    return PULSE_SUCCESS;

  PulseError err = pulse_rtmp_session_disconnect_input (application->client, srv.connected_media_content);
  if (err != PULSE_SUCCESS) {
    PEX_LOG_WARNING ("rtmp_server::_stop: pulse_rtmp_session_disconnect_input failed: %s", pulse_strerror (err));
    return err;
  }
  PEX_LOG_DEBUG ("rtmp_server::_stop: listener down (was on slot=%s)",
             pulse_media_content_to_string (srv.connected_media_content));
  srv.is_connected = false;
  srv.lazy_started = false;
  /* Clear transient publisher state — a fresh listener starts with
   * no publishers, regardless of what the previous one had. */
  {
    std::lock_guard<std::mutex> lock (srv.mu);
    srv.live_publishers = 0;
  }
  return PULSE_SUCCESS;
}

/* Acquire ownership of the listener for @source_id. Only one
 * Compositor source can own the listener at a time (the Pulse RTMP
 * API exposes one path per listener). Re-acquiring for the same
 * @source_id is a no-op success. Returns false if another source
 * already owns it. */
static bool
_acquire_owner (PexNinja * application, uint32_t source_id)
{
  if (source_id == 0)
    return false;
  std::lock_guard<std::mutex> lock (application->state.rtmp_server.mu);
  auto & owner = application->state.rtmp_server.owner_source_id;
  if (owner != 0 && owner != source_id)
    return false;
  owner = source_id;
  return true;
}

/* Release ownership iff @source_id currently holds it. Releasing
 * also clears the live-publisher counter — the source card that
 * cared about it is going away. */
static void
_release_owner (PexNinja * application, uint32_t source_id)
{
  std::lock_guard<std::mutex> lock (application->state.rtmp_server.mu);
  if (application->state.rtmp_server.owner_source_id == source_id) {
    application->state.rtmp_server.owner_source_id = 0;
    application->state.rtmp_server.live_publishers = 0;
  }
}

/* True while at least one publisher is currently publishing on the
 * configured server-wide path. */
static bool
_is_live (PexNinja * application)
{
  std::lock_guard<std::mutex> lock (application->state.rtmp_server.mu);
  return application->state.rtmp_server.live_publishers > 0;
}

/* True iff a Compositor source currently owns the listener. Used by
 * `_release` to decide whether to lazy-stop. */
static bool
_has_owner (PexNinja * application)
{
  std::lock_guard<std::mutex> lock (application->state.rtmp_server.mu);
  return application->state.rtmp_server.owner_source_id != 0;
}

} /* namespace rtmp_server */

/* ----------------------------------------------------------------------------
 * Source Library helpers (kind labels + materialise/release).
 *
 * The standalone Source Library window that shipped in Phase 2a has
 * been retired — its functionality is now folded into the Compositor
 * sources rail. The helpers below survive because the Compositor
 * still needs to materialise / release library entries on demand. */

namespace source_library
{

static const char *
_kind_label (PexNinjaState::PexNinjaSourceLibrary::Kind k)
{
  using SL = PexNinjaState::PexNinjaSourceLibrary;
  switch (k) {
    case SL::kCamera:
      return "Camera";
    case SL::kMp4:
      return "MP4 file";
    case SL::kImage:
      return "Image (JPEG/PNG)";
    case SL::kRtmp:
      return "RTMP stream";
    case SL::kRtsp:
      return "RTSP stream";
    case SL::kDesktop:
      return "Desktop capture";
    case SL::kWindow:
      return "Window capture";
    case SL::kGfx:
      return "Annotation overlay";
  }
  return "?";
}

/* Pretty default name for a freshly-added source. We dedupe later by
 * appending " #N" if needed. */
static const char *
_kind_default_name (PexNinjaState::PexNinjaSourceLibrary::Kind k)
{
  using SL = PexNinjaState::PexNinjaSourceLibrary;
  switch (k) {
    case SL::kCamera:
      return "Camera";
    case SL::kMp4:
      return "MP4";
    case SL::kImage:
      return "Image";
    case SL::kRtmp:
      return "RTMP";
    case SL::kRtsp:
      return "RTSP";
    case SL::kDesktop:
      return "Desktop";
    case SL::kWindow:
      return "Window";
    case SL::kGfx:
      return "Annotation";
  }
  return "Source";
}

/* RTSP plumbing — each kRtsp source dials its own camera URL and
 * gets its own opaque PulseRtspSessionID, so there is no shared
 * "listener" object and no slot/owner contention to manage. The
 * Compositor places sessions onto canvas regions purely via the
 * mix-API, which is content-agnostic. */

/* Materialise a Source into a PulseVideoMixInputID by dispatching to
 * the matching pulse_video_mix_input_from_*() factory. Returns the
 * Pulse error code so the caller can surface it in the UI. On success,
 * src.input_id is updated; on failure it is left untouched. */
static PulseError
_materialise (PexNinja * application, PexNinjaState::PexNinjaSourceLibrary::Source & src)
{
  using SL = PexNinjaState::PexNinjaSourceLibrary;

  if (src.input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE)
    return PULSE_SUCCESS; /* idempotent */

  PulseVideoMixInputID id = PULSE_VIDEO_MIX_INPUT_ID_NONE;
  PulseError err = PULSE_SUCCESS;

  switch (src.kind) {
    case SL::kCamera:
    {
      /* camera_devices is guarded by devices_mutex elsewhere; the UI
       * path that calls this runs on the main thread and the device
       * list is mutated only via UI-triggered callbacks that also run
       * there. Take the lock anyway to be safe. */
      std::lock_guard<std::mutex> lock (devices_mutex);
      if (camera_devices.empty () || src.camera_idx < 0 || src.camera_idx >= (int)camera_devices.size ()) {
        return PULSE_ERROR_INVALID_PARAMETER;
      }
      err = pulse_video_mix_input_from_device (application->client, camera_devices[src.camera_idx], &id);
      if (err == PULSE_SUCCESS)
        src.materialised_camera_idx = src.camera_idx;
      break;
    }
    case SL::kMp4:
    {
      if (src.file_path[0] == '\0')
        return PULSE_ERROR_INVALID_PARAMETER;
      err = pulse_video_mix_input_from_file_with_loop (application->client, src.file_path, src.loop, &id);
      break;
    }
    case SL::kImage:
    {
      if (src.file_path[0] == '\0')
        return PULSE_ERROR_INVALID_PARAMETER;
      err = pulse_video_mix_input_from_file (application->client, src.file_path, &id);
      break;
    }
    case SL::kRtmp:
    {
      /* Lazy-start the shared RTMP listener if it isn't already up
       * (the user may never have opened the standalone "RTMP Server"
       * window). Then claim ownership so the accept callback admits
       * publishers on the configured server-wide path. */
      auto & srv = application->state.rtmp_server;
      if (srv.path[0] == '\0') {
        PEX_LOG_WARNING ("source_library: kRtmp source '%s' cannot start: server path is empty "
                     "(configure one in Menu → RTMP Server)",
                     src.name);
        return PULSE_ERROR_INVALID_PARAMETER;
      }
      /* Only one Compositor RTMP source can own the listener at a
       * time — the underlying Pulse RTMP API exposes one path per
       * listener, so two sources can't meaningfully share it. */
      if (!rtmp_server::_acquire_owner (application, src.id)) {
        PEX_LOG_WARNING ("source_library: kRtmp source '%s' cannot start: another RTMP source "
                     "is already using the listener",
                     src.name);
        return PULSE_ERROR_INVALID_PARAMETER;
      }
      if (!srv.is_connected) {
        err = rtmp_server::_start (application, /*lazy=*/true);
        if (err != PULSE_SUCCESS) {
          rtmp_server::_release_owner (application, src.id);
          return err;
        }
      }
      err = pulse_video_mix_input_from_rtmp_session (application->client, srv.connected_media_content, &id);
      if (err != PULSE_SUCCESS) {
        /* Materialise failed — release ownership so a retry (or a
         * different source) can take it, and lazy-stop if we own
         * the listener's lifetime. */
        rtmp_server::_release_owner (application, src.id);
        if (srv.lazy_started)
          rtmp_server::_stop (application);
      }
      break;
    }
    case SL::kRtsp:
    {
      /* Per-source dial-out: each kRtsp source carries its own URL,
       * transport, and (optional) credentials, and gets its own
       * opaque PulseRtspSessionID. RTSP is a content source, not a
       * slot, so multiple kRtsp sources (i.e. multiple cameras) can
       * be materialised concurrently — they don't fight over any
       * shared resource here.
       *
       * NOTE: pulse_rtsp_session_connect_input is synchronous — it
       * blocks the calling thread (here, the ImGui main loop) until
       * the camera SDP has been negotiated or the dial fails. For
       * the demo this is acceptable; a future iteration could move
       * the dial onto a worker and surface progress in the UI.
       *
       * Two-phase materialisation:
       *   1. Connect the RTSP session (if not already connected) and
       *      cache the SDP-derived video stream list onto the Source.
       *      With exactly one video stream we auto-pick it; with more
       *      than one we leave @input_id at NONE and bail out
       *      successfully so the UI can render the stream picker.
       *   2. Once @rtsp_selected_stream_id is set, bind that specific
       *      stream into the mixer via
       *      pulse_video_mix_input_from_rtsp_stream(). Subsequent
       *      _eager_materialise calls land here directly. */
      if (src.rtsp_session_id == PULSE_RTSP_SESSION_ID_NONE) {
        if (src.rtsp_url[0] == '\0') {
          snprintf (src.rtsp_last_error, sizeof (src.rtsp_last_error), "URL is empty");
          return PULSE_ERROR_INVALID_PARAMETER;
        }

        PulseRtspAuthConfig auth_config = {.username = src.rtsp_username, .password = src.rtsp_password};
        const bool have_auth = (src.rtsp_username[0] != '\0');
        PulseRtspInputConfig config = {
          .location = src.rtsp_url,
          .transport = (PulseRtspTransport)src.rtsp_transport,
          .latency_ms = (uint32_t)src.rtsp_latency_ms,
          .callbacks = {.input_added_cb = NULL, .disconnect_cb = NULL, .user_context = NULL},
          .auth_config = have_auth ? &auth_config : NULL,
        };
        PulseRtspSessionID session_id = PULSE_RTSP_SESSION_ID_NONE;
        err = pulse_rtsp_session_connect_input (application->client, &config, &session_id);
        if (err != PULSE_SUCCESS) {
          snprintf (src.rtsp_last_error, sizeof (src.rtsp_last_error), "%s", pulse_strerror (err));
          PEX_LOG_WARNING ("source_library: kRtsp source '%s' connect failed (%s): %s", src.name, src.rtsp_url,
                       pulse_strerror (err));
          break;
        }
        src.rtsp_session_id = session_id;

        /* Snapshot the camera's video streams once, right after the
         * SDP has been negotiated. We don't keep the iterator alive
         * past this scope because its #PulseRtspStream pointers are
         * invalidated by pulse_rtsp_stream_iterator_free(); instead we
         * copy each stream's id + display label + codec into a small
         * value-type cache that the UI owns for the rest of the
         * session's lifetime. */
        src.rtsp_video_streams.clear ();
        src.rtsp_selected_stream_id = PULSE_RTSP_STREAM_ID_NONE;
        PulseRtspStreamIterator * it = NULL;
        PulseError it_err =
          pulse_rtsp_session_stream_iterator_new (application->client, session_id, PULSE_MEDIA_VIDEO, &it);
        if (it_err == PULSE_SUCCESS && it != NULL) {
          for (const PulseRtspStream * s = pulse_rtsp_stream_iterator_first (it); s != NULL;
               s = pulse_rtsp_stream_iterator_next (it)) {
            SL::Source::RtspStreamChoice choice = {};
            choice.id = pulse_rtsp_stream_get_id (s);
            const char * sname = pulse_rtsp_stream_get_name (s);
            snprintf (choice.name, sizeof (choice.name), "%s", sname ? sname : "");
            const char * scodec = pulse_rtsp_stream_get_codec (s);
            snprintf (choice.codec, sizeof (choice.codec), "%s", scodec ? scodec : "");
            src.rtsp_video_streams.push_back (choice);
          }
          pulse_rtsp_stream_iterator_free (it);
        }

        if (src.rtsp_video_streams.empty ()) {
          snprintf (src.rtsp_last_error, sizeof (src.rtsp_last_error), "no video streams advertised");
          PEX_LOG_WARNING ("source_library: kRtsp source '%s' has no video streams", src.name);
          /* Tear the dial back down so a later retry starts clean. */
          pulse_rtsp_session_disconnect_input (application->client, session_id);
          src.rtsp_session_id = PULSE_RTSP_SESSION_ID_NONE;
          err = PULSE_ERROR_NOT_CONFIGURED;
          break;
        }

        /* One-stream camera: skip the picker, auto-bind. Multi-stream
         * cameras (NVRs, dual main+sub IP cameras, …) fall through to
         * the success-with-NONE path, which leaves the source un-bound
         * and lets the UI render a dropdown of choices. */
        if (src.rtsp_video_streams.size () == 1) {
          src.rtsp_selected_stream_id = src.rtsp_video_streams[0].id;
        } else {
          src.rtsp_last_error[0] = '\0';
          /* Success — connected but awaiting a stream pick from the user. */
          return PULSE_SUCCESS;
        }
      }

      /* By this point the session is connected and a stream has been
       * chosen (either auto-picked above or set by the UI dropdown
       * before re-invoking _materialise). Bind it into the mixer. */
      if (src.rtsp_selected_stream_id == PULSE_RTSP_STREAM_ID_NONE) {
        /* Connected but no choice yet — UI will surface the picker. */
        return PULSE_SUCCESS;
      }
      err = pulse_video_mix_input_from_rtsp_stream (application->client, src.rtsp_session_id,
                                                    src.rtsp_selected_stream_id, &id);
      if (err != PULSE_SUCCESS) {
        snprintf (src.rtsp_last_error, sizeof (src.rtsp_last_error), "bind stream failed: %s", pulse_strerror (err));
        PEX_LOG_WARNING ("source_library: kRtsp source '%s' stream-bind failed: %s", src.name, pulse_strerror (err));
        break;
      }
      src.rtsp_last_error[0] = '\0';
      break;
    }
    case SL::kDesktop:
    {
      err = pulse_video_mix_input_from_desktop (application->client, src.desktop_handle, PULSE_DISPLAY, &id);
      break;
    }
    case SL::kWindow:
    {
      err = pulse_video_mix_input_from_desktop (application->client, src.desktop_handle, PULSE_WINDOW, &id);
      break;
    }
    case SL::kGfx:
    {
      if (src.gfx_width <= 0 || src.gfx_height <= 0)
        return PULSE_ERROR_INVALID_PARAMETER;
      err = pulse_video_mix_input_from_annotation (application->client, (uint32_t)src.gfx_width,
                                                   (uint32_t)src.gfx_height, &id);
      /* Push the user's whiteboard preference straight after acquire
       * so a re-materialised source comes back up with the same
       * background it had when it was last released. Annotation IDs
       * share the numeric space with PulseVideoMixInputID so we can
       * pass @id directly to pulse_annotation_set_background. */
      if (err == PULSE_SUCCESS) {
        const uint8_t r = (uint8_t)std::round (std::min (std::max (src.gfx_bg_color[0], 0.0f), 1.0f) * 255.0f);
        const uint8_t g = (uint8_t)std::round (std::min (std::max (src.gfx_bg_color[1], 0.0f), 1.0f) * 255.0f);
        const uint8_t b = (uint8_t)std::round (std::min (std::max (src.gfx_bg_color[2], 0.0f), 1.0f) * 255.0f);
        const uint8_t a = (uint8_t)std::round (std::min (std::max (src.gfx_bg_color[3], 0.0f), 1.0f) * 255.0f);
        pulse_annotation_set_background (application->client, id, src.gfx_bg_enabled, r, g, b, a);
      }
      break;
    }
  }

  if (err == PULSE_SUCCESS)
    src.input_id = id;
  return err;
}

/* Release a previously-materialised Source. Idempotent. */
static void
_release (PexNinja * application, PexNinjaState::PexNinjaSourceLibrary::Source & src)
{
  using SL = PexNinjaState::PexNinjaSourceLibrary;
  /* The mix-input release is conditional — for kRtsp sources we may
   * be in the "connected, awaiting stream pick" state where the
   * session is up but no PulseVideoMixInputID has been minted yet.
   * The per-kind teardown below still needs to fire to drop the
   * RTSP session itself. */
  if (src.input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    PulseError err = pulse_video_mix_input_release (application->client, src.input_id);
    if (err != PULSE_SUCCESS)
      PEX_LOG_WARNING ("source_library: pulse_video_mix_input_release failed for source '%s': %s", src.name,
                   pulse_strerror (err));
    src.input_id = PULSE_VIDEO_MIX_INPUT_ID_NONE;
    /* Forget which device the (now-released) input was bound to so the
     * next _materialise re-stamps materialised_camera_idx fresh. */
    src.materialised_camera_idx = -1;
  }

  /* RTMP-specific cleanup: release this source's ownership of the
   * shared listener so the accept callback stops admitting publishers,
   * and lazy-stop the listener if we own its lifetime. */
  if (src.kind == SL::kRtmp) {
    rtmp_server::_release_owner (application, src.id);
    if (application->state.rtmp_server.lazy_started && !rtmp_server::_has_owner (application))
      rtmp_server::_stop (application);
  }

  /* RTSP-specific cleanup: tear down this source's dial. With the
   * opaque-session-id model, each kRtsp source owns its own session
   * outright — no shared-slot bookkeeping required. We also drop the
   * cached SDP-derived stream list and any stream the user had picked,
   * so that the next Connect re-snapshots whatever the camera offers
   * (the URL or the camera itself may have changed in the meantime). */
  if (src.kind == SL::kRtsp && src.rtsp_session_id != PULSE_RTSP_SESSION_ID_NONE) {
    PulseError err = pulse_rtsp_session_disconnect_input (application->client, src.rtsp_session_id);
    if (err != PULSE_SUCCESS && err != PULSE_ERROR_NOT_CONFIGURED)
      PEX_LOG_WARNING ("source_library: pulse_rtsp_session_disconnect_input failed for source '%s': %s", src.name,
                   pulse_strerror (err));
    src.rtsp_session_id = PULSE_RTSP_SESSION_ID_NONE;
    src.rtsp_video_streams.clear ();
    src.rtsp_selected_stream_id = PULSE_RTSP_STREAM_ID_NONE;
  }
}

} /* namespace source_library */

/* ----------------------------------------------------------------------------
 * Shared file-picker plumbing.
 *
 * The Compositor sources rail needs a way to point an MP4 / Image
 * source at a file on disk. Rather than threading an ImGuiFileBrowser
 * through every per-source row, we keep a single static instance plus
 * a tiny "pending request" record that says *which* char buffer the
 * selected path should be written into. The browser is rendered once
 * at the end of the frame; when it returns true, we copy the path and
 * clear the request.
 *
 * This works because at most one file dialog can be open at a time
 * (it's a modal popup) — so a single instance + a single pending
 * request suffices, no per-call ownership juggling. */
namespace file_picker
{

struct Request
{
  char * dest;       /* fixed-size char buffer to overwrite              */
  size_t dest_size;  /* sizeof(dest)                                     */
  std::string title; /* unique modal id; also the dialog window title    */
  std::string exts;  /* comma-separated valid extensions, e.g. ".mp4"    */
  bool just_opened;  /* true on the frame request_open was called; the
                        actual ImGui::OpenPopup call is deferred to
                        render_pending() so the popup ID stack matches
                        the BeginPopupModal call inside showFileDialog. */
};

static imgui_addons::ImGuiFileBrowser g_browser;
static Request g_request{nullptr, 0, std::string (), std::string (), false};

/* Open the browser. Safe to call from inside any window's Begin/End
 * scope — the actual OpenPopup is deferred to render_pending(). */
static void
request_open (char * dest, size_t dest_size, const char * title, const char * exts)
{
  g_request.dest = dest;
  g_request.dest_size = dest_size;
  g_request.title = title ? title : "Open File";
  g_request.exts = exts ? exts : "*.*";
  g_request.just_opened = true;
}

/* Render the open dialog (if any). Must be called every frame at the
 * top level (outside any ImGui::Begin) so the popup's ID stack is
 * stable across windows. Cheap when no request is pending. */
static void
render_pending ()
{
  if (g_request.dest == nullptr)
    return;
  if (g_request.just_opened) {
    ImGui::OpenPopup (g_request.title.c_str ());
    g_request.just_opened = false;
  }
  if (g_browser.showFileDialog (g_request.title, imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2 (700, 380),
                                g_request.exts)) {
    /* User clicked Open — copy the absolute path into the caller's
     * fixed-size buffer and snprintf-truncate to be safe. */
    snprintf (g_request.dest, g_request.dest_size, "%s", g_browser.selected_path.c_str ());
    g_request.dest = nullptr;
    g_request.dest_size = 0;
  } else if (!ImGui::IsPopupOpen (g_request.title.c_str ())) {
    /* Dialog was cancelled or closed without selection. */
    g_request.dest = nullptr;
    g_request.dest_size = 0;
  }
}

/* Convenience: a "Browse…" button that, when clicked, opens the
 * picker for the given destination buffer. PushID-safe; the caller is
 * responsible for any surrounding ImGui::PushID/PopID it needs. */
static void
browse_button (const char * label_id, char * dest, size_t dest_size, const char * title, const char * exts)
{
  if (ImGui::Button (label_id))
    request_open (dest, dest_size, title, exts);
}

} /* namespace file_picker */

/* Release every materialised library input across every canvas's
 * library. Called at application shutdown so we don't leak Pulse
 * resources when the user has parked inputs in a Library without
 * wiring them anywhere. Safe to call on a never-populated library. */
static void
pexninja_release_source_library (PexNinja * application)
{
  using CO = PexNinjaState::PexNinjaCompositor;
  for (int ci = 0; ci < (int)CO::kCanvasCount; ++ci) {
    auto & lib = application->state.compositor.canvases[ci].library;
    for (auto & src : lib.sources) {
      source_library::_release (application, src);
    }
    lib.sources.clear ();
  }
}

/* ----------------------------------------------------------------------------
 * Compositor window (Phase 2b.0).
 *
 * Skeleton authoring surface for the PGM/PVW frame compositor. See
 * the long comment on PexNinjaState::PexNinjaCompositor for the
 * model. This phase ships:
 *   - the doc model (committed_doc + editing_doc per canvas)
 *   - default-seed of Main with a "Camera fills canvas" region
 *   - inline sources rail (replaces the retired Source Library window):
 *       per-source kind selector, name, type-specific config, "+ on
 *       canvas" / Delete actions, and a "+ New" button at the top
 *   - canvas rendering of region rectangles colour-tagged by source kind
 *   - per-region layer reorder + delete
 *   - a single Take button (broadcast-style PVW→PGM transfer) that
 *     rebuilds the wire mix for the currently-selected canvas from
 *     its committed_doc and calls pulse_video_mix_connect
 *   - eager source materialisation when a region is added
 *
 * Drag/resize, per-region inspector and live preview rendering are
 * deferred to 2b.1 / 2b.2 / 2b.3 respectively. */

namespace compositor
{

static const char *
_canvas_label (PexNinjaState::PexNinjaCompositor::CanvasIdx c)
{
  using CO = PexNinjaState::PexNinjaCompositor;
  switch (c) {
    case CO::kCanvasMain:
      return "Main";
    case CO::kCanvasPreso:
      return "Preso";
    case CO::kCanvasCount:
      break;
  }
  return "?";
}

/* Colour-tag regions by the kind of source they are bound to so the
 * canvas reads at a glance. Returns ImU32 in IM_COL32 format. The
 * palette is intentionally muted so labels stay readable; the active
 * region gets a brighter border highlight elsewhere. */
static ImU32
_region_fill (PexNinjaState::PexNinjaSourceLibrary::Kind k)
{
  using SL = PexNinjaState::PexNinjaSourceLibrary;
  switch (k) {
    case SL::kCamera:
      return IM_COL32 (60, 110, 180, 200);
    case SL::kMp4:
      return IM_COL32 (70, 150, 90, 200);
    case SL::kImage:
      return IM_COL32 (140, 90, 170, 200);
    case SL::kRtmp:
      return IM_COL32 (200, 130, 60, 200);
    case SL::kRtsp:
      return IM_COL32 (180, 80, 80, 200);
    case SL::kDesktop:
    case SL::kWindow:
      return IM_COL32 (60, 150, 160, 200);
    case SL::kGfx:
      return IM_COL32 (200, 190, 60, 200);
  }
  return IM_COL32 (120, 120, 120, 200);
}

/* Resolve a Region's source_lib_id to the matching SourceLibrary
 * entry, scoped to the canvas that owns the region. Returns nullptr
 * if the entry was deleted from that canvas's Library after the
 * region was created — the caller renders an "(unbound)"
 * placeholder in that case.
 *
 * Source IDs are unique only within a canvas (each canvas's
 * library has its own next_id starting at 1), so the canvas index
 * is required to disambiguate. */
static PexNinjaState::PexNinjaSourceLibrary::Source *
_lookup_source (PexNinja * application, int canvas_idx, uint32_t source_lib_id)
{
  using CO = PexNinjaState::PexNinjaCompositor;
  if (source_lib_id == 0)
    return nullptr;
  if (canvas_idx < 0 || canvas_idx >= (int)CO::kCanvasCount)
    return nullptr;
  for (auto & s : application->state.compositor.canvases[canvas_idx].library.sources) {
    if (s.id == source_lib_id)
      return &s;
  }
  return nullptr;
}

/* Eager materialise: when a region is first added pointing at a
 * library source, warm the source so the (forthcoming) preview can
 * show real frames even though the on-the-wire mix doesn't reference
 * it yet. Idempotent — _materialise short-circuits on already-warm
 * sources. */
static void
_eager_materialise (PexNinja * application, int canvas_idx, uint32_t source_lib_id)
{
  auto * src = _lookup_source (application, canvas_idx, source_lib_id);
  if (!src)
    return;
  PulseError err = source_library::_materialise (application, *src);
  if (err != PULSE_SUCCESS) {
    PEX_LOG_WARNING ("compositor: eager materialise failed for source '%s' (%s): %s", src->name,
                 source_library::_kind_label (src->kind), pulse_strerror (err));
    application->state.error_msg = pulse_strerror (err);
  }
}

/* Pick the next unused per-kind index for naming a new source. The
 * library numbers each kind independently — "Camera 1", "Camera 2",
 * "MP4 1" — so users can tell their sources apart at a glance. We
 * count existing sources of the same kind and add one; this is naive
 * (re-uses the index of a deleted source if there's a hole) but it's
 * fine for an authoring UI where users rarely delete then re-add. */
static int
_next_kind_index (const PexNinjaState::PexNinjaSourceLibrary & lib, PexNinjaState::PexNinjaSourceLibrary::Kind kind)
{
  int n = 1;
  for (const auto & s : lib.sources) {
    if (s.kind == kind)
      ++n;
  }
  return n;
}

/* Create a new SourceLibrary entry inside the given canvas's library,
 * of the given kind, with sensible defaults + a unique-per-kind name.
 * Returns the new source's id (the UI-stable handle within the
 * canvas, NOT the PulseVideoMixInputID — that's set later by
 * _materialise). */
static uint32_t
_create_source (PexNinja * application, int canvas_idx, PexNinjaState::PexNinjaSourceLibrary::Kind kind)
{
  using SL = PexNinjaState::PexNinjaSourceLibrary;
  using CO = PexNinjaState::PexNinjaCompositor;
  if (canvas_idx < 0 || canvas_idx >= (int)CO::kCanvasCount)
    return 0;
  auto & lib = application->state.compositor.canvases[canvas_idx].library;
  SL::Source s;
  s.id = lib.next_id++;
  s.kind = kind;
  snprintf (s.name, sizeof (s.name), "%s %d", source_library::_kind_default_name (kind), _next_kind_index (lib, kind));
  lib.sources.push_back (std::move (s));
  return lib.sources.back ().id;
}

/* Build a PulseVideoMixConfig from a canvas's committed_doc. Regions
 * with an unbound source (source_lib_id == 0) or a not-yet-materialised
 * source (input_id == NONE) are silently skipped — they will simply
 * not appear in the wire output. The caller owns `inputs[]` and must
 * keep it alive until pulse_video_mix_connect returns. Returned
 * config has num_inputs == 0 if nothing made it through, in which
 * case the caller should pulse_video_mix_disconnect instead.
 *
 * `inputs_cap` bounds how many regions we serialise; further regions
 * are dropped with a warning. The cap is the array size the caller
 * stack-allocated.
 *
 * IMPORTANT: the produced inputs[] is sorted by layer ASCENDING (with
 * a stable secondary tie-break on input_id). This is non-negotiable —
 * the downstream JSON layout generator (pulse_layout_json_generator.c)
 * emits slots in (layer asc, then input-array iteration order within
 * the layer), and the wire binds input[i] → slot[i] POSITIONALLY (no
 * slot-id is sent in the protocol). If we naively serialised
 * doc.regions in vector-insertion order, then a layer-swap that just
 * twiddles the `layer` field on two regions would re-order the slots
 * in the JSON but not the inputs[] array — so the camera input would
 * end up bound to the desktop's slot geometry and v.v., flipping the
 * region CONTENTS on the wire (not just the z-order). */
static PulseVideoMixConfig
_build_mix_config_from_doc (PexNinja * application, int canvas_idx,
                            const PexNinjaState::PexNinjaCompositor::VideoMixDoc & doc, PulseVideoMixInput * inputs,
                            size_t inputs_cap)
{
  size_t n = 0;
  for (const auto & r : doc.regions) {
    if (n >= inputs_cap) {
      PEX_LOG_WARNING ("compositor: doc has more regions (%zu) than inputs cap (%zu); truncating", doc.regions.size (),
                   inputs_cap);
      break;
    }
    auto * src = _lookup_source (application, canvas_idx, r.source_lib_id);
    if (!src || src->input_id == PULSE_VIDEO_MIX_INPUT_ID_NONE)
      continue;
    inputs[n++] = {
      .input_id = src->input_id,
      .layer = r.layer,
      .width_ratio = r.width_ratio,
      .height_ratio = r.height_ratio,
      .x_centrepoint = r.x_centrepoint,
      .y_centrepoint = r.y_centrepoint,
      .videoproc_mask = r.videoproc_mask,
    };
  }
  /* Sort by layer asc; stable tie-break on input_id keeps two regions
   * on the same layer in a deterministic side-by-side order regardless
   * of which one was added first. std::stable_sort is overkill here
   * (n is at most a few dozen) but spelling out the tie-break in the
   * comparator makes the intent obvious to the next reader. */
  std::sort (inputs, inputs + n, [] (const PulseVideoMixInput & a, const PulseVideoMixInput & b) {
    if (a.layer != b.layer)
      return a.layer < b.layer;
    return a.input_id < b.input_id;
  });
  return PulseVideoMixConfig{.num_inputs = n, .inputs = inputs};
}

/* Translate a CanvasIdx to its on-the-wire PulseMediaContent. */
static PulseMediaContent
_canvas_media_content (PexNinjaState::PexNinjaCompositor::CanvasIdx c)
{
  using CO = PexNinjaState::PexNinjaCompositor;
  return c == CO::kCanvasPreso ? PULSE_MEDIA_CONTENT_PRESENTATION : PULSE_MEDIA_CONTENT_MAIN;
}

/* Apply the canvas's committed_doc to the wire: disconnect any
 * existing session on the target media_content and reconnect with the
 * new config. This is the actual broadcast-switcher Take operation —
 * called from the Take buttons and from auto-take whenever the
 * editing_doc has just been promoted into committed_doc.
 *
 * Note: this overrides whatever the legacy slot-based UI had connected
 * on the same media_content. That's by design — once the user opts
 * into the Compositor by hitting Take, the compositor owns the wire
 * for that canvas. The legacy UI's `vm.active` flag is left untouched
 * so its disconnect-at-shutdown path still no-ops harmlessly; we
 * track our own ownership in `comp.connected_on_wire[]` so the
 * shutdown handler can disconnect cleanly. */
static void
_apply_canvas (PexNinja * application, PexNinjaState::PexNinjaCompositor::CanvasIdx c)
{
  using SL = PexNinjaState::PexNinjaSourceLibrary;
  auto & comp = application->state.compositor;
  PulseMediaContent media_content = _canvas_media_content (c);

  /* Always disconnect first — pulse_video_mix_connect rejects with
   * PULSE_ERROR_UNEXPECTED_STATE if there's already a session on the
   * same media_content, so re-applying without disconnect would
   * silently no-op. We swallow the disconnect error because nothing
   * may be connected (first Take, or after a previous empty doc).
   *
   * Crucially this also has to happen BEFORE any
   * pulse_video_mix_input_release on inputs that were part of the
   * old session — releasing an in-use input deadlocks against the
   * compositor session that's still holding it. The legacy Settings
   * dialog camera-hot-swap path gets away with a bare release-then-
   * acquire because the hot-swap mutates the IDs the next mix
   * iteration will re-read; the Compositor owns the session
   * directly so we have to tear it down first. */
  pulse_video_mix_disconnect (application->client, media_content);

  /* Sweep stale source-content edits now that the wire is no longer
   * holding the inputs open. Today only kCamera carries a "live"
   * config knob (the device choice) — release the stale input and
   * the subsequent _build_mix_config_from_doc / _materialise pair
   * picks up the new device from src.camera_idx.
   *
   * We only act on sources actually referenced by committed_doc;
   * library entries that aren't on this canvas's wire need no
   * intervention (they'll re-materialise lazily next time they're
   * used). */
  for (const auto & r : comp.canvases[(int)c].committed.regions) {
    auto * src = _lookup_source (application, (int)c, r.source_lib_id);
    if (!src)
      continue;
    if (src->kind == SL::kCamera && src->input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE &&
        src->materialised_camera_idx != -1 && src->materialised_camera_idx != src->camera_idx) {
      source_library::_release (application, *src);
      PulseError remat_err = source_library::_materialise (application, *src);
      if (remat_err != PULSE_SUCCESS) {
        PEX_LOG_WARNING ("compositor: camera re-materialise failed for source '%s': %s", src->name,
                     pulse_strerror (remat_err));
        application->state.error_msg = pulse_strerror (remat_err);
        /* Best-effort: continue with whatever inputs we have. The
         * region for this source will silently drop out of the
         * config (input_id is NONE after a failed remat). */
      }
    }
  }

  /* Build the new config. Stack-allocate generously: the soft cap of
   * 16 regions is well above what users will sanely compose, and even
   * a pathological doc just gets truncated with a log line. */
  constexpr size_t kMaxInputs = 16;
  PulseVideoMixInput inputs[kMaxInputs];
  PulseVideoMixConfig config =
    _build_mix_config_from_doc (application, (int)c, comp.canvases[(int)c].committed, inputs, kMaxInputs);

  if (config.num_inputs == 0) {
    /* Doc is effectively empty (all regions unbound) — nothing to
     * connect, but flag the wire as no longer compositor-owned so we
     * don't try to disconnect a non-session at shutdown. */
    comp.connected_on_wire[(int)c] = false;
    return;
  }

  PulseError err = pulse_video_mix_connect (application->client, &config, media_content);
  if (err != PULSE_SUCCESS) {
    PEX_LOG_WARNING ("compositor: apply canvas %s failed: %s", _canvas_label (c), pulse_strerror (err));
    application->state.error_msg = pulse_strerror (err);
    comp.connected_on_wire[(int)c] = false;
    return;
  }
  comp.connected_on_wire[(int)c] = true;
}

/* On the very first open of the Compositor window, seed Main's
 * editing_doc AND committed_doc with a single "Camera fills canvas"
 * region so PexNinja users see exactly what they expect before they
 * start playing. If the SourceLibrary contains no Camera entry yet,
 * create one bound to camera index 0 so the user has a visible source
 * tile in the rail to start composing with. */
static void
_default_seed_if_needed (PexNinja * application)
{
  using CO = PexNinjaState::PexNinjaCompositor;
  using SL = PexNinjaState::PexNinjaSourceLibrary;

  auto & comp = application->state.compositor;
  if (comp.default_seeded)
    return;
  comp.default_seeded = true;

  auto & main_canvas = comp.canvases[CO::kCanvasMain];
  auto & lib = main_canvas.library;

  /* Find or create a Camera entry in MAIN's library. Preso starts
   * with an empty library — the user gets a clean slate for the
   * presentation stream and can pick whatever sources they want. */
  uint32_t cam_id = 0;
  for (const auto & s : lib.sources) {
    if (s.kind == SL::kCamera) {
      cam_id = s.id;
      break;
    }
  }
  if (cam_id == 0) {
    cam_id = _create_source (application, (int)CO::kCanvasMain, SL::kCamera);
    /* Pin to the first available camera. _create_source defaults
     * camera_idx to 0 via the Source constructor. */
  }

  /* Build the default Region — fills the entire canvas, layer 0. */
  CO::Region r;
  r.id = comp.next_region_id++;
  r.source_lib_id = cam_id;
  r.layer = 0;
  r.width_ratio = 1.0;
  r.height_ratio = 1.0;
  r.x_centrepoint = 0.5;
  r.y_centrepoint = 0.5;
  r.videoproc_mask = PULSE_VIDEO_PROCESS_TYPE_NONE;

  main_canvas.editing.regions.push_back (r);
  main_canvas.committed.regions.push_back (r);

  /* Eager warm so the preview immediately shows camera frames;
   * harmless if camera_devices isn't populated yet — _materialise
   * will return INVALID_PARAMETER and we just log it. */
  _eager_materialise (application, (int)CO::kCanvasMain, cam_id);
}

/* Add a region bound to a SourceLibrary entry into the active canvas's
 * editing_doc. Newly-added regions appear half-canvas centred so they
 * don't fully eclipse what's underneath. The first region added to an
 * empty canvas defaults to filling the canvas (so casual users get a
 * visible result without touching geometry). Layer auto-assigns to one
 * above the current top. Eager-materialises the source. */
static void
_add_region_to_active_canvas (PexNinja * application, uint32_t source_lib_id)
{
  using CO = PexNinjaState::PexNinjaCompositor;

  auto & comp = application->state.compositor;
  if (comp.active_canvas < 0 || comp.active_canvas >= (int)CO::kCanvasCount)
    return;
  auto & doc = comp.canvases[comp.active_canvas].editing;

  CO::Region r;
  r.id = comp.next_region_id++;
  r.source_lib_id = source_lib_id;

  /* Auto-layer: top of stack + 1. */
  int top_layer = -1;
  for (const auto & existing : doc.regions) {
    if (existing.layer > top_layer)
      top_layer = existing.layer;
  }
  r.layer = top_layer + 1;

  if (doc.regions.empty ()) {
    /* First region: fill the canvas, matches the default-seed shape. */
    r.width_ratio = 1.0;
    r.height_ratio = 1.0;
  } else {
    /* Subsequent regions: half-size centred so user can see the new
     * region without drag/resize (which arrives in 2b.1). */
    r.width_ratio = 0.5;
    r.height_ratio = 0.5;
  }
  r.x_centrepoint = 0.5;
  r.y_centrepoint = 0.5;

  /* Inherit the source's segmentation/blur intent so a freshly-added
   * region honours the user's "segment this Camera" toggle without
   * a second click. */
  if (auto * src = _lookup_source (application, comp.active_canvas, source_lib_id))
    r.videoproc_mask = src->videoproc_mask;

  doc.regions.push_back (r);
  comp.selected_region_id = r.id;

  _eager_materialise (application, comp.active_canvas, source_lib_id);
}

/* Push a source's videoproc_mask down to every region in the SAME
 * canvas that references it. Called from the rail checkbox handlers
 * so toggling Segmentation on a Camera in Main fans out to every
 * Main region using that Camera — but does NOT cross over to Preso
 * (Preso has its own independent library). */
static void
_sync_source_mask_to_regions (PexNinja * application, int canvas_idx, uint32_t source_lib_id)
{
  using CO = PexNinjaState::PexNinjaCompositor;

  auto & comp = application->state.compositor;
  auto * src = _lookup_source (application, canvas_idx, source_lib_id);
  if (!src)
    return;
  if (canvas_idx < 0 || canvas_idx >= (int)CO::kCanvasCount)
    return;
  auto & doc = comp.canvases[canvas_idx].editing;
  for (auto & r : doc.regions) {
    if (r.source_lib_id == source_lib_id)
      r.videoproc_mask = src->videoproc_mask;
  }
}

/* Take = atomic copy editing -> committed for the given canvas. The
 * caller is expected to follow up with _apply_canvas() to push the
 * new committed_doc onto the wire. We split the doc copy from the
 * wire apply so callers like Take Both can serialise both copies
 * before triggering both pulse_video_mix_connect calls (the eventual
 * 2b.5 atomic-dual-take work). */
static void
_take (PexNinjaState::PexNinjaCompositor & comp, PexNinjaState::PexNinjaCompositor::CanvasIdx c)
{
  comp.canvases[(int)c].committed = comp.canvases[(int)c].editing;
}

/* True iff the canvas has uncommitted edits — drives the per-tab
 * "On Air" / dirty marker.
 *
 * "Dirty" covers two flavours:
 *   1. Layout edits — editing.regions vs committed.regions differ
 *      (positions, layers, sizes, source_lib_id bindings).
 *   2. Pending source-content edits — e.g. the user picked a new
 *      camera device on a kCamera source. The doc is byte-equal in
 *      both directions (same source_lib_id), but materialised_camera_idx
 *      no longer matches camera_idx and Take needs to release+remat
 *      the input. Without this branch the user's swap would silently
 *      sit there until they jiggled some unrelated region geometry. */
static bool
_is_dirty (const PexNinjaState::PexNinjaCompositor & comp, PexNinjaState::PexNinjaCompositor::CanvasIdx c)
{
  if (!comp.canvases[(int)c].editing.equal_to (comp.canvases[(int)c].committed))
    return true;
  using SL = PexNinjaState::PexNinjaSourceLibrary;
  const auto & lib = comp.canvases[(int)c].library;
  for (const auto & r : comp.canvases[(int)c].editing.regions) {
    for (const auto & s : lib.sources) {
      if (s.id != r.source_lib_id)
        continue;
      if (s.kind == SL::kCamera && s.materialised_camera_idx != -1 && s.materialised_camera_idx != s.camera_idx)
        return true;
      break;
    }
  }
  return false;
}

/* Convert a region's normalised (centrepoint, ratio) geometry into a
 * pixel rect on the canvas, using the *anchor-with-edge-clamp*
 * semantics that Pulse uses downstream.
 *
 * The centrepoint pair (cx, cy) is NOT the pixel center of the rect.
 * It's a position in 0..1 that the receiver maps onto the available
 * placement range [0, 1 - size] — so cx=0 → left edge flush, cx=1 →
 * right edge flush, cx=0.5 → centred. This is what makes the
 * canonical Twitch test layout (w=h=0.25, cx=cy=1.0) actually land
 * in the bottom-right CORNER instead of running off the canvas.
 *
 * A previous version used center-at-cx semantics here, which made
 * cy=0.75 (the largest the old centrepoint clamp allowed for a
 * 50%-tall region) look "all the way down" in the UI but rendered
 * on the wire as top=0.375/bottom=0.875 — leaving a visible 12.5%
 * gap below the region in the actual output.
 *
 * The math is symmetric for x: top  = cy * (canvas_h - h)
 *                            left = cx * (canvas_w - w)
 *
 * Both axes are clamped after the multiply just in case the doc
 * holds a slightly out-of-range value (e.g. from a stale region
 * after the user shrank the canvas size in a future revision). */
static void
_region_pixel_rect (const ImVec2 & canvas_p0, const ImVec2 & canvas_size,
                    const PexNinjaState::PexNinjaCompositor::Region & r, ImVec2 & out_p0, ImVec2 & out_p1)
{
  float w = (float)r.width_ratio * canvas_size.x;
  float h = (float)r.height_ratio * canvas_size.y;
  if (w > canvas_size.x)
    w = canvas_size.x;
  if (h > canvas_size.y)
    h = canvas_size.y;
  float free_w = canvas_size.x - w;
  float free_h = canvas_size.y - h;
  float cx = (float)r.x_centrepoint;
  float cy = (float)r.y_centrepoint;
  if (cx < 0.0f)
    cx = 0.0f;
  if (cx > 1.0f)
    cx = 1.0f;
  if (cy < 0.0f)
    cy = 0.0f;
  if (cy > 1.0f)
    cy = 1.0f;
  float left = canvas_p0.x + cx * free_w;
  float top = canvas_p0.y + cy * free_h;
  out_p0 = ImVec2 (left, top);
  out_p1 = ImVec2 (left + w, top + h);
}

/* Render one region as a coloured rectangle on the canvas. This is a
 * cheap placeholder for the real Approach-B preview that lands in
 * 2b.3 (which will draw thumbnails from each source's self-view
 * texture). Today it's enough to confirm geometry, layer order and
 * source binding. */
static void
_draw_region (ImDrawList * dl, const ImVec2 & canvas_p0, const ImVec2 & canvas_size,
              const PexNinjaState::PexNinjaCompositor::Region & r, PexNinjaState::PexNinjaSourceLibrary::Source * src,
              bool selected)
{
  ImVec2 p0, p1;
  _region_pixel_rect (canvas_p0, canvas_size, r, p0, p1);
  float w = p1.x - p0.x;
  float h = p1.y - p0.y;

  ImU32 fill = src ? _region_fill (src->kind) : IM_COL32 (90, 90, 90, 200);
  ImU32 border = selected ? IM_COL32 (255, 230, 80, 255) : IM_COL32 (240, 240, 240, 220);

  dl->AddRectFilled (p0, p1, fill, 4.0f);
  dl->AddRect (p0, p1, border, 4.0f, 0, selected ? 3.0f : 1.5f);

  /* Label: source name + layer + a [seg]/[blur] hint when active.
   * Hint lets the user see at a glance which regions will have
   * their background segmented/blurred at apply time without
   * opening the rail card. ASCII-only — the bundled ImGui default
   * font has no glyphs above U+00FF, so unicode badges render as
   * the missing-glyph "?". Clipped if the region is too small. */
  char label[180];
  const char * proc_hint = "";
  if (r.videoproc_mask & PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION)
    proc_hint = "  [seg]";
  else if (r.videoproc_mask & PULSE_VIDEO_PROCESS_TYPE_BLUR)
    proc_hint = "  [blur]";
  if (src) {
    snprintf (label, sizeof (label), "%s  (L%d)%s", src->name, r.layer, proc_hint);
  } else {
    snprintf (label, sizeof (label), "(unbound)  (L%d)%s", r.layer, proc_hint);
  }
  ImVec2 ts = ImGui::CalcTextSize (label);
  if (ts.x + 8.0f <= w && ts.y + 4.0f <= h) {
    dl->AddText (ImVec2 (p0.x + 4.0f, p0.y + 2.0f), IM_COL32 (255, 255, 255, 240), label);
  }
}

} /* namespace compositor */

static inline void
configure_window_compositor (PexNinja * application)
{
  using CO = PexNinjaState::PexNinjaCompositor;
  auto & comp = application->state.compositor;

  ImGui::SetNextWindowSize (ImVec2 (1100, 680), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin ("Compositor", &application->state.windows.show_compositor, ImGuiWindowFlags_None)) {
    ImGui::End ();
    return;
  }

  /* Lazy default-seed on first frame the window is visible. We only
   * seed once per process — if the user later deletes the default
   * region they get an empty canvas, not a re-seed loop. */
  compositor::_default_seed_if_needed (application);

  /* ------------------------------------------------------------ Take bar
   *
   * Single-button broadcast-style "Take": commits the currently-
   * selected canvas's editing doc to its committed_doc and pushes
   * it to the wire. Mirrors the PVW→PGM transfer button on a
   * production switcher — one knob, no ambiguity about which bus
   * you're punching live. The previous Take Main / Take Preso /
   * Take Both / Revert quartet was unloved; users only ever wanted
   * "actuate the canvas I'm staring at", which is exactly what
   * comp.active_canvas already tracks. */
  {
    const auto active = (CO::CanvasIdx)comp.active_canvas;
    const bool active_dirty = compositor::_is_dirty (comp, active);

    char take_label[64];
    snprintf (take_label, sizeof (take_label), "Take (%s)", compositor::_canvas_label (active));

    if (!active_dirty)
      imgui_begin_disabled_state ();
    if (ImGui::Button (take_label)) {
      compositor::_take (comp, active);
      compositor::_apply_canvas (application, active);
    }
    if (!active_dirty)
      imgui_end_disabled_state ();

    ImGui::SameLine (0.0f, 24.0f);
    if (ImGui::Checkbox ("Auto-take", &comp.auto_take)) {
      /* Toggling on flushes any pending edit so PGM matches PVW
       * immediately — otherwise the first-take latency would feel
       * surprising to the user. */
      if (comp.auto_take) {
        const bool m_dirty = compositor::_is_dirty (comp, CO::kCanvasMain);
        const bool p_dirty = compositor::_is_dirty (comp, CO::kCanvasPreso);
        compositor::_take (comp, CO::kCanvasMain);
        compositor::_take (comp, CO::kCanvasPreso);
        if (m_dirty)
          compositor::_apply_canvas (application, CO::kCanvasMain);
        if (p_dirty)
          compositor::_apply_canvas (application, CO::kCanvasPreso);
      }
    }
    ImGui::SameLine ();
    ImGui::TextDisabled ("(off = explicit Take, on = edits go live each frame)");

    /* ----- Presentation start/stop -----------------------------
     *
     * The compositor's preso canvas drives the pipeline mix on
     * PRESENTATION media_content (via _apply_canvas), but the
     * Pexip platform also needs an explicit *floor request* to
     * actually open the outgoing presentation stream — without
     * it our beautifully-composed preso canvas is just sitting
     * locally with nothing on the wire towards the conference.
     *
     * Mirrors the legacy "Start/Stop Presentation" menu action
     * (configure_presentation_windows / configure_menu_presentation_control):
     *   Start → take_floor + apply preso canvas to wire (so the
     *           moment the platform routes our presentation we're
     *           already broadcasting from the committed_doc)
     *   Stop  → release_floor + disconnect preso wire
     *
     * Disabled if the preso canvas is empty (nothing to present). */
    {
      auto & preso_canvas = comp.canvases[CO::kCanvasPreso];
      const bool preso_empty = preso_canvas.committed.regions.empty () && preso_canvas.editing.regions.empty ();
      const bool presenting = application->state.presenting;

      ImGui::SameLine (0.0f, 24.0f);
      if (presenting) {
        if (ImGui::Button ("Stop Presentation")) {
          /* Tear down the wire mix we own on the PRESENTATION
           * media_content, then drop the floor so the platform
           * stops routing our presentation stream. Order
           * matters: disconnect the wire first so the platform
           * doesn't briefly see "presenting with nothing". */
          pulse_video_mix_disconnect (application->client, PULSE_MEDIA_CONTENT_PRESENTATION);
          comp.connected_on_wire[(int)CO::kCanvasPreso] = false;
          pexninja_set_presenting (application, false);
        }
        if (ImGui::IsItemHovered ())
          ImGui::SetTooltip ("Release the floor and tear down the outgoing\n"
                             "presentation stream towards the Pexip platform.\n"
                             "The Preso canvas docs are kept intact so you can\n"
                             "Start Presentation again without redoing the layout.");
      } else {
        if (preso_empty)
          imgui_begin_disabled_state ();
        if (ImGui::Button ("Start Presentation") && !preso_empty) {
          /* Take the floor first so the platform expects a
           * presentation stream from us, then commit the preso
           * canvas to the wire. _apply_canvas is idempotent — if
           * the preso doc is clean and already on-wire it's a
           * no-op. */
          pexninja_set_presenting (application, true);
          /* Auto-Take if the preso canvas is dirty: a user
           * pressing Start Presentation without Take'ing first
           * almost certainly wants the layout they're looking at
           * to go live. */
          if (compositor::_is_dirty (comp, CO::kCanvasPreso)) {
            compositor::_take (comp, CO::kCanvasPreso);
          }
          compositor::_apply_canvas (application, CO::kCanvasPreso);
        }
        if (preso_empty)
          imgui_end_disabled_state ();
        if (ImGui::IsItemHovered ()) {
          if (preso_empty)
            ImGui::SetTooltip ("Add a source to the Preso canvas first.");
          else
            ImGui::SetTooltip ("Take the floor and start sending the Preso canvas\n"
                               "as a presentation stream towards the Pexip platform.\n"
                               "If the Preso doc is dirty it's auto-Taken first.");
        }
      }
      ImGui::SameLine ();
      ImGui::TextColored (presenting ? green_color : ImVec4 (0.6f, 0.6f, 0.6f, 1.0f),
                          presenting ? "● PRESENTING" : "○ idle");
    }
  }
  if (comp.auto_take) {
    /* Auto-take: keep PGM == PVW every frame, and re-apply only when
     * the doc actually changed (cheap — equality check is one-pass
     * O(N regions) and we skip the disconnect/connect when stable).
     *
     * While the user is mid-drag we hold off applying — otherwise
     * we'd issue a pulse_video_mix_disconnect + connect on every
     * frame of the drag, which flickers the wire and burns CPU.
     * The drag-end frame still triggers an apply because dragging_*
     * is cleared and the doc is now dirty vs. committed. */
    const bool dragging_now = (comp.dragging_region_id != 0);
    if (!dragging_now) {
      const bool m_dirty = compositor::_is_dirty (comp, CO::kCanvasMain);
      const bool p_dirty = compositor::_is_dirty (comp, CO::kCanvasPreso);
      if (m_dirty) {
        compositor::_take (comp, CO::kCanvasMain);
        compositor::_apply_canvas (application, CO::kCanvasMain);
      }
      if (p_dirty) {
        compositor::_take (comp, CO::kCanvasPreso);
        compositor::_apply_canvas (application, CO::kCanvasPreso);
      }
    }
  }

  ImGui::TextDisabled ("Take rebuilds the wire mix from the committed doc; until you press Take");
  ImGui::TextDisabled ("the outgoing video is bit-identical to the legacy slot UI.");
  ImGui::Separator ();

  /* ------------------------------------------------------------ Canvas tabs */
  if (ImGui::BeginTabBar ("##compositor_tabs", ImGuiTabBarFlags_None)) {
    for (int ci = 0; ci < (int)CO::kCanvasCount; ++ci) {
      CO::CanvasIdx c = (CO::CanvasIdx)ci;
      const bool dirty = compositor::_is_dirty (comp, c);
      char tab_label[64];
      snprintf (tab_label, sizeof (tab_label), "%s%s###tab%d", compositor::_canvas_label (c), dirty ? "  *" : "", ci);

      if (ImGui::BeginTabItem (tab_label)) {
        comp.active_canvas = ci;

        /* Two-column body: sources rail + canvas. The rail width is
         * fixed; the canvas claims the rest and is forced to 16:9. */
        const float rail_w = 280.0f;
        if (ImGui::BeginChild ("##sources_rail", ImVec2 (rail_w, 0), true, ImGuiWindowFlags_None)) {
          ImGui::TextUnformatted ("Sources");
          ImGui::TextDisabled ("Configure inline; \"+ on canvas\"");
          ImGui::TextDisabled ("drops one onto the active canvas.");
          ImGui::Separator ();

          using SL = PexNinjaState::PexNinjaSourceLibrary;
          /* Per-canvas library: Main and Preso each have their own
           * sources rail, so what you see depends on which tab is
           * open. The combo-default-kind also tracks the canvas. */
          auto & lib = comp.canvases[ci].library;

          /* "+ New Source" row: kind combo + button. Replaces the old
           * standalone Source Library window's add UI. Default name
           * is unique-per-kind so two Cameras don't both end up
           * "Camera 1". */
          {
            static const SL::Kind kinds[] = {
              SL::kCamera, SL::kMp4, SL::kImage, SL::kRtmp, SL::kRtsp, SL::kDesktop, SL::kWindow, SL::kGfx,
            };
            ImGui::SetNextItemWidth (rail_w - 110.0f);
            if (ImGui::BeginCombo ("##new_kind", source_library::_kind_label ((SL::Kind)lib.add_kind))) {
              for (SL::Kind k : kinds) {
                bool sel = (lib.add_kind == (int)k);
                if (ImGui::Selectable (source_library::_kind_label (k), sel))
                  lib.add_kind = (int)k;
                if (sel)
                  ImGui::SetItemDefaultFocus ();
              }
              ImGui::EndCombo ();
            }
            ImGui::SameLine ();
            if (ImGui::Button ("+ New")) {
              compositor::_create_source (application, ci, (SL::Kind)lib.add_kind);
            }
          }

          ImGui::Separator ();

          if (lib.sources.empty ()) {
            ImGui::TextDisabled ("No sources yet — pick a kind above");
            ImGui::TextDisabled ("and click \"+ New\".");
          } else {
            /* Per-source card: name + per-kind config inline +
             * actions (drop on canvas / delete). Each card is wrapped
             * in a bordered child so consecutive sources have a
             * clear visual boundary — a couple of users complained
             * that adjacent cards "slid into each other" without one.
             * Iteration is by index since Delete may erase the
             * current entry. */
            int delete_lib_idx = -1;
            for (size_t li = 0; li < lib.sources.size (); ++li) {
              auto & ls = lib.sources[li];
              ImGui::PushID ((int)ls.id);

              /* Visual: each card gets a content-sized border rect
               * drawn after the fact, plus a tinted header band
               * across the top, so consecutive sources have a clear
               * boundary instead of running into each other. We
               * snapshot the cursor before content renders, then
               * rewind in z-order via the draw-list to paint the
               * decorations behind whatever ImGui drew. */
              ImDrawList * rail_dl = ImGui::GetWindowDrawList ();
              const ImVec2 card_p0 = ImGui::GetCursorScreenPos ();
              const float card_w = ImGui::GetContentRegionAvail ().x;
              const float pad_x = 6.0f;
              const float pad_y = 4.0f;
              /* Reserve the header-band height + body padding via an
               * indented inner cursor. ImGui's spacing behaviour
               * means we just shift the cursor down/right by the
               * pad; the EndGroup at the bottom recovers the bbox. */
              ImGui::Dummy (ImVec2 (0, pad_y));
              ImGui::Indent (pad_x);
              ImGui::BeginGroup ();

              char header[128];
              const bool warm = (ls.input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE);
              snprintf (header, sizeof (header), "[%s] %s%s###hdr", source_library::_kind_label (ls.kind), ls.name,
                        warm ? "  •" : "");
              if (ImGui::CollapsingHeader (header, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SetNextItemWidth (card_w - 2 * pad_x - 60.0f);
                ImGui::InputText ("name", ls.name, sizeof (ls.name));

                /* Per-kind inline config. Mirrors what the retired
                 * Source Library window used to surface — verbatim
                 * fields, just rehomed. */
                switch (ls.kind) {
                  case SL::kCamera:
                  {
                    std::lock_guard<std::mutex> lock (devices_mutex);
                    if (camera_devices.empty ()) {
                      ImGui::TextDisabled ("No camera devices available.");
                    } else {
                      int idx = ls.camera_idx;
                      if (idx < 0)
                        idx = 0;
                      if (idx >= (int)camera_devices.size ())
                        idx = (int)camera_devices.size () - 1;
                      const char * preview = pulse_device_get_name (camera_devices[idx]);
                      ImGui::SetNextItemWidth (card_w - 2 * pad_x - 60.0f);
                      if (ImGui::BeginCombo ("device", preview)) {
                        for (int n = 0; n < (int)camera_devices.size (); ++n) {
                          bool sel = (ls.camera_idx == n);
                          if (ImGui::Selectable (pulse_device_get_name (camera_devices[n]), sel)) {
                            /* Just stash the user's pick. The actual
                             * release+remat happens inside the next
                             * _apply_canvas (driven by Take), where
                             * we've already disconnected the live
                             * compositor session — releasing an
                             * input that's still held by an active
                             * pulse_video_mix_connect deadlocks, so
                             * we never do it from here.
                             *
                             * compositor::_is_dirty notices the
                             * camera_idx vs materialised_camera_idx
                             * mismatch and lights up the Take button
                             * even though the layout doc is
                             * unchanged. */
                            ls.camera_idx = n;
                          }
                          if (sel)
                            ImGui::SetItemDefaultFocus ();
                        }
                        ImGui::EndCombo ();
                      }
                    }
                    break;
                  }
                  case SL::kMp4:
                  {
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 130.0f);
                    ImGui::InputText ("##mp4path", ls.file_path, sizeof (ls.file_path));
                    ImGui::SameLine ();
                    file_picker::browse_button ("Browse…##mp4", ls.file_path, sizeof (ls.file_path), "Open MP4",
                                                ".mp4");
                    if (ImGui::Checkbox ("loop", &ls.loop)) {
                      /* loop is baked into the PmxMp4Session via
                       * pmx_mp4_session_configure_input at materialise
                       * time, and that call requires STATE_STOP — so
                       * we can't tweak it on a running session.
                       * Mirror the kWindow handle-change pattern:
                       * if the source is already warm, release it and
                       * re-materialise so the next Take goes live with
                       * the new loop setting. */
                      if (ls.input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
                        source_library::_release (application, ls);
                        compositor::_eager_materialise (application, ci, ls.id);
                      }
                    }
                    break;
                  }
                  case SL::kImage:
                  {
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 130.0f);
                    ImGui::InputText ("##imgpath", ls.file_path, sizeof (ls.file_path));
                    ImGui::SameLine ();
                    file_picker::browse_button ("Browse…##img", ls.file_path, sizeof (ls.file_path), "Open Image",
                                                ".png,.jpg,.jpeg");
                    break;
                  }
                  case SL::kRtmp:
                  {
                    /* Path and port are server-wide settings (one
                     * RTMP listener per pexninja instance ⇒ one
                     * path, one port) — we surface both inline on
                     * the source card so the user doesn't have to
                     * jump to Menu → RTMP Server for the common
                     * tweaks. Edits here mutate the same `srv.path`
                     * / `srv.listening_port` that window writes to.
                     * They are locked while the listener is up
                     * because Pulse cannot live-reconfigure path or
                     * port — Disconnect first. */
                    auto & srv = application->state.rtmp_server;
                    const bool locked = srv.is_connected;
                    if (locked)
                      imgui_begin_disabled_state ();
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 60.0f);
                    ImGui::InputText ("path", srv.path, sizeof (srv.path));
                    int port = (int)srv.listening_port;
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 60.0f);
                    if (ImGui::InputInt ("port", &port)) {
                      if (port < 0)
                        port = 0;
                      if (port > 65535)
                        port = 65535;
                      srv.listening_port = (uint16_t)port;
                    }
                    if (locked)
                      imgui_end_disabled_state ();
                    if (locked)
                      ImGui::TextDisabled ("(Disconnect in Menu → RTMP Server to change path/port)");

                    /* Publish URL: read-only InputText that fills
                     * the card width so the URL is never obscured
                     * by a label or button to its left, with a Copy
                     * button below. The host is the Pulse-reported
                     * IPv4 address when available — same address
                     * the bottom-right network chip shows — so
                     * publishers can use it without DNS. */
                    const std::string url = rtmp_server::_publish_url (application);
                    ImGui::TextDisabled ("Publish URL");
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x);
                    /* InputText with ReadOnly is selectable+copyable
                     * with the system shortcut, and we add an
                     * explicit Copy button below for discoverability. */
                    char url_buf[512];
                    snprintf (url_buf, sizeof (url_buf), "%s", url.c_str ());
                    ImGui::InputText ("##publish_url", url_buf, sizeof (url_buf), ImGuiInputTextFlags_ReadOnly);
                    if (ImGui::Button ("Copy URL"))
                      ImGui::SetClipboardText (url.c_str ());

                    /* Live status chip. The publish_start/stop
                     * callbacks bump the listener-wide live counter
                     * under rtmp_server.mu — read it each frame from
                     * that authoritative state. */
                    ImGui::SameLine ();
                    const bool live = rtmp_server::_is_live (application);
                    if (live)
                      ImGui::TextColored (green_color, "● live");
                    else
                      ImGui::TextColored (ImVec4 (0.6f, 0.6f, 0.6f, 1.0f), "○ waiting for publisher");
                    break;
                  }
                  case SL::kRtsp:
                  {
                    /* Per-source RTSP dial: URL + transport + optional
                     * basic auth + jitterbuffer latency. Unlike RTMP
                     * (one shared listener), each RTSP source carries
                     * its own dial config because the underlying Pulse
                     * RTSP API dials out per-session.
                     *
                     * Two state bits drive this card:
                     *   session_connected — the RTSP session is up and
                     *     we've snapshotted its SDP-derived video
                     *     stream list. URL/transport/auth become
                     *     read-only at this point.
                     *   warm_now — a specific stream from the session
                     *     has been bound into the mixer (`input_id`
                     *     is set). Frames are flowing.
                     *
                     * Multi-stream cameras (NVRs, dual-stream IP
                     * cameras, …) sit at session_connected=true but
                     * warm_now=false until the user picks a stream
                     * from the dropdown rendered below. Single-stream
                     * cameras auto-bind on Connect and skip straight
                     * to warm_now. */
                    const bool warm_now = (ls.input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE);
                    const bool session_connected = (ls.rtsp_session_id != PULSE_RTSP_SESSION_ID_NONE);
                    /* Hint at the top of the card so it's immediately
                     * obvious this is an RTSP dial-out (not a USB
                     * camera picker, which is what the Camera kind
                     * gives you). The placeholder URL also doubles as
                     * a "I'm an RTSP source, paste a URL here"
                     * affordance. */
                    if (!session_connected)
                      ImGui::TextDisabled ("RTSP source — paste your camera's URL below.");
                    if (session_connected)
                      imgui_begin_disabled_state ();
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 60.0f);
                    /* Show a placeholder hint while the field is empty
                     * to make this card unmistakably an RTSP setup form
                     * (the hint vanishes the moment the user starts
                     * typing). */
                    if (ls.rtsp_url[0] == '\0')
                      ImGui::InputTextWithHint ("url", "rtsp://camera.local:554/stream1", ls.rtsp_url,
                                                sizeof (ls.rtsp_url));
                    else
                      ImGui::InputText ("url", ls.rtsp_url, sizeof (ls.rtsp_url));
                    if (session_connected)
                      imgui_end_disabled_state ();
                    if (session_connected)
                      ImGui::TextDisabled ("(Disconnect to change URL/transport/auth)");

                    /* Transport combo — TCP is the safe default for
                     * the demo (NAT-friendly, no UDP firewall holes). */
                    if (session_connected)
                      imgui_begin_disabled_state ();
                    static const char * transport_labels[] = {"TCP", "UDP", "UDP_MCAST", "HTTP"};
                    int tr = ls.rtsp_transport;
                    if (tr < 0 || tr >= (int)(sizeof (transport_labels) / sizeof (transport_labels[0])))
                      tr = (int)PULSE_RTSP_TRANSPORT_TCP;
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 60.0f);
                    if (ImGui::BeginCombo ("transport", transport_labels[tr])) {
                      for (int n = 0; n < (int)(sizeof (transport_labels) / sizeof (transport_labels[0])); ++n) {
                        bool sel = (tr == n);
                        if (ImGui::Selectable (transport_labels[n], sel))
                          ls.rtsp_transport = n;
                        if (sel)
                          ImGui::SetItemDefaultFocus ();
                      }
                      ImGui::EndCombo ();
                    }
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 80.0f);
                    ImGui::InputText ("user", ls.rtsp_username, sizeof (ls.rtsp_username));
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 80.0f);
                    ImGui::InputText ("pass", ls.rtsp_password, sizeof (ls.rtsp_password),
                                      ImGuiInputTextFlags_Password);
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 100.0f);
                    ImGui::InputInt ("latency (ms)", &ls.rtsp_latency_ms);
                    if (ls.rtsp_latency_ms < 0)
                      ls.rtsp_latency_ms = 0;
                    if (session_connected)
                      imgui_end_disabled_state ();

                    /* Stream picker. After a successful Connect we
                     * have an SDP-derived snapshot of the camera's
                     * video streams cached on the source. With more
                     * than one stream we surface a dropdown so the
                     * user can pick which one to bind into the mixer
                     * (e.g. main vs sub stream on a dual-stream IP
                     * camera, or channel N on an NVR). On pick we
                     * stash the chosen stream id and re-run
                     * _eager_materialise to acquire it via
                     * pulse_video_mix_input_from_rtsp_stream(). */
                    if (session_connected && !ls.rtsp_video_streams.empty ()) {
                      /* Build the preview label from the currently
                       * selected stream (or a "pick one" hint if
                       * nothing is chosen yet). */
                      auto format_stream_label =
                        [] (const PexNinjaState::PexNinjaSourceLibrary::Source::RtspStreamChoice & c, char * buf,
                            size_t buf_sz) {
                          if (c.codec[0] != '\0')
                            snprintf (buf, buf_sz, "%s [%s]", c.name, c.codec);
                          else
                            snprintf (buf, buf_sz, "%s", c.name);
                        };
                      char preview[64] = "(pick a video stream)";
                      for (const auto & c : ls.rtsp_video_streams) {
                        if (c.id == ls.rtsp_selected_stream_id) {
                          format_stream_label (c, preview, sizeof (preview));
                          break;
                        }
                      }
                      ImGui::SetNextItemWidth (card_w - 2 * pad_x - 60.0f);
                      if (ImGui::BeginCombo ("stream", preview)) {
                        for (const auto & c : ls.rtsp_video_streams) {
                          char item[64];
                          format_stream_label (c, item, sizeof (item));
                          bool sel = (c.id == ls.rtsp_selected_stream_id);
                          if (ImGui::Selectable (item, sel)) {
                            /* Switching streams while a previous one
                             * is bound: drop just the mix input (not
                             * the session) so the next materialise
                             * picks up the new choice without a fresh
                             * DESCRIBE/SETUP/PLAY round trip. */
                            if (ls.input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE && c.id != ls.rtsp_selected_stream_id) {
                              pulse_video_mix_input_release (application->client, ls.input_id);
                              ls.input_id = PULSE_VIDEO_MIX_INPUT_ID_NONE;
                            }
                            ls.rtsp_selected_stream_id = c.id;
                            compositor::_eager_materialise (application, ci, ls.id);
                          }
                          if (sel)
                            ImGui::SetItemDefaultFocus ();
                        }
                        ImGui::EndCombo ();
                      }
                      if (!warm_now)
                        ImGui::TextDisabled ("Pick a video stream to start receiving frames.");
                    }

                    /* Connect / Disconnect button. Connect dials
                     * synchronously (blocks the UI for the duration of
                     * the RTSP DESCRIBE/SETUP/PLAY round trip); for
                     * the demo this is acceptable. Disconnect is
                     * effectively the same as the trash-can release
                     * path but doesn't remove the source. */
                    if (!session_connected) {
                      if (ImGui::Button ("Connect")) {
                        compositor::_eager_materialise (application, ci, ls.id);
                      }
                    } else {
                      if (ImGui::Button ("Disconnect")) {
                        source_library::_release (application, ls);
                      }
                    }
                    ImGui::SameLine ();
                    if (warm_now)
                      ImGui::TextColored (green_color, "● connected");
                    else if (session_connected)
                      ImGui::TextColored (ImVec4 (0.9f, 0.8f, 0.3f, 1.0f), "● connected — pick a stream");
                    else if (ls.rtsp_last_error[0] != '\0')
                      ImGui::TextColored (ImVec4 (0.9f, 0.5f, 0.3f, 1.0f), "✗ %s", ls.rtsp_last_error);
                    else
                      ImGui::TextColored (ImVec4 (0.6f, 0.6f, 0.6f, 1.0f), "○ idle");
                    break;
                  }
                  case SL::kDesktop:
                  {
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 60.0f);
                    ImGui::InputScalar ("handle", ImGuiDataType_U64, &ls.desktop_handle);
                    ImGui::TextDisabled ("0 = primary display.");
                    break;
                  }
                  case SL::kWindow:
                  {
                    /* Window picker: combo whose preview is the
                     * cached human-readable name of the currently-
                     * selected window, and whose items are freshly
                     * enumerated each time the popup opens. Mirrors
                     * the "Window capture" dropdown in the legacy
                     * presentation menu (configure_presentation_windows)
                     * — same enumerate_desktop_windows() helper, just
                     * scoped per-source instead of acting on the
                     * legacy preso slot. */
                    const char * preview = ls.desktop_handle_name[0] != '\0'
                                             ? ls.desktop_handle_name
                                             : (ls.desktop_handle == 0 ? "(pick a window)" : "(unknown handle)");
                    ImGui::SetNextItemWidth (card_w - 2 * pad_x - 60.0f);
                    if (ImGui::BeginCombo ("window", preview)) {
                      auto handles = enumerate_desktop_windows ();
                      if (handles.empty ()) {
                        ImGui::TextDisabled ("No capturable windows found.");
                      } else {
                        for (auto handle : handles) {
                          auto wname = get_window_handle_name (handle);
                          if (wname.empty ())
                            continue;
                          bool wsel = ((uint64_t)handle == ls.desktop_handle);
                          if (ImGui::Selectable (wname.c_str (), wsel)) {
                            /* New pick: stash the handle + cache its
                             * label so the preview survives across
                             * frames without us re-querying the
                             * platform every redraw. If the source
                             * was already materialised against an
                             * older handle, release it so the next
                             * eager-materialise picks up the new
                             * choice. */
                            ls.desktop_handle = (uint64_t)handle;
                            snprintf (ls.desktop_handle_name, sizeof (ls.desktop_handle_name), "%s", wname.c_str ());
                            if (ls.input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
                              source_library::_release (application, ls);
                              compositor::_eager_materialise (application, ci, ls.id);
                            }
                          }
                          if (wsel)
                            ImGui::SetItemDefaultFocus ();
                        }
                      }
                      ImGui::EndCombo ();
                    }
                    break;
                  }
                  case SL::kGfx:
                  {
                    ImGui::SetNextItemWidth (80.0f);
                    ImGui::InputInt ("w", &ls.gfx_width);
                    ImGui::SameLine ();
                    ImGui::SetNextItemWidth (80.0f);
                    ImGui::InputInt ("h", &ls.gfx_height);
                    if (ls.gfx_width < 1)
                      ls.gfx_width = 1;
                    if (ls.gfx_height < 1)
                      ls.gfx_height = 1;

                    /* Background fill: tickbox flips an annotation
                     * surface from "transparent overlay" into
                     * "whiteboard". Default colour is opaque white;
                     * the picker gives blackboard / $colour-board
                     * variants. Pushed live to Pulse on every change
                     * so the user sees it immediately. */
                    bool bg_changed = ImGui::Checkbox ("Background", &ls.gfx_bg_enabled);
                    if (ls.gfx_bg_enabled) {
                      ImGui::SameLine ();
                      ImGui::SetNextItemWidth (200.0f);
                      bg_changed |= ImGui::ColorEdit4 ("##bgcolor", ls.gfx_bg_color,
                                                       ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                    }
                    if (bg_changed && ls.input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
                      const uint8_t r =
                        (uint8_t)std::round (std::min (std::max (ls.gfx_bg_color[0], 0.0f), 1.0f) * 255.0f);
                      const uint8_t g =
                        (uint8_t)std::round (std::min (std::max (ls.gfx_bg_color[1], 0.0f), 1.0f) * 255.0f);
                      const uint8_t b =
                        (uint8_t)std::round (std::min (std::max (ls.gfx_bg_color[2], 0.0f), 1.0f) * 255.0f);
                      const uint8_t a =
                        (uint8_t)std::round (std::min (std::max (ls.gfx_bg_color[3], 0.0f), 1.0f) * 255.0f);
                      pulse_annotation_set_background (application->client, ls.input_id, ls.gfx_bg_enabled, r, g, b, a);
                    }
                    break;
                  }
                }

                /* Per-source video processing toggles. Wire-side
                 * model is per-region; we keep a per-source intent
                 * here (so the user toggles "Segmentation" once on
                 * the Camera, not every time they add a region) and
                 * fan out to all regions referencing this source on
                 * change.
                 *
                 * Segmentation makes the background of a person
                 * transparent — the win is overlapping a head-and-
                 * shoulders shot on top of a slide without a hard
                 * rectangle around them. Blur is the same idea but
                 * background-blurred instead of transparent. */
                ImGui::Spacing ();
                bool seg_on = (ls.videoproc_mask & PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION) != 0;
                if (ImGui::Checkbox ("Segmentation", &seg_on)) {
                  if (seg_on)
                    ls.videoproc_mask =
                      (PulseVideoProcessTypeMask)(ls.videoproc_mask | PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION);
                  else
                    ls.videoproc_mask =
                      (PulseVideoProcessTypeMask)(ls.videoproc_mask & ~PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION);
                  compositor::_sync_source_mask_to_regions (application, ci, ls.id);
                }
                if (ImGui::IsItemHovered ()) {
                  ImGui::SetTooltip ("Make the background of a person transparent so this source can\n"
                                     "overlap other regions without a hard rectangle around them.\n"
                                     "Applies to every region that uses this source.");
                }
                ImGui::SameLine ();
                bool blur_on = (ls.videoproc_mask & PULSE_VIDEO_PROCESS_TYPE_BLUR) != 0;
                if (ImGui::Checkbox ("Blur bg", &blur_on)) {
                  if (blur_on)
                    ls.videoproc_mask = (PulseVideoProcessTypeMask)(ls.videoproc_mask | PULSE_VIDEO_PROCESS_TYPE_BLUR);
                  else
                    ls.videoproc_mask = (PulseVideoProcessTypeMask)(ls.videoproc_mask & ~PULSE_VIDEO_PROCESS_TYPE_BLUR);
                  compositor::_sync_source_mask_to_regions (application, ci, ls.id);
                }
                if (ImGui::IsItemHovered ()) {
                  ImGui::SetTooltip ("Blur the background of a person instead of replacing it.");
                }

                /* Action row. "+ on canvas" warms the source if cold
                 * (eager materialisation) and adds a region pointing
                 * at it to the active canvas's editing_doc. */
                ImGui::Spacing ();
                if (ImGui::SmallButton ("+ on canvas")) {
                  compositor::_add_region_to_active_canvas (application, ls.id);
                }
                ImGui::SameLine ();
                if (ImGui::SmallButton ("Delete")) {
                  delete_lib_idx = (int)li;
                }
                ImGui::SameLine ();
                ImGui::TextDisabled (warm ? "warm" : "cold");
              }

              ImGui::EndGroup ();
              ImGui::Unindent (pad_x);
              ImGui::Dummy (ImVec2 (0, pad_y));

              /* Now paint the card border on top of everything. We
               * draw only the outline (no fill) so the content stays
               * visible — the goal here is just a clear edge between
               * adjacent cards, not a tinted background. */
              const ImVec2 card_p1 = ImVec2 (card_p0.x + card_w, ImGui::GetCursorScreenPos ().y);
              const ImU32 border_col = ImGui::GetColorU32 (ImGuiCol_Border);
              rail_dl->AddRect (card_p0, card_p1, border_col, 4.0f, 0, 1.0f);

              /* Visible spacer between cards so the borders don't
               * touch — gives the rail a "stack of cards" feel. */
              ImGui::Spacing ();

              ImGui::PopID ();
            }

            if (delete_lib_idx >= 0) {
              auto & victim = lib.sources[(size_t)delete_lib_idx];
              uint32_t victim_id = victim.id;
              source_library::_release (application, victim);
              lib.sources.erase (lib.sources.begin () + delete_lib_idx);
              /* Drop any regions in THIS canvas that referenced the
               * deleted source — otherwise they'd render as
               * "(unbound)" placeholders forever. The other canvas
               * has its own independent library so it's untouched. */
              auto & doc = comp.canvases[ci].editing;
              doc.regions.erase (std::remove_if (doc.regions.begin (), doc.regions.end (),
                                                 [&] (const CO::Region & r) { return r.source_lib_id == victim_id; }),
                                 doc.regions.end ());
            }
          }
        }
        ImGui::EndChild ();

        ImGui::SameLine ();
        if (ImGui::BeginChild ("##canvas_pane", ImVec2 (0, 0), false, ImGuiWindowFlags_None)) {
          /* 16:9-pinned canvas: take all available width, derive
           * height from it; if the resulting height exceeds the
           * available height, fall back to height-driven sizing. */
          ImVec2 avail = ImGui::GetContentRegionAvail ();
          /* Reserve some space at the bottom for the region list. */
          const float region_panel_h = 180.0f;
          float canvas_max_h = avail.y - region_panel_h - 8.0f;
          if (canvas_max_h < 120.0f)
            canvas_max_h = 120.0f;

          float canvas_w = avail.x;
          float canvas_h = canvas_w * 9.0f / 16.0f;
          if (canvas_h > canvas_max_h) {
            canvas_h = canvas_max_h;
            canvas_w = canvas_h * 16.0f / 9.0f;
          }
          ImVec2 canvas_p0 = ImGui::GetCursorScreenPos ();
          ImVec2 canvas_size (canvas_w, canvas_h);

          ImDrawList * dl = ImGui::GetWindowDrawList ();

          /* Canvas-wide invisible button — this is the canonical
           * ImGui pattern (see imgui_demo.cpp "Canvas") for click+
           * drag on a custom-drawn surface:
           *
           *   1. InvisibleButton claims the bbox so ImGui's hit-test
           *      treats it as a normal item.
           *   2. IsItemHovered() — mouse is over the canvas right now.
           *   3. IsItemActive()  — mouse went DOWN on the canvas and
           *      hasn't been released. This stays true even if the
           *      cursor strays outside while the user is still
           *      holding, which is exactly what a drag wants.
           *
           * We use IsItemActive (not IsMouseDown) for the held state
           * so a press that started somewhere else doesn't get
           * mistaken for a drag on the canvas. */
          ImGui::SetCursorScreenPos (canvas_p0);
          ImGui::InvisibleButton ("##canvas_hit", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
          const bool canvas_hovered = ImGui::IsItemHovered ();
          const bool canvas_active = ImGui::IsItemActive ();
          const bool mouse_clicked = canvas_hovered && ImGui::IsMouseClicked (ImGuiMouseButton_Left);
          const ImVec2 mouse = ImGui::GetMousePos ();
          /* Mouse position in canvas-local pixels. */
          const ImVec2 mouse_local (mouse.x - canvas_p0.x, mouse.y - canvas_p0.y);

          /* Canvas backdrop — dark grey solid; clearer than
           * transparent for spotting region bounds. The border is
           * tinted brighter when the canvas is hovered so the user
           * can see at a glance that the surface is interactive
           * (otherwise drag is invisible until they actually grab a
           * region). */
          dl->AddRectFilled (canvas_p0, ImVec2 (canvas_p0.x + canvas_w, canvas_p0.y + canvas_h),
                             IM_COL32 (28, 28, 32, 255), 6.0f);
          const ImU32 frame_col = canvas_hovered ? IM_COL32 (160, 200, 255, 255) : IM_COL32 (90, 90, 100, 255);
          dl->AddRect (canvas_p0, ImVec2 (canvas_p0.x + canvas_w, canvas_p0.y + canvas_h), frame_col, 6.0f, 0,
                       canvas_hovered ? 1.5f : 1.0f);

          /* Render regions back-to-front by layer so on-top regions
           * actually draw on top. We sort a copy of indices to avoid
           * mutating the doc during draw. */
          auto & doc = comp.canvases[ci].editing;
          std::vector<size_t> order (doc.regions.size ());
          for (size_t i = 0; i < doc.regions.size (); ++i)
            order[i] = i;
          std::sort (order.begin (), order.end (),
                     [&] (size_t a, size_t b) { return doc.regions[a].layer < doc.regions[b].layer; });

          for (size_t idx : order) {
            const auto & r = doc.regions[idx];
            auto * src = compositor::_lookup_source (application, ci, r.source_lib_id);
            compositor::_draw_region (dl, canvas_p0, canvas_size, r, src, r.id == comp.selected_region_id);
          }

          /* ----- Hit-testing & drag/resize ---------------------------
           *
           * Two interaction states, mutually exclusive:
           *
           *  (A) idle / hovering: no mouse button held. We compute a
           *      hover decoration (resize cursor when over a handle)
           *      and, on click, latch dragging_region_id + drag_mode
           *      and snapshot the region's geometry.
           *
           *  (B) dragging: mouse held since latch. We translate the
           *      mouse delta from grab time into a doc-space update
           *      of the region's centrepoint / size, clamped to
           *      sensible bounds. Release ends the drag.
           *
           * Hit-test order is front-to-back (highest layer first) so
           * a small region on top of a big one wins the click. */
          const float kHandle = 8.0f; /* px — handle hot-zone radius from corner/edge */

          auto region_rect = [&] (const CO::Region & r, ImVec2 & out_p0, ImVec2 & out_p1) {
            /* Use the SAME anchor-with-edge-clamp math as
             * compositor::_region_pixel_rect, but produce a
             * canvas-local rect (mouse_local is canvas-local too)
             * so the hit-test stays consistent with the rendered
             * outline. */
            ImVec2 screen_p0, screen_p1;
            compositor::_region_pixel_rect (canvas_p0, canvas_size, r, screen_p0, screen_p1);
            out_p0 = ImVec2 (screen_p0.x - canvas_p0.x, screen_p0.y - canvas_p0.y);
            out_p1 = ImVec2 (screen_p1.x - canvas_p0.x, screen_p1.y - canvas_p0.y);
          };

          /* Classify a canvas-local point against a region rect. */
          auto classify_hit = [&] (const ImVec2 & pt, const ImVec2 & p0, const ImVec2 & p1) -> CO::DragMode {
            const bool inside_x = pt.x >= p0.x - kHandle && pt.x <= p1.x + kHandle;
            const bool inside_y = pt.y >= p0.y - kHandle && pt.y <= p1.y + kHandle;
            if (!inside_x || !inside_y)
              return CO::kDragNone;
            const bool near_l = std::fabs (pt.x - p0.x) <= kHandle;
            const bool near_r = std::fabs (pt.x - p1.x) <= kHandle;
            const bool near_t = std::fabs (pt.y - p0.y) <= kHandle;
            const bool near_b = std::fabs (pt.y - p1.y) <= kHandle;
            if (near_t && near_l)
              return CO::kDragNW;
            if (near_t && near_r)
              return CO::kDragNE;
            if (near_b && near_l)
              return CO::kDragSW;
            if (near_b && near_r)
              return CO::kDragSE;
            if (near_t)
              return CO::kDragN;
            if (near_b)
              return CO::kDragS;
            if (near_l)
              return CO::kDragW;
            if (near_r)
              return CO::kDragE;
            /* Strictly inside the rect (no handle hit). */
            if (pt.x >= p0.x && pt.x <= p1.x && pt.y >= p0.y && pt.y <= p1.y)
              return CO::kDragMove;
            return CO::kDragNone;
          };

          auto cursor_for_mode = [] (CO::DragMode m) {
            switch (m) {
              case CO::kDragMove:
                return ImGuiMouseCursor_ResizeAll;
              case CO::kDragN:
              case CO::kDragS:
                return ImGuiMouseCursor_ResizeNS;
              case CO::kDragE:
              case CO::kDragW:
                return ImGuiMouseCursor_ResizeEW;
              case CO::kDragNW:
              case CO::kDragSE:
                return ImGuiMouseCursor_ResizeNWSE;
              case CO::kDragNE:
              case CO::kDragSW:
                return ImGuiMouseCursor_ResizeNESW;
              default:
                return ImGuiMouseCursor_Arrow;
            }
          };

          /* Resolve who's being dragged into a pointer. */
          CO::Region * dragging = nullptr;
          if (comp.dragging_region_id != 0) {
            for (auto & r : doc.regions) {
              if (r.id == comp.dragging_region_id) {
                dragging = &r;
                break;
              }
            }
            /* If the region we were dragging was deleted out from
             * under us (e.g. via the Regions list), abandon the drag
             * cleanly. */
            if (!dragging) {
              comp.dragging_region_id = 0;
              comp.drag_mode = (int)CO::kDragNone;
            }
          }

          if (dragging && canvas_active) {
            /* (B) Active drag — express everything in canvas pixel
             * space, then convert back to normalised at the end.
             *
             * Pixel space is the natural language for "this edge is
             * pinned, that edge follows the mouse" — there's no
             * subtle interaction between size and position like
             * there is in normalised anchor space (where free_w
             * shrinks as w grows). */
            const float canvas_w_px = canvas_size.x;
            const float canvas_h_px = canvas_size.y;
            const float kMinPx = std::max (8.0f, 0.05f * std::min (canvas_w_px, canvas_h_px));

            float L = comp.drag_start_left_px;
            float T = comp.drag_start_top_px;
            float R = comp.drag_start_right_px;
            float B = comp.drag_start_bot_px;

            switch ((CO::DragMode)comp.drag_mode) {
              case CO::kDragMove:
              {
                /* Move: keep the cursor at the same offset within
                 * the rect that it was at grab time — no jumping. */
                float new_left = mouse_local.x - comp.drag_grab_offset_x;
                float new_top = mouse_local.y - comp.drag_grab_offset_y;
                const float w = R - L;
                const float h = B - T;
                if (new_left < 0.0f)
                  new_left = 0.0f;
                if (new_top < 0.0f)
                  new_top = 0.0f;
                if (new_left + w > canvas_w_px)
                  new_left = canvas_w_px - w;
                if (new_top + h > canvas_h_px)
                  new_top = canvas_h_px - h;
                L = new_left;
                T = new_top;
                R = L + w;
                B = T + h;
                break;
              }
              /* Edge resizes: the OPPOSITE edge is pinned (its
               * pixel position stays at the snapshot value); the
               * dragged edge follows the mouse, clamped to the
               * canvas and to the min-size floor on the other
               * side of the rect. */
              case CO::kDragE:
                R = mouse_local.x;
                if (R < L + kMinPx)
                  R = L + kMinPx;
                if (R > canvas_w_px)
                  R = canvas_w_px;
                break;
              case CO::kDragW:
                L = mouse_local.x;
                if (L > R - kMinPx)
                  L = R - kMinPx;
                if (L < 0.0f)
                  L = 0.0f;
                break;
              case CO::kDragS:
                B = mouse_local.y;
                if (B < T + kMinPx)
                  B = T + kMinPx;
                if (B > canvas_h_px)
                  B = canvas_h_px;
                break;
              case CO::kDragN:
                T = mouse_local.y;
                if (T > B - kMinPx)
                  T = B - kMinPx;
                if (T < 0.0f)
                  T = 0.0f;
                break;
              case CO::kDragSE:
                R = mouse_local.x;
                B = mouse_local.y;
                if (R < L + kMinPx)
                  R = L + kMinPx;
                if (B < T + kMinPx)
                  B = T + kMinPx;
                if (R > canvas_w_px)
                  R = canvas_w_px;
                if (B > canvas_h_px)
                  B = canvas_h_px;
                break;
              case CO::kDragSW:
                L = mouse_local.x;
                B = mouse_local.y;
                if (L > R - kMinPx)
                  L = R - kMinPx;
                if (B < T + kMinPx)
                  B = T + kMinPx;
                if (L < 0.0f)
                  L = 0.0f;
                if (B > canvas_h_px)
                  B = canvas_h_px;
                break;
              case CO::kDragNE:
                R = mouse_local.x;
                T = mouse_local.y;
                if (R < L + kMinPx)
                  R = L + kMinPx;
                if (T > B - kMinPx)
                  T = B - kMinPx;
                if (R > canvas_w_px)
                  R = canvas_w_px;
                if (T < 0.0f)
                  T = 0.0f;
                break;
              case CO::kDragNW:
                L = mouse_local.x;
                T = mouse_local.y;
                if (L > R - kMinPx)
                  L = R - kMinPx;
                if (T > B - kMinPx)
                  T = B - kMinPx;
                if (L < 0.0f)
                  L = 0.0f;
                if (T < 0.0f)
                  T = 0.0f;
                break;
              default:
                break;
            }

            /* Convert pixel rect back to normalised (cx, cy, w, h)
             * using the anchor-with-edge-clamp inverse:
             *   w  = (R - L) / canvas_w
             *   cx = L / (canvas_w - (R - L))     (free_w in px)
             * If free_w_px == 0 the rect fills the canvas and cx is
             * indeterminate; keep it at 0.0 in that case. */
            const float w_px = R - L;
            const float h_px = B - T;
            const float free_w_px = canvas_w_px - w_px;
            const float free_h_px = canvas_h_px - h_px;
            double new_w = (double)w_px / (double)canvas_w_px;
            double new_h = (double)h_px / (double)canvas_h_px;
            double new_cx = free_w_px > 0.001f ? (double)L / (double)free_w_px : 0.0;
            double new_cy = free_h_px > 0.001f ? (double)T / (double)free_h_px : 0.0;
            if (new_cx < 0.0)
              new_cx = 0.0;
            if (new_cx > 1.0)
              new_cx = 1.0;
            if (new_cy < 0.0)
              new_cy = 0.0;
            if (new_cy > 1.0)
              new_cy = 1.0;

            dragging->width_ratio = new_w;
            dragging->height_ratio = new_h;
            dragging->x_centrepoint = new_cx;
            dragging->y_centrepoint = new_cy;

            ImGui::SetMouseCursor (cursor_for_mode ((CO::DragMode)comp.drag_mode));
          } else {
            /* End any active drag now that the button has come up.
             * If we got here it's because canvas_active is false,
             * which means the mouse was released (or the click
             * never started on us). Either way, drop the drag. */
            if (dragging) {
              comp.dragging_region_id = 0;
              comp.drag_mode = (int)CO::kDragNone;
              dragging = nullptr;
            }

            /* (A) Idle / hovering — front-to-back hit-test for hover
             * decoration and click-to-select-and-grab. */
            if (canvas_hovered) {
              CO::DragMode hover_mode = CO::kDragNone;
              uint32_t hover_id = 0;
              for (auto it = order.rbegin (); it != order.rend (); ++it) {
                const auto & r = doc.regions[*it];
                ImVec2 p0, p1;
                region_rect (r, p0, p1);
                CO::DragMode m = classify_hit (mouse_local, p0, p1);
                if (m != CO::kDragNone) {
                  hover_mode = m;
                  hover_id = r.id;
                  break;
                }
              }
              if (hover_mode != CO::kDragNone)
                ImGui::SetMouseCursor (cursor_for_mode (hover_mode));

              if (mouse_clicked) {
                if (hover_id != 0) {
                  comp.selected_region_id = hover_id;
                  comp.dragging_region_id = hover_id;
                  comp.drag_mode = (int)hover_mode;
                  for (const auto & r : doc.regions) {
                    if (r.id == hover_id) {
                      /* Snapshot the rect in canvas-local pixels. */
                      ImVec2 sp0, sp1;
                      compositor::_region_pixel_rect (canvas_p0, canvas_size, r, sp0, sp1);
                      comp.drag_start_left_px = sp0.x - canvas_p0.x;
                      comp.drag_start_top_px = sp0.y - canvas_p0.y;
                      comp.drag_start_right_px = sp1.x - canvas_p0.x;
                      comp.drag_start_bot_px = sp1.y - canvas_p0.y;
                      comp.drag_grab_offset_x = mouse_local.x - comp.drag_start_left_px;
                      comp.drag_grab_offset_y = mouse_local.y - comp.drag_start_top_px;
                      break;
                    }
                  }
                  comp.drag_start_mouse = mouse_local;
                } else {
                  /* Click on empty canvas — deselect. */
                  comp.selected_region_id = 0;
                }
              }
            }
          }

          /* "On Air" / dirty banner under the canvas. */
          if (dirty) {
            ImGui::TextColored (red_color, "● PVW differs from PGM — press Take %s to go live.",
                                compositor::_canvas_label (c));
          } else {
            ImGui::TextColored (green_color, "● PVW == PGM — %s on air.", compositor::_canvas_label (c));
          }
          /* Live mouse-state readout. Helps users discover that the
           * canvas IS interactive — when they hover over a region,
           * this line updates with the region's name and the
           * affordance their cursor is currently picking up. */
          if (comp.dragging_region_id != 0) {
            ImGui::TextDisabled ("Dragging — release the mouse to commit the move/resize.");
          } else if (canvas_hovered) {
            uint32_t hov_id = 0;
            for (auto it = order.rbegin (); it != order.rend (); ++it) {
              const auto & r = doc.regions[*it];
              ImVec2 sp0, sp1;
              compositor::_region_pixel_rect (canvas_p0, canvas_size, r, sp0, sp1);
              /* Convert to canvas-local for comparison with mouse_local. */
              ImVec2 p0 (sp0.x - canvas_p0.x, sp0.y - canvas_p0.y);
              ImVec2 p1 (sp1.x - canvas_p0.x, sp1.y - canvas_p0.y);
              const float kHandleStatus = 8.0f;
              if (mouse_local.x >= p0.x - kHandleStatus && mouse_local.x <= p1.x + kHandleStatus &&
                  mouse_local.y >= p0.y - kHandleStatus && mouse_local.y <= p1.y + kHandleStatus) {
                hov_id = r.id;
                break;
              }
            }
            if (hov_id != 0) {
              const CO::Region * hr = nullptr;
              for (const auto & r : doc.regions) {
                if (r.id == hov_id) {
                  hr = &r;
                  break;
                }
              }
              auto * hsrc = hr ? compositor::_lookup_source (application, ci, hr->source_lib_id) : nullptr;
              ImGui::TextDisabled ("Hovering: %s — click to select, drag to move, edges/corners to resize.",
                                   hsrc ? hsrc->name : "(unbound)");
            } else {
              ImGui::TextDisabled ("Click on a region to select it; drag its body to move, edges/corners to resize.");
            }
          } else {
            ImGui::TextDisabled ("Tip: drag a region's body to move it; drag its edges or corners to resize.");
          }

          /* Helpful overlay when the canvas is empty — without this
           * a fresh Preso tab is just a dark rectangle and it's not
           * obvious that you need to add sources from the rail. */
          if (doc.regions.empty ()) {
            const char * msg = "(empty canvas — add a source from the rail)";
            ImVec2 ts = ImGui::CalcTextSize (msg);
            dl->AddText (ImVec2 (canvas_p0.x + (canvas_w - ts.x) * 0.5f, canvas_p0.y + (canvas_h - ts.y) * 0.5f),
                         IM_COL32 (160, 160, 170, 220), msg);
          }

          ImGui::Separator ();

          /* Region list / inspector. Per-region row with layer
           * Up/Dn and Delete. Rows are ORDERED BY LAYER DESCENDING
           * (top of stack first) so the up/down arrows match what
           * the user sees on the canvas — the topmost region is
           * the first row, and "Up" on it is a no-op (already on
           * top).
           *
           * The proper inspector (videoproc mask, exact geometry
           * sliders, source rebind) lands in 2b.2. */
          ImGui::TextUnformatted ("Regions");
          if (doc.regions.empty ()) {
            ImGui::TextDisabled ("Empty canvas — pick a source from the rail and click \"+ on canvas\".");
          } else {
            uint32_t delete_id = 0;
            uint32_t swap_a_id = 0, swap_b_id = 0;
            /* Build a layer-descending display order. Sort by
             * (layer DESC, region id ASC) for a stable secondary
             * key when two regions share a layer (they're laid out
             * side-by-side on the wire, so insertion order wins). */
            std::vector<size_t> row_order (doc.regions.size ());
            for (size_t i = 0; i < doc.regions.size (); ++i)
              row_order[i] = i;
            std::sort (row_order.begin (), row_order.end (), [&] (size_t a, size_t b) {
              if (doc.regions[a].layer != doc.regions[b].layer)
                return doc.regions[a].layer > doc.regions[b].layer;
              return doc.regions[a].id < doc.regions[b].id;
            });

            for (size_t pos = 0; pos < row_order.size (); ++pos) {
              size_t i = row_order[pos];
              auto & r = doc.regions[i];
              auto * src = compositor::_lookup_source (application, ci, r.source_lib_id);
              ImGui::PushID ((int)r.id);

              char row[200];
              if (src)
                snprintf (row, sizeof (row), "L%d  [%s] %s", r.layer, source_library::_kind_label (src->kind),
                          src->name);
              else
                snprintf (row, sizeof (row), "L%d  (unbound)", r.layer);

              bool sel = (comp.selected_region_id == r.id);
              if (ImGui::Selectable (row, sel, ImGuiSelectableFlags_AllowItemOverlap, ImVec2 (0, 0)))
                comp.selected_region_id = r.id;

              /* Up/Dn use plain ASCII because the bundled ImGui
               * default font has no glyphs above U+00FF and the
               * pretty arrows (U+2191 ↑ / U+2193 ↓) renderered as
               * the missing-glyph "?".
               *
               * The arrows refer to the *visual* z-order, NOT the
               * vector index. We swap layer values with the
               * neighbour ABOVE / BELOW in display-order (which is
               * sorted by layer descending), so "Up" on the
               * second-from-top row swaps with the topmost. The
               * old code blindly swapped with the vector neighbour
               * instead, which made the on-air mix appear to flip
               * the IDENTITY of two named rows. */
              ImGui::SameLine ();
              const bool can_up = (pos > 0);
              if (ImGui::SmallButton ("Up") && can_up) {
                swap_a_id = r.id;
                swap_b_id = doc.regions[row_order[pos - 1]].id;
              }
              if (ImGui::IsItemHovered ())
                ImGui::SetTooltip ("Move up one layer (towards the top of the stack).");

              ImGui::SameLine ();
              const bool can_dn = (pos + 1 < row_order.size ());
              if (ImGui::SmallButton ("Dn") && can_dn) {
                swap_a_id = r.id;
                swap_b_id = doc.regions[row_order[pos + 1]].id;
              }
              if (ImGui::IsItemHovered ())
                ImGui::SetTooltip ("Move down one layer (towards the bottom of the stack).");

              ImGui::SameLine ();
              if (ImGui::SmallButton ("Delete"))
                delete_id = r.id;

              ImGui::PopID ();
            }

            if (swap_a_id != 0 && swap_b_id != 0) {
              CO::Region * ra = nullptr;
              CO::Region * rb = nullptr;
              for (auto & r : doc.regions) {
                if (r.id == swap_a_id)
                  ra = &r;
                else if (r.id == swap_b_id)
                  rb = &r;
              }
              if (ra && rb)
                std::swap (ra->layer, rb->layer);
            }
            if (delete_id != 0) {
              if (delete_id == comp.selected_region_id)
                comp.selected_region_id = 0;
              doc.regions.erase (std::remove_if (doc.regions.begin (), doc.regions.end (),
                                                 [&] (const CO::Region & r) { return r.id == delete_id; }),
                                 doc.regions.end ());
            }
          }
        }
        ImGui::EndChild ();

        ImGui::EndTabItem ();
      }
    }
    ImGui::EndTabBar ();
  }

  ImGui::End ();
}

static inline void
configure_window_show_pulse_info (PexNinja * application)
{
  PulseInfo info;

  pulse_get_info (application->client, &info);
  // ImGui::SetNextWindowSize (ImVec2 (300, 450), ImGuiCond_FirstUseEver);
  ImGui::Begin ("Pulse info", &application->state.windows.show_pulse_info, ImGuiWindowFlags_NoResize);
  ImGui::Text ("Client type: %s", info.client_type);
  ImGui::Text ("Session state: %s", info.session_state);
  ImGui::Text ("");
  ImGui::Text ("FIPS:");
  ImGui::Text (" Enabled: %s", info.fips.enabled ? "true" : "false");
  if (info.fips.enabled) {
    ImGui::Text (" Verified: %s", info.fips.verified ? "true" : "false");
    ImGui::Text (" Path: %s", info.fips.fips_so_path);
  }
  ImGui::Text ("");
  ImGui::Text ("CURL:");
  ImGui::Text (" Version: %s", info.curl.version);
  ImGui::Text (" SSL version: %s", info.curl.ssl_version);
  ImGui::End ();
}

static void
_show_ptz_controls (PexNinja * application, bool is_fecc)
{
  int movement = -1;
  static int pt_responsiveness = 1000;
  static int z_responsiveness = 1000;

  ImGui::Text ("Pan/tilt:");
  if (ImGui::BeginTable ("_PAN_TILT", 3, ImGuiTableFlags_None, ImVec2 (100, 0))) {
    ImGui::TableNextRow ();
    ImGui::TableSetColumnIndex (0);
    if (ImGui::Button ("*  7", ImVec2 (18, 19))) {
      movement = (PULSE_FECC_MOVEMENT_TILT_UP | PULSE_FECC_MOVEMENT_PAN_LEFT);
    }
    ImGui::TableSetColumnIndex (1);
    if (ImGui::ArrowButton ("up", ImGuiDir_Up)) {
      movement = PULSE_FECC_MOVEMENT_TILT_UP;
    }
    ImGui::TableSetColumnIndex (2);
    if (ImGui::Button ("*  9", ImVec2 (18, 19))) {
      movement = (PULSE_FECC_MOVEMENT_TILT_UP | PULSE_FECC_MOVEMENT_PAN_RIGHT);
    }
    ImGui::TableNextRow ();
    ImGui::TableSetColumnIndex (0);
    if (ImGui::ArrowButton ("left", ImGuiDir_Left)) {
      movement = PULSE_FECC_MOVEMENT_PAN_LEFT;
    }

    ImGui::TableSetColumnIndex (2);
    if (ImGui::ArrowButton ("right", ImGuiDir_Right)) {
      movement = PULSE_FECC_MOVEMENT_PAN_RIGHT;
    }

    ImGui::TableNextRow ();
    ImGui::TableSetColumnIndex (0);
    if (ImGui::Button ("*  1", ImVec2 (18, 19))) {
      movement = (PULSE_FECC_MOVEMENT_TILT_DOWN | PULSE_FECC_MOVEMENT_PAN_LEFT);
    }
    ImGui::TableSetColumnIndex (1);
    if (ImGui::ArrowButton ("down", ImGuiDir_Down)) {
      movement = PULSE_FECC_MOVEMENT_TILT_DOWN;
    }
    ImGui::TableSetColumnIndex (2);
    if (ImGui::Button ("*  3", ImVec2 (18, 19))) {
      movement = (PULSE_FECC_MOVEMENT_TILT_DOWN | PULSE_FECC_MOVEMENT_PAN_RIGHT);
    }
    ImGui::EndTable ();
  }

  ImGui::PushItemWidth (-FLT_MIN);
  ImGui::SliderInt ("##pt_responsiveness", &pt_responsiveness, 10, 1000, "sensitivity", ImGuiSliderFlags_NoInput);
  ImGui::PopItemWidth ();

  if (is_fecc == false) {
    float buttonWidth = ImGui::GetContentRegionAvail ().x;
    if (ImGui::Button ("Reset PT", ImVec2 (buttonWidth, 0.0f))) {
      pulse_device_session_reset_ptz (application->client, PULSE_MEDIA_CONTENT_MAIN,
                                      (PulsePTZAxis)(PULSE_PTZ_AXIS_PAN | PULSE_PTZ_AXIS_TILT));
    }
  }

  ImGui::Separator ();
  ImGui::Text ("Zoom:");
  if (ImGui::BeginTable ("_ZOOM", 2, ImGuiTableFlags_SizingStretchSame)) {
    float buttonWidth = ImGui::GetContentRegionAvail ().x;
    ImGui::TableNextRow ();
    ImGui::TableSetColumnIndex (0);
    if (ImGui::Button ("in", ImVec2 (buttonWidth / 2, 0.0f))) {
      movement = PULSE_FECC_MOVEMENT_ZOOM_IN;
    }
    ImGui::TableSetColumnIndex (1);
    if (ImGui::Button ("out", ImVec2 (buttonWidth / 2, 0.0f))) {
      movement = PULSE_FECC_MOVEMENT_ZOOM_OUT;
    }
    ImGui::EndTable ();
  }

  ImGui::PushItemWidth (-FLT_MIN);
  ImGui::SliderInt (" ", &z_responsiveness, 10, 1000, "sensitivity", ImGuiSliderFlags_NoInput);
  ImGui::PopItemWidth ();

  if (is_fecc == false) {
    float buttonWidth = ImGui::GetContentRegionAvail ().x;
    if (ImGui::Button ("Reset Z", ImVec2 (buttonWidth, 0.0f))) {
      pulse_device_session_reset_ptz (application->client, PULSE_MEDIA_CONTENT_MAIN, PULSE_PTZ_AXIS_ZOOM);
    }
  }

  if (movement != (PulseFeccMovementDirection)-1) {
    if (is_fecc) {
      pulse_participant_control_fecc (application->client, application->state.fecc.participant_uuid.c_str (),
                                      PULSE_FECC_ACTION_START, (PulseFeccMovementDirection)movement, 1000);
    } else {
      int timeout = (movement == PULSE_FECC_MOVEMENT_ZOOM_IN || movement == PULSE_FECC_MOVEMENT_ZOOM_OUT)
                      ? z_responsiveness
                      : pt_responsiveness;
      pulse_device_session_control_ptz (application->client, PULSE_MEDIA_CONTENT_MAIN, PULSE_FECC_ACTION_START,
                                        (PulseFeccMovementDirection)movement, timeout);
    }
  }
}

static inline void
configure_window_show_camera_controls (PexNinja * application)
{
  ImGui::SetNextWindowSize (ImVec2 (100, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin ("PTZ controls", &application->state.windows.show_camera_controls, ImGuiWindowFlags_NoResize);

  _show_ptz_controls (application, false);

  ImGui::End ();
}

static inline void
configure_window_show_pmx_media_tx_stats (const char * label, PulseMediaTxStats * tx_stats, bool is_video)
{
  if (tx_stats) {
    if (ImGui::BeginTable (label, 3, ImGuiTableFlags_None) == 1) {
      ImGui::TableSetupColumn ("TX", ImGuiTableColumnFlags_None);
      std::stringstream label;
      label << "Last " << tx_stats->window_size << " seconds" << std::endl;
      ImGui::TableSetupColumn (label.str ().c_str (), ImGuiTableColumnFlags_None);
      std::stringstream total;
      total << "Total (" << (tx_stats->last_rtp_activity - tx_stats->first_rtp_activity) / 1000000000 << " seconds)"
            << std::endl;
      ImGui::TableSetupColumn (total.str ().c_str (), ImGuiTableColumnFlags_None);
      ImGui::TableHeadersRow ();

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Packets sent");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64, tx_stats->windowed_packets_sent);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64, tx_stats->total_packets_sent);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Packets lost");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64 " (%.02f%%)", tx_stats->windowed_packets_lost, tx_stats->windowed_packets_lost_pct);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64 " (%.02f%%)", tx_stats->total_packets_lost, tx_stats->total_packets_lost_pct);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Bytes sent");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64, tx_stats->windowed_bytes);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64, tx_stats->total_bytes);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Bitrate");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%u", tx_stats->windowed_bitrate);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%u", tx_stats->total_bitrate);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Jitter(ms)");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%f", tx_stats->windowed_jitter_ms);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%f", tx_stats->total_jitter_ms);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Round-trip time");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%f", tx_stats->rtt_ms);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Encoding");
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%s", tx_stats->encoding_name ? tx_stats->encoding_name : "<unknown>");

      if (is_video) {
        ImGui::TableNextRow ();
        ImGui::TableSetColumnIndex (0);
        ImGui::Text ("Resolution");
        ImGui::TableSetColumnIndex (2);
        ImGui::Text ("%ux%u@%.2f", tx_stats->width, tx_stats->height, tx_stats->framerate);
      }

      ImGui::EndTable ();
    }
  } else {
    if (ImGui::BeginTable (label, 2, ImGuiTableFlags_None) == 1) {
      ImGui::TableSetupColumn ("TX", ImGuiTableColumnFlags_None);
      ImGui::TableSetupColumn ("No data available.", ImGuiTableColumnFlags_None);
      ImGui::TableHeadersRow ();
      ImGui::EndTable ();
    }
  }
};

static inline void
configure_window_show_pmx_media_rx_stats (const char * label, PulseMediaRxStats * rx_stats, bool is_video)
{
  if (rx_stats) {
    if (ImGui::BeginTable (label, 3, ImGuiTableFlags_None) == 1) {
      ImGui::TableSetupColumn ("RX", ImGuiTableColumnFlags_None);
      std::stringstream label;
      label << "Last " << rx_stats->window_size << " seconds" << std::endl;
      ImGui::TableSetupColumn (label.str ().c_str (), ImGuiTableColumnFlags_None);
      std::stringstream total;
      total << "Total (" << (rx_stats->last_rtp_activity - rx_stats->first_rtp_activity) / 1000000000 << " seconds)"
            << std::endl;
      ImGui::TableSetupColumn (total.str ().c_str (), ImGuiTableColumnFlags_None);
      ImGui::TableHeadersRow ();

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Packets recv");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64, rx_stats->windowed_packets_received);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64, rx_stats->total_packets_received);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Packets lost");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64 " (%.02f%%)", rx_stats->windowed_packets_lost, rx_stats->windowed_packets_lost_pct);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64 " (%.02f%%)", rx_stats->total_packets_lost, rx_stats->total_packets_lost_pct);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Packets actual lost");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64 " (%.02f%%)", rx_stats->windowed_packets_actual_lost,
                   rx_stats->windowed_packets_actual_lost_pct);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64 " (%.02f%%)", rx_stats->total_packets_actual_lost,
                   rx_stats->total_packets_actual_lost_pct);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Packets late");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64, rx_stats->windowed_packets_late);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64, rx_stats->total_packets_late);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Packets duplicates");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64, rx_stats->windowed_packets_duplicates);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64, rx_stats->total_packets_duplicates);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("RTX received");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64, rx_stats->windowed_rtx_received);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64, rx_stats->total_rtx_received);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("RTX success");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64, rx_stats->windowed_rtx_success);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64, rx_stats->total_rtx_success);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("RTX RTT");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%f", rx_stats->windowed_rtx_rtt_ms);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%f", rx_stats->total_rtx_rtt_ms);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Bytes");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%" PRIu64, rx_stats->windowed_bytes);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%" PRIu64, rx_stats->total_bytes);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Jitter(ms)");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%f", rx_stats->windowed_jitter_ms);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%f", rx_stats->total_jitter_ms);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Bitrate");
      ImGui::TableSetColumnIndex (1);
      ImGui::Text ("%u", rx_stats->windowed_bitrate);
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%u", rx_stats->total_bitrate);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("");

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Instant bitrate");
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%u", rx_stats->instant_bitrate);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("JB jitter(ms)");
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%f", rx_stats->jb_jitter_ms);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("JB latency(ms)");
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%u", rx_stats->jb_latency_ms);

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      ImGui::Text ("Encoding");
      ImGui::TableSetColumnIndex (2);
      ImGui::Text ("%s", rx_stats->encoding_name ? rx_stats->encoding_name : "<unknown>");

      if (is_video) {
        ImGui::TableNextRow ();
        ImGui::TableSetColumnIndex (0);
        ImGui::Text ("Resolution");
        ImGui::TableSetColumnIndex (2);
        ImGui::Text ("%ux%u@%.2f", rx_stats->width, rx_stats->height, rx_stats->framerate);

        ImGui::TableNextRow ();
        ImGui::TableSetColumnIndex (0);
        ImGui::Text ("Face detection");
        ImGui::TableSetColumnIndex (2);
        ImGui::Text ("%s", rx_stats->face_detection_enabled ? "Enabled" : "Disabled");

        if (rx_stats->face_detection_enabled) {
          ImGui::TableNextRow ();
          ImGui::TableSetColumnIndex (0);
          ImGui::Text ("No face duration");
          ImGui::TableSetColumnIndex (2);
          ImGui::Text ("%u", rx_stats->no_face_duration_ms);

          ImGui::TableNextRow ();
          ImGui::TableSetColumnIndex (0);
          ImGui::Text ("No movement duration");
          ImGui::TableSetColumnIndex (2);
          ImGui::Text ("%u", rx_stats->no_face_duration_ms);

          ImGui::TableNextRow ();
          ImGui::TableSetColumnIndex (0);
          ImGui::Text ("Num faces");
          ImGui::TableSetColumnIndex (2);
          ImGui::Text ("%d", rx_stats->num_faces);
        }
      }

      ImGui::EndTable ();
    }
  } else {
    if (ImGui::BeginTable (label, 2, ImGuiTableFlags_None) == 1) {
      ImGui::TableSetupColumn ("RX", ImGuiTableColumnFlags_None);
      ImGui::TableSetupColumn ("No data available.", ImGuiTableColumnFlags_None);
      ImGui::TableHeadersRow ();
      ImGui::EndTable ();
    }
  }
};

ImPlotPoint
configure_window_show_rx_stats_graphs_getter (int idx, void * data)
{
  ImPlotPoint p = {};
  if (data) {
    std::vector<PulseMediaRxStats *> & stats_v = *reinterpret_cast<std::vector<PulseMediaRxStats *> *> (data);
    PulseMediaRxStats * stats = stats_v[idx];
    if (stats) {
      p.x = (double)stats->last_rtp_activity / (double)1000000000;
      p.y = (double)stats->total_bitrate / (double)1000;
    }
  }
  return p;
}

ImPlotPoint
configure_window_show_tx_stats_graphs_getter (int idx, void * data)
{
  ImPlotPoint p = {};
  if (data) {
    std::vector<PulseMediaTxStats *> & stats_v = *reinterpret_cast<std::vector<PulseMediaTxStats *> *> (data);
    PulseMediaTxStats * stats = stats_v[idx];
    if (stats) {
      p.x = (double)stats->last_rtp_activity / (double)1000000000;
      p.y = (double)stats->total_bitrate / (double)1000;
    }
  }
  return p;
}

void
show_stats_graphs_plot_data (PexNinja * application, stats_src ss, stats_dir sd, stats_entry se, size_t range,
                             const char * override_legend)
{
  if (application->state.media_stats_entries.has_stats[ss][sd][se] == false)
    return;

  size_t start_idx = 0;
  size_t entries = application->state.media_stats_entries.entries.size ();
  if (range && entries >= range) {
    start_idx = entries - range;

    bool has_stats = false;
    for (size_t i = start_idx; i < entries; i++) {
      if (application->state.media_stats_entries.entries[i].stats[ss][sd][se] != 0) {
        has_stats = true;
        break;
      }
    }
    if (has_stats == false)
      return;
    entries = range;
  }

  ImU64 * x = &application->state.media_stats_entries.entries[start_idx].rel_ts;
  ImU64 * y = &application->state.media_stats_entries.entries[start_idx].stats[ss][sd][se];

  if (override_legend == NULL) {
    int legend_idx = (int)(ss * 2) + sd;
    const char * legends[] = {"audio_rx", "audio_tx", "video_rx", "video_tx", "slides_rx", "slides_tx"};
    override_legend = legends[legend_idx];
  }

  switch (se) {
    case STATS_ENTRY_RESOLUTION_X:
    case STATS_ENTRY_RESOLUTION_Y:
      ImPlot::PlotStairs (override_legend, x, y, (int)entries, 0, 0, sizeof (struct PexNinjaMediaStatsEntry));
      break;
    case STATS_ENTRY_FPS:
      ImPlot::PlotShaded (override_legend, x, y, (int)entries, 0, 0, 0, sizeof (struct PexNinjaMediaStatsEntry));
      break;
    default:
      ImPlot::PlotLine (override_legend, x, y, (int)entries, 0, 0, sizeof (struct PexNinjaMediaStatsEntry));
      break;
  }
}

void
show_stats_graphs (PexNinja * application)
{
  assert (application);

  /* We should already be holding the media_stats_lock here! */

  const char * intervals[] = {"Lifetime", "30 minutes", "15 minutes", "5 minutes",
                              "1 minute", "30 seconds", "15 seconds", "5 seconds"};
  const int intervals_i[] = {0, 1800, 900, 300, 60, 30, 15, 5};
  static int current_interval_idx = 0;

  if (ImGui::CollapsingHeader ("Connection graphs", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginCombo ("Plotting period", intervals[current_interval_idx], ImGuiComboFlags_None)) {
      for (int n = 0; n < IM_ARRAYSIZE (intervals); n++) {
        bool is_selected = (current_interval_idx == n);
        if (ImGui::Selectable (intervals[n], is_selected)) {
          current_interval_idx = n;
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus ();
      }
      ImGui::EndCombo ();
    }

    const std::tm calendar_time = *std::localtime (std::addressof (application->state.media_stats_entries.start_ts));
    std::stringstream x_label;
    x_label << "Seconds (since " << std::setfill ('0') << std::setw (2) << calendar_time.tm_hour << ":"
            << std::setfill ('0') << std::setw (2) << calendar_time.tm_min << ":" << std::setfill ('0') << std::setw (2)
            << calendar_time.tm_sec << ")";

    if (ImGui::BeginTable ("VideoStats", 2, ImGuiTableFlags_SizingStretchSame) == 1) {
      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      if (ImPlot::BeginPlot ("Audio bitrate")) {
        ImPlot::SetupAxis (ImAxis_X1, x_label.str ().c_str (), ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis (ImAxis_Y1, "Kbps", ImPlotAxisFlags_AutoFit);
        ImPlot::PushStyleVar (ImPlotStyleVar_FillAlpha, 0.25f);
        show_stats_graphs_plot_data (application, STAT_SRC_AUDIO, STAT_DIR_RX, STATS_ENTRY_BITRATE,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_AUDIO, STAT_DIR_TX, STATS_ENTRY_BITRATE,
                                     intervals_i[current_interval_idx], NULL);
        ImPlot::PopStyleVar ();
        ImPlot::EndPlot ();
      }
      ImGui::TableSetColumnIndex (1);
      if (ImPlot::BeginPlot ("Video bitrate")) {
        ImPlot::SetupAxis (ImAxis_X1, x_label.str ().c_str (), ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis (ImAxis_Y1, "Kbps", ImPlotAxisFlags_AutoFit);
        ImPlot::PushStyleVar (ImPlotStyleVar_FillAlpha, 0.25f);
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_RX, STATS_ENTRY_BITRATE,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_TX, STATS_ENTRY_BITRATE,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_RX, STATS_ENTRY_BITRATE,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_TX, STATS_ENTRY_BITRATE,
                                     intervals_i[current_interval_idx], NULL);
        ImPlot::PopStyleVar ();
        ImPlot::EndPlot ();
      }

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      if (ImPlot::BeginPlot ("Jitter")) {
        ImPlot::SetupAxis (ImAxis_X1, x_label.str ().c_str (), ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis (ImAxis_Y1, "Jitter (micro seconds)", ImPlotAxisFlags_AutoFit);
        ImPlot::PushStyleVar (ImPlotStyleVar_FillAlpha, 0.25f);
        show_stats_graphs_plot_data (application, STAT_SRC_AUDIO, STAT_DIR_RX, STATS_ENTRY_JITTER,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_AUDIO, STAT_DIR_TX, STATS_ENTRY_JITTER,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_RX, STATS_ENTRY_JITTER,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_TX, STATS_ENTRY_JITTER,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_RX, STATS_ENTRY_JITTER,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_TX, STATS_ENTRY_JITTER,
                                     intervals_i[current_interval_idx], NULL);

        ImPlot::PopStyleVar ();
        ImPlot::EndPlot ();
      }
      ImGui::TableSetColumnIndex (1);
      if (ImPlot::BeginPlot ("Packetloss")) {
        ImPlot::SetupAxis (ImAxis_X1, x_label.str ().c_str (), ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis (ImAxis_Y1, "Packets lost", ImPlotAxisFlags_AutoFit);
        ImPlot::PushStyleVar (ImPlotStyleVar_FillAlpha, 0.25f);
        show_stats_graphs_plot_data (application, STAT_SRC_AUDIO, STAT_DIR_RX, STATS_ENTRY_PACKETLOSS,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_AUDIO, STAT_DIR_TX, STATS_ENTRY_PACKETLOSS,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_RX, STATS_ENTRY_PACKETLOSS,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_TX, STATS_ENTRY_PACKETLOSS,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_RX, STATS_ENTRY_PACKETLOSS,
                                     intervals_i[current_interval_idx], NULL);
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_TX, STATS_ENTRY_PACKETLOSS,
                                     intervals_i[current_interval_idx], NULL);

        ImPlot::PopStyleVar ();
        ImPlot::EndPlot ();
      }

      ImGui::TableNextRow ();
      ImGui::TableSetColumnIndex (0);
      if (ImPlot::BeginPlot ("Rtx")) {
        ImPlot::SetupAxis (ImAxis_X1, x_label.str ().c_str (), ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis (ImAxis_Y1, "RTX", ImPlotAxisFlags_AutoFit);
        ImPlot::PushStyleVar (ImPlotStyleVar_FillAlpha, 0.25f);
        show_stats_graphs_plot_data (application, STAT_SRC_AUDIO, STAT_DIR_RX, STATS_ENTRY_RTX_RECEIVED,
                                     intervals_i[current_interval_idx], "audio_rtx_received");
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_RX, STATS_ENTRY_RTX_RECEIVED,
                                     intervals_i[current_interval_idx], "video_rtx_received");
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_RX, STATS_ENTRY_RTX_RECEIVED,
                                     intervals_i[current_interval_idx], "slides_rtx_received");
        show_stats_graphs_plot_data (application, STAT_SRC_AUDIO, STAT_DIR_RX, STATS_ENTRY_RTX_SUCCESS,
                                     intervals_i[current_interval_idx], "audio_rtx_success");
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_RX, STATS_ENTRY_RTX_SUCCESS,
                                     intervals_i[current_interval_idx], "video_rtx_success");
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_RX, STATS_ENTRY_RTX_SUCCESS,
                                     intervals_i[current_interval_idx], "slides_rtx_success");

        ImPlot::PopStyleVar ();
        ImPlot::EndPlot ();
      }

      ImGui::TableSetColumnIndex (1);
      if (ImPlot::BeginPlot ("Video resolutions")) {
        ImPlot::SetupAxis (ImAxis_X1, x_label.str ().c_str (), ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis (ImAxis_Y1, "Pixels", ImPlotAxisFlags_AutoFit);
        ImPlot::PushStyleVar (ImPlotStyleVar_FillAlpha, 0.25f);
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_RX, STATS_ENTRY_RESOLUTION_X,
                                     intervals_i[current_interval_idx], "video_rx_width");
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_RX, STATS_ENTRY_RESOLUTION_Y,
                                     intervals_i[current_interval_idx], "video_rx_height");
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_TX, STATS_ENTRY_RESOLUTION_X,
                                     intervals_i[current_interval_idx], "video_tx_width");
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_TX, STATS_ENTRY_RESOLUTION_Y,
                                     intervals_i[current_interval_idx], "video_tx_height");
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_RX, STATS_ENTRY_FPS,
                                     intervals_i[current_interval_idx], "video_rx_fps");
        show_stats_graphs_plot_data (application, STAT_SRC_VIDEO, STAT_DIR_TX, STATS_ENTRY_FPS,
                                     intervals_i[current_interval_idx], "video_tx_fps");
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_RX, STATS_ENTRY_RESOLUTION_X,
                                     intervals_i[current_interval_idx], "slides_rx_width");
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_RX, STATS_ENTRY_RESOLUTION_Y,
                                     intervals_i[current_interval_idx], "slides_rx_height");
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_TX, STATS_ENTRY_RESOLUTION_X,
                                     intervals_i[current_interval_idx], "slides_tx_width");
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_TX, STATS_ENTRY_RESOLUTION_Y,
                                     intervals_i[current_interval_idx], "slides_tx_height");
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_RX, STATS_ENTRY_FPS,
                                     intervals_i[current_interval_idx], "slides_rx_fps");
        show_stats_graphs_plot_data (application, STAT_SRC_SLIDES, STAT_DIR_TX, STATS_ENTRY_FPS,
                                     intervals_i[current_interval_idx], "slides_tx_fps");
        ImPlot::PopStyleVar ();
        ImPlot::EndPlot ();
      }

      ImGui::EndTable ();
    }
  }
}

static inline void
configure_window_show_pmx_media_stats (PexNinja * application)
{
  const char * intervals[] = {"1 second",   "3 seconds",  "5 seconds", "10 seconds",
                              "20 seconds", "30 seconds", "1 minute",  "5 minutes"};
  const int intervals_i[] = {1, 3, 5, 10, 20, 30, 60, 300};
  static int current_interval_idx = 3;

  ImGui::SetNextWindowSize (ImVec2 (850, 500), ImGuiCond_FirstUseEver);
  ImGui::Begin ("Media Stats", &application->state.windows.show_pmx_media_stats, ImGuiWindowFlags_None);
  if (application->state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
    std::lock_guard<std::mutex> lock (application->state.media_stats_lock);
    if (application->state.media_stats) {
      show_stats_graphs (application);

      ImGui::Separator ();
      if (ImGui::CollapsingHeader ("Detailed media stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginCombo ("Lookback window", intervals[current_interval_idx], ImGuiComboFlags_None)) {
          for (int n = 0; n < IM_ARRAYSIZE (intervals); n++) {
            bool is_selected = (current_interval_idx == n);
            if (ImGui::Selectable (intervals[n], is_selected)) {
              current_interval_idx = n;
              application->state.media_stats_window_secs = intervals_i[current_interval_idx];
            }
            if (is_selected)
              ImGui::SetItemDefaultFocus ();
          }
          ImGui::EndCombo ();
        }

        if (ImGui::CollapsingHeader ("Audio", ImGuiTreeNodeFlags_None)) {
          if (ImGui::BeginTable ("AudioStats", 2, ImGuiTableFlags_SizingStretchSame) == 1) {
            ImGui::TableNextRow ();
            ImGui::TableSetColumnIndex (0);
            configure_window_show_pmx_media_rx_stats ("audio-rx", &application->state.media_stats->audio_rx, false);
            ImGui::TableSetColumnIndex (1);
            configure_window_show_pmx_media_tx_stats ("audio-tx", &application->state.media_stats->audio_tx, false);
            ImGui::EndTable ();
          }
        }
        if (ImGui::CollapsingHeader ("Video", ImGuiTreeNodeFlags_None)) {
          if (ImGui::BeginTable ("VideoStats", 2, ImGuiTableFlags_SizingStretchProp) == 1) {
            ImGui::TableNextRow ();
            ImGui::TableSetColumnIndex (0);
            configure_window_show_pmx_media_rx_stats ("video-rx", &application->state.media_stats->video_rx, true);
            ImGui::TableSetColumnIndex (1);
            configure_window_show_pmx_media_tx_stats ("video-tx", &application->state.media_stats->video_tx, true);
            ImGui::EndTable ();
          }
        }
        if (ImGui::CollapsingHeader ("Slides", ImGuiTreeNodeFlags_None)) {
          if (ImGui::BeginTable ("SlidesStats", 2, ImGuiTableFlags_SizingStretchProp) == 1) {
            ImGui::TableNextRow ();
            ImGui::TableSetColumnIndex (0);
            configure_window_show_pmx_media_rx_stats ("slides-rx", &application->state.media_stats->slides_rx, true);
            ImGui::TableSetColumnIndex (1);
            configure_window_show_pmx_media_tx_stats ("slides-tx", &application->state.media_stats->slides_tx, true);
            ImGui::EndTable ();
          }
        }
      }
    } else {
      ImGui::Text ("Failed to retrieve stats!");
    }
  } else {
    application->state.windows.show_pmx_media_stats = false;
    ImGui::Text ("Waiting for connection....");
  }
  ImGui::End ();
}

static inline void
configure_window_fecc_window (PexNinja * application)
{
  ImGui::SetNextWindowSize (ImVec2 (250, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin ("Send FECC to participant", &application->state.windows.show_fecc_window, ImGuiWindowFlags_None);

  ImGui::TextWrapped ("recepient: %s", application->state.fecc.participant_display_name.c_str ());

  ImGui::Separator ();

  _show_ptz_controls (application, true);

  ImGui::End ();
}

static void
on_audio_level_changed (void * user_context, const PulseAudioLevelMetasList * list)
{
  PexNinja * application = (PexNinja *)user_context;
  AudioLevelQueue * mic_audio_levels = application->state.mic_audio_levels;

  std::lock_guard<std::mutex> lock (mic_audio_levels->mutex);
  for (size_t i = 0; i < list->len; i++) {
    mic_audio_levels->items.push_back (list->metas[i].level);
  }

  while ((int)mic_audio_levels->items.size () > audio_levels_window_size)
    mic_audio_levels->items.pop_front ();
}

static void
set_show_settings (PexNinja * application, bool value)
{
  if (value != application->state.windows.show_config) {

    if (value) {
      pulse_register_device_audio_level_callback (application->client, PULSE_MEDIA_INPUT, audio_levels_window_size,
                                                  on_audio_level_changed, application);

      /* get ready for preflight ! */
      PulseDataSessionConfig * config = pulse_data_session_config_video_new ();
      pulse_data_session_connect_output (application->client, config, PULSE_MEDIA_CONTENT_PREFLIGHT);
      pulse_data_session_config_free (config);

    } else {
      pulse_deregister_device_audio_level_callback (application->client, PULSE_MEDIA_INPUT);

      pulse_data_session_disconnect (application->client, PULSE_MEDIA_VIDEO, PULSE_MEDIA_OUTPUT,
                                     PULSE_MEDIA_CONTENT_PREFLIGHT);
      pulse_device_session_disconnect_main_video (application->client, PULSE_MEDIA_CONTENT_PREFLIGHT,
                                                  PULSE_MEDIA_INPUT);
    }

    application->state.selected_cam_preflight_idx = -1;
    application->state.windows.show_config = value;
  }
}
#if defined(HOST_WINDOWS)
static bool
IsWindowValidForCapture (HWND hwnd)
{
  if (hwnd == GetShellWindow ())
    return false;
  if (!IsWindowVisible (hwnd))
    return false;

  if (GetParent (hwnd) != NULL)
    return false;
  if (GetAncestor (hwnd, 2) != hwnd)
    return false;

  int style = GetWindowLong (hwnd, GWL_STYLE);
  if (style & WS_DISABLED)
    return false;

  BOOL cloaked = false;
  HRESULT hr = DwmGetWindowAttribute (hwnd, DWMWA_CLOAKED, &cloaked, sizeof (BOOL));
  if (hr == S_OK && cloaked)
    return false;

  return true;
}

static BOOL
windowsProcFunc (HWND hwnd, LPARAM lParam)
{
  auto ret = (std::vector<HWND> *)lParam;
  if (IsWindowValidForCapture (hwnd))
    ret->push_back (hwnd);
  return true;
}

static std::vector<HWND>
enumerate_desktop_windows ()
{
  auto ret = std::vector<HWND> ();
  EnumWindows (windowsProcFunc, (LPARAM)&ret);
  return ret;
}

static std::string
get_window_handle_name (HWND hwnd)
{
  char title[64];

  if (GetWindowText (hwnd, title, sizeof (title)) && pex_strrstr (title, "PexNinja") == NULL)
    return std::string (title);

  return "";
}

static BOOL CALLBACK
monitorEnumFunc (HMONITOR monitor, HDC, LPRECT, LPARAM lParam)
{
  auto ret = (std::vector<HMONITOR> *)lParam;
  ret->push_back (monitor);
  return true;
}

static std::vector<PexNinjaDisplayHandle>
enumerate_displays ()
{
  auto ret = std::vector<HMONITOR> ();
  EnumDisplayMonitors (NULL, NULL, monitorEnumFunc, (LPARAM)&ret);
  return ret;
}

static std::string
get_display_name (HMONITOR monitor)
{
  MONITORINFOEX info = {0};
  info.cbSize = sizeof (info);

  if (!GetMonitorInfo (monitor, &info))
    return "";
  return std::string (info.szDevice);
}

#elif defined(HAVE_X11)

static Display *
_get_x11_display ()
{
  static Display * dpy = nullptr;
  if (dpy == nullptr)
    dpy = XOpenDisplay (nullptr);
  return dpy;
}

static std::string
_x11_get_window_name (Display * dpy, Window win)
{
  /* Try _NET_WM_NAME (UTF-8) first */
  Atom utf8_name = XInternAtom (dpy, "_NET_WM_NAME", True);
  Atom utf8_type = XInternAtom (dpy, "UTF8_STRING", True);

  if (utf8_name != None && utf8_type != None) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char * data = nullptr;

    int status = XGetWindowProperty (dpy, win, utf8_name, 0, 256, False, utf8_type, &actual_type, &actual_format,
                                     &nitems, &bytes_after, &data);
    if (status == Success && actual_type == utf8_type && nitems > 0) {
      std::string name ((const char *)data, nitems);
      XFree (data);
      return name;
    }
    if (data)
      XFree (data);
  }

  /* Fallback to WM_NAME (XTextProperty) */
  XTextProperty text_prop;
  if (XGetWMName (dpy, win, &text_prop) && text_prop.value) {
    std::string name ((const char *)text_prop.value);
    XFree (text_prop.value);
    return name;
  }

  return "";
}

static bool
_x11_is_valid_window (Display * dpy, Window win)
{
  /* Must have _NET_WM_WINDOW_TYPE */
  Atom type_atom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE", True);
  Atom normal_atom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_NORMAL", True);
  Atom dialog_atom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DIALOG", True);

  if (type_atom == None)
    return false;

  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char * data = nullptr;

  int status = XGetWindowProperty (dpy, win, type_atom, 0, 32, False, XA_ATOM, &actual_type, &actual_format, &nitems,
                                   &bytes_after, &data);
  if (status != Success || actual_type != XA_ATOM || nitems < 1) {
    if (data)
      XFree (data);
    return false;
  }

  Atom * types = (Atom *)data;
  bool valid = false;
  for (unsigned long i = 0; i < nitems; i++) {
    if (types[i] == normal_atom || types[i] == dialog_atom) {
      valid = true;
      break;
    }
  }
  XFree (data);

  if (!valid)
    return false;

  /* Must not be in _NET_WM_STATE_HIDDEN (minimised) */
  Atom state_atom = XInternAtom (dpy, "_NET_WM_STATE", True);
  Atom hidden_atom = XInternAtom (dpy, "_NET_WM_STATE_HIDDEN", True);

  if (state_atom != None && hidden_atom != None) {
    data = nullptr;
    status = XGetWindowProperty (dpy, win, state_atom, 0, 32, False, XA_ATOM, &actual_type, &actual_format, &nitems,
                                 &bytes_after, &data);
    if (status == Success && actual_type == XA_ATOM) {
      Atom * states = (Atom *)data;
      for (unsigned long i = 0; i < nitems; i++) {
        if (states[i] == hidden_atom) {
          XFree (data);
          return false;
        }
      }
    }
    if (data)
      XFree (data);
  }

  /* Must have a non-empty name */
  std::string name = _x11_get_window_name (dpy, win);
  if (name.empty ())
    return false;

  /* Skip our own window */
  if (name.find ("PexNinja") != std::string::npos)
    return false;

  return true;
}

static void
_x11_collect_windows (Display * dpy, Window root, std::vector<PexNinjaWindowHandle> & out)
{
  /* Use _NET_CLIENT_LIST if available (stacking order) */
  Atom client_list = XInternAtom (dpy, "_NET_CLIENT_LIST_STACKING", True);
  if (client_list == None)
    client_list = XInternAtom (dpy, "_NET_CLIENT_LIST", True);

  if (client_list != None) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char * data = nullptr;

    int status = XGetWindowProperty (dpy, root, client_list, 0, 4096, False, XA_WINDOW, &actual_type, &actual_format,
                                     &nitems, &bytes_after, &data);
    if (status == Success && actual_type == XA_WINDOW && nitems > 0) {
      Window * windows = (Window *)data;
      for (unsigned long i = 0; i < nitems; i++) {
        if (_x11_is_valid_window (dpy, windows[i]))
          out.push_back ((PexNinjaWindowHandle)windows[i]);
      }
      XFree (data);
      return;
    }
    if (data)
      XFree (data);
  }

  /* Fallback: recurse through the tree to immediate children */
  Window parent, *children = nullptr;
  unsigned int nchildren = 0;
  if (XQueryTree (dpy, root, &root, &parent, &children, &nchildren)) {
    for (unsigned int i = 0; i < nchildren; i++) {
      if (_x11_is_valid_window (dpy, children[i]))
        out.push_back ((PexNinjaWindowHandle)children[i]);
    }
    if (children)
      XFree (children);
  }
}

static std::vector<PexNinjaWindowHandle>
enumerate_desktop_windows ()
{
  std::vector<PexNinjaWindowHandle> ret;

  Display * dpy = _get_x11_display ();
  if (dpy == nullptr)
    return ret;

  Window root = DefaultRootWindow (dpy);
  _x11_collect_windows (dpy, root, ret);

  return ret;
}

static std::string
get_window_handle_name (PexNinjaWindowHandle handle)
{
  Display * dpy = _get_x11_display ();
  if (dpy == nullptr)
    return "";

  return _x11_get_window_name (dpy, (Window)handle);
}

static std::vector<PexNinjaDisplayHandle>
enumerate_displays ()
{
  /* X11 doesn't have a clean per-monitor handle like HMONITOR.
   * Return the root window as "the display" for full-screen capture. */
  std::vector<PexNinjaDisplayHandle> ret;

  Display * dpy = _get_x11_display ();
  if (dpy == nullptr)
    return ret;

  ret.push_back ((PexNinjaDisplayHandle)DefaultRootWindow (dpy));
  return ret;
}

static std::string
get_display_name (PexNinjaDisplayHandle handle)
{
  Display * dpy = _get_x11_display ();
  if (dpy == nullptr)
    return "";

  char buf[64];
  snprintf (buf, sizeof (buf), "Display :%s (root 0x%x)", DisplayString (dpy), (unsigned int)handle);
  return std::string (buf);
}

#elif defined(HOST_DARWIN)

#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>

/* On macOS, a "window handle" is a CGWindowID (uint32_t).
 * A "display handle" is a CGDirectDisplayID (uint32_t).
 * Both fit in our int-sized typedefs. */

static std::string
_cf_string_to_std (CFStringRef cfstr)
{
  if (cfstr == NULL)
    return "";

  CFIndex len = CFStringGetLength (cfstr);
  CFIndex max_size = CFStringGetMaximumSizeForEncoding (len, kCFStringEncodingUTF8) + 1;
  std::string result (max_size, '\0');

  if (!CFStringGetCString (cfstr, &result[0], max_size, kCFStringEncodingUTF8))
    return "";

  result.resize (strlen (result.c_str ()));
  return result;
}

static std::vector<PexNinjaWindowHandle>
enumerate_desktop_windows ()
{
  std::vector<PexNinjaWindowHandle> ret;

  /* Get all on-screen windows, ordered front-to-back.
   * kCGWindowListOptionOnScreenOnly excludes off-screen/minimised windows.
   * kCGWindowListExcludeDesktopElements excludes the desktop and menu bar. */
  CFArrayRef window_list =
    CGWindowListCopyWindowInfo (kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);

  if (window_list == NULL)
    return ret;

  CFIndex count = CFArrayGetCount (window_list);
  for (CFIndex i = 0; i < count; i++) {
    CFDictionaryRef info = (CFDictionaryRef)CFArrayGetValueAtIndex (window_list, i);

    /* Only include "normal" windows (layer 0) — this filters out
     * the menu bar, Dock, Spotlight, screensavers, etc. */
    CFNumberRef layer_ref = (CFNumberRef)CFDictionaryGetValue (info, kCGWindowLayer);
    int layer = -1;
    if (layer_ref != NULL)
      CFNumberGetValue (layer_ref, kCFNumberIntType, &layer);
    if (layer != 0)
      continue;

    /* Must have a non-empty name */
    CFStringRef name_ref = (CFStringRef)CFDictionaryGetValue (info, kCGWindowName);
    std::string name = _cf_string_to_std (name_ref);
    if (name.empty ())
      continue;

    /* Skip our own window */
    CFStringRef owner_ref = (CFStringRef)CFDictionaryGetValue (info, kCGWindowOwnerName);
    std::string owner = _cf_string_to_std (owner_ref);
    if (owner.find ("PexNinja") != std::string::npos)
      continue;

    /* Must have a reasonable size (skip tiny helper windows) */
    CFDictionaryRef bounds_ref = (CFDictionaryRef)CFDictionaryGetValue (info, kCGWindowBounds);
    if (bounds_ref != NULL) {
      CGRect bounds;
      CGRectMakeWithDictionaryRepresentation (bounds_ref, &bounds);
      if (bounds.size.width < 50 || bounds.size.height < 50)
        continue;
    }

    /* Extract the CGWindowID */
    CFNumberRef window_id_ref = (CFNumberRef)CFDictionaryGetValue (info, kCGWindowNumber);
    if (window_id_ref == NULL)
      continue;

    CGWindowID window_id = 0;
    CFNumberGetValue (window_id_ref, kCGWindowIDCFNumberType, &window_id);

    ret.push_back ((PexNinjaWindowHandle)window_id);
  }

  CFRelease (window_list);
  return ret;
}

static std::string
get_window_handle_name (PexNinjaWindowHandle handle)
{
  CGWindowID target_id = (CGWindowID)handle;

  /* Fetch info for just this one window */
  CFArrayRef window_list = CGWindowListCopyWindowInfo (kCGWindowListOptionIncludingWindow, target_id);

  if (window_list == NULL)
    return "";

  std::string result;
  if (CFArrayGetCount (window_list) > 0) {
    CFDictionaryRef info = (CFDictionaryRef)CFArrayGetValueAtIndex (window_list, 0);

    CFStringRef owner_ref = (CFStringRef)CFDictionaryGetValue (info, kCGWindowOwnerName);
    CFStringRef name_ref = (CFStringRef)CFDictionaryGetValue (info, kCGWindowName);

    std::string owner = _cf_string_to_std (owner_ref);
    std::string name = _cf_string_to_std (name_ref);

    if (!name.empty () && !owner.empty ())
      result = owner + " — " + name;
    else if (!name.empty ())
      result = name;
    else if (!owner.empty ())
      result = owner;
  }

  CFRelease (window_list);
  return result;
}

static std::vector<PexNinjaDisplayHandle>
enumerate_displays ()
{
  std::vector<PexNinjaDisplayHandle> ret;

  /* Up to 16 displays should be more than enough */
  CGDirectDisplayID displays[16];
  uint32_t display_count = 0;

  CGError err = CGGetActiveDisplayList (16, displays, &display_count);
  if (err != kCGErrorSuccess)
    return ret;

  for (uint32_t i = 0; i < display_count; i++)
    ret.push_back ((PexNinjaDisplayHandle)displays[i]);

  return ret;
}

static std::string
get_display_name (PexNinjaDisplayHandle handle)
{
  CGDirectDisplayID display_id = (CGDirectDisplayID)handle;

  uint32_t width = CGDisplayPixelsWide (display_id);
  uint32_t height = CGDisplayPixelsHigh (display_id);
  bool is_main = CGDisplayIsMain (display_id);

  char buf[128];
  snprintf (buf, sizeof (buf), "%sDisplay %u (%ux%u)", is_main ? "Main " : "", display_id, width, height);

  return std::string (buf);
}

#else

static std::vector<PexNinjaDisplayHandle>
enumerate_displays ()
{
  return std::vector<PexNinjaDisplayHandle> ();
}

static std::vector<PexNinjaWindowHandle>
enumerate_desktop_windows ()
{
  return std::vector<PexNinjaWindowHandle> ();
}

static std::string
get_window_handle_name (PexNinjaWindowHandle handle)
{
  (void)handle;
  return "";
}

static std::string
get_display_name (PexNinjaDisplayHandle handle)
{
  (void)handle;
  return "";
}

#endif /* !defined(HOST_WINDOWS) */
static void
pexninja_start_video_mix (PexNinja * application, PexNinjaState::PexNinjaVideoMix & vm,
                          PulseVideoMixConfig * mix_config, PulseMediaContent media_content)
{
  PulseError err = pulse_video_mix_connect (application->client, mix_config, media_content);
  if (err != PULSE_SUCCESS) {
    PEX_LOG_WARNING ("Failed to connect video mix: %s", pulse_strerror (err));
    if (vm.desktop_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
      pulse_video_mix_input_release (application->client, vm.desktop_input);
      vm.desktop_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
      vm.desktop_input_kind = PexNinjaState::PexNinjaVideoMix::kBackgroundNone;
    }
    /* we only release camera if it's not presentation */
    if (vm.camera_input != PULSE_VIDEO_MIX_INPUT_ID_NONE && media_content != PULSE_MEDIA_CONTENT_PRESENTATION) {
      pulse_video_mix_input_release (application->client, vm.camera_input);
      vm.camera_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
    }
    return;
  }
  vm.media_content = media_content;
  vm.active = true;
}

/* Convenience overload: build the mix config from `vm`'s current
 * input/layout state and connect it. All non-trivial callers
 * (background-replacement toggle, presentation start, annotation
 * acquire, presentation stop-while-bg-replacement) go through this
 * — there is no reason to repeat the inputs[]/_build_mix_config
 * boilerplate per site. */
static void
pexninja_start_video_mix (PexNinja * application, PexNinjaState::PexNinjaVideoMix & vm, PulseMediaContent media_content)
{
  PulseVideoMixInput inputs[kPexNinjaVideoMixMaxInputs];
  PulseVideoMixConfig config = _build_mix_config (vm, inputs);
  pexninja_start_video_mix (application, vm, &config, media_content);
}
static void
pexninja_stop_video_mix (PexNinja * application, PexNinjaState::PexNinjaVideoMix & vm)
{

  if (!vm.active)
    return;

  pulse_video_mix_disconnect (application->client, vm.media_content);

  if (vm.desktop_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    pulse_video_mix_input_release (application->client, vm.desktop_input);
    vm.desktop_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
    vm.desktop_input_kind = PexNinjaState::PexNinjaVideoMix::kBackgroundNone;
  }
  if (vm.camera_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    pulse_video_mix_input_release (application->client, vm.camera_input);
    vm.camera_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
  }
  if (vm.annotation_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    /* Don't release a borrowed handle — the owning Compositor
     * source library will release it via pexninja_release_source_library. */
    if (!vm.annotation_input_borrowed)
      pulse_video_mix_input_release (application->client, vm.annotation_input);
    vm.annotation_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
    vm.annotation_input_borrowed = false;
  }
  vm.active = false;
}
static void
_reconnect_video_mix (PexNinja * application, PexNinjaState::PexNinjaVideoMix & vm)
{
  PulseVideoMixInput inputs[kPexNinjaVideoMixMaxInputs];
  PulseVideoMixConfig config = _build_mix_config (vm, inputs);
  /* only reconnect what's already started */
  if (vm.active) {
    pulse_video_mix_disconnect (application->client, vm.media_content);
    PulseError err = pulse_video_mix_connect (application->client, &config, vm.media_content);
    if (err != PULSE_SUCCESS) {
      PEX_LOG_WARNING ("Failed to connect video mix: %s", pulse_strerror (err));
      vm.active = false;
    }
  }
}

/* -----------------------------------------------------------------------
 * Paint / drawing overlay
 *
 * The model (per the design and the new "draw locally, commit on
 * release" requirement):
 *
 *   - The Paint Tools window owns colour, thickness, tool, and a
 *     "drawing mode" toggle.
 *   - When drawing mode is enabled, paint_overlay_handle() inside the
 *     self-view window covers the video texture with an
 *     ImGui::InvisibleButton — that absorbs hover / drag events
 *     without occluding the image.
 *   - During a drag we collect points in two parallel vectors:
 *       * live_points_screen — for ImGui's draw list to paint a
 *         transient, anti-aliased preview directly into the window
 *         (instant feedback, no pipeline round-trip).
 *       * live_points_canvas — same points, mapped into the gfx
 *         canvas pixel space we declared at acquire time.
 *   - On mouse-release we send one stroke_begin / add_point* /
 *     stroke_end through Pulse and clear the preview. The next gfx
 *     frame the pipeline produces should look identical to the
 *     preview we just removed.
 *
 * The gfx input is acquired lazily on first drawing interaction
 * inside paint_overlay_handle() (i.e. the first mouse-press over
 * the self-view while drawing-mode is enabled), and is released
 * along with the rest of the video-mix inputs by
 * pexninja_stop_video_mix(). This matches how desktop_input is
 * treated. Opening the Paint Tools window on its own does not
 * acquire the input — there is nothing to draw on yet.
 * ----------------------------------------------------------------------- */

/* Locate a Gfx-kind source on the active Compositor canvas whose
 * region is on the wire (i.e. on the committed_doc). Returns the
 * first such source, or nullptr.
 *
 * Why this matters for paint: the Compositor (when its Take has
 * fired) owns the wire mix on the canvas's media_content. If we
 * allocate a fresh annotation_input here and call _reconnect_video_mix,
 * we'd clobber the Compositor's mix with the legacy slot config —
 * AND the strokes the user draws would land on a different
 * annotation_input than the one currently being rendered. Net
 * effect: paint silently does nothing. So when the user has
 * pre-staged a Gfx region on the canvas, we route paint straight
 * into that source's annotation handle. */
static PexNinjaState::PexNinjaSourceLibrary::Source *
_paint_find_gfx_source_on_active_canvas (PexNinja * application)
{
  using CO = PexNinjaState::PexNinjaCompositor;
  using SL = PexNinjaState::PexNinjaSourceLibrary;
  auto & comp = application->state.compositor;
  int ci = comp.active_canvas;
  if (ci < 0 || ci >= (int)CO::kCanvasCount)
    return nullptr;
  auto & canvas = comp.canvases[ci];
  /* Search the committed doc — that's what's actually on the wire. */
  for (const auto & r : canvas.committed.regions) {
    auto * src = compositor::_lookup_source (application, ci, r.source_lib_id);
    if (src && src->kind == SL::kGfx && src->input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE)
      return src;
  }
  /* Fall back to the editing doc — useful in auto-take mode where
   * there might not yet be a committed region for a freshly-added
   * Gfx source, but it's been eager-materialised. */
  for (const auto & r : canvas.editing.regions) {
    auto * src = compositor::_lookup_source (application, ci, r.source_lib_id);
    if (src && src->kind == SL::kGfx && src->input_id != PULSE_VIDEO_MIX_INPUT_ID_NONE)
      return src;
  }
  return nullptr;
}

static PulseError
_paint_ensure_annotation_input (PexNinja * application)
{
  auto & vm = application->state.video_mix;
  if (vm.annotation_input != PULSE_VIDEO_MIX_INPUT_ID_NONE)
    return PULSE_SUCCESS;

  /* Prefer borrowing a Gfx source from the active Compositor
   * canvas. If the user added an annotation-overlay region there,
   * its handle is already on the wire — strokes go straight onto
   * the visible composition. Skips _reconnect_video_mix entirely
   * (the Compositor owns the wire, not the legacy `vm`). */
  if (auto * gfx = _paint_find_gfx_source_on_active_canvas (application)) {
    vm.annotation_input = gfx->input_id;
    vm.annotation_input_borrowed = true;
    PEX_LOG_INFO ("paint: borrowing annotation_input %u from Compositor Gfx source '%s' on canvas %d",
              (unsigned)vm.annotation_input, gfx->name, application->state.compositor.active_canvas);
    return PULSE_SUCCESS;
  }

  PulseError err = pulse_video_mix_input_from_annotation (application->client, application->state.paint.canvas_width,
                                                          application->state.paint.canvas_height, &vm.annotation_input);
  if (err != PULSE_SUCCESS) {
    PEX_LOG_WARNING ("Failed to acquire annotation input: %s", pulse_strerror (err));
    return err;
  }
  vm.annotation_input_borrowed = false;

  /* If a video mix is already running, splice the gfx input in by
   * reconnecting with the new config. Otherwise — provided we're not
   * currently presenting — start the main mix now: this is what
   * actually swaps the outgoing MAIN session's source from the raw
   * camera tap to the videomixer's output. Mirrors the equivalent
   * path in bg_replacement_on_mix().
   *
   * NOTE: If a Compositor canvas has Take'n on MAIN, this branch
   * would clobber its wire. We avoid that by short-circuiting on
   * the borrow above whenever a Compositor Gfx source exists; this
   * legacy path only runs for users who haven't moved to the
   * Compositor flow at all. */
  if (vm.active) {
    _reconnect_video_mix (application, vm);
  } else if (!application->state.presenting) {
    pexninja_start_video_mix (application, vm, PULSE_MEDIA_CONTENT_MAIN);
  }

  return PULSE_SUCCESS;
}

static void
_paint_push_style_to_pulse (PexNinja * application)
{
  auto & vm = application->state.video_mix;
  auto & p = application->state.paint;
  if (vm.annotation_input == PULSE_VIDEO_MIX_INPUT_ID_NONE)
    return;
  uint8_t r = (uint8_t)(p.color.x * 255.0f + 0.5f);
  uint8_t g = (uint8_t)(p.color.y * 255.0f + 0.5f);
  uint8_t b = (uint8_t)(p.color.z * 255.0f + 0.5f);
  uint8_t a = (uint8_t)(p.color.w * 255.0f + 0.5f);
  pulse_annotation_set_color (application->client, vm.annotation_input, r, g, b, a);
  pulse_annotation_set_thickness (application->client, vm.annotation_input, (uint32_t)std::max (1, p.thickness));
}

/* Commit the locally-drawn preview as one stroke through the Pulse
 * gfx API, then drop the preview. */
static void
_paint_commit_active_stroke (PexNinja * application)
{
  auto & p = application->state.paint;
  auto & vm = application->state.video_mix;
  if (!p.active_drag) {
    return;
  }

  if (!p.live_points_canvas.empty () && vm.annotation_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    _paint_push_style_to_pulse (application);

    PulseAnnotationStrokeID sid = PULSE_ANNOTATION_STROKE_ID_NONE;
    if (pulse_annotation_stroke_begin (application->client, vm.annotation_input, &sid) == PULSE_SUCCESS) {
      for (const ImVec2 & pt : p.live_points_canvas) {
        pulse_annotation_stroke_add_point (application->client, vm.annotation_input, sid, (int32_t)pt.x, (int32_t)pt.y);
      }
      pulse_annotation_stroke_end (application->client, vm.annotation_input, sid);
    }
  }

  p.active_drag = false;
  p.live_points_screen.clear ();
  p.live_points_canvas.clear ();
}

/* Map a point in screen pixels (relative to the video widget origin)
 * into gfx canvas pixels, clamped to the documented [0..width-1] ×
 * [0..height-1] valid range. The Pulse layer also clamps, but doing
 * it here too keeps the locally-rendered preview points consistent
 * with what we eventually ship to Pulse. */
static ImVec2
_paint_screen_to_canvas (PexNinja * application, ImVec2 local, ImVec2 widget_size)
{
  auto & p = application->state.paint;
  float w = widget_size.x > 0.0f ? widget_size.x : 1.0f;
  float h = widget_size.y > 0.0f ? widget_size.y : 1.0f;
  float cx = (local.x / w) * (float)p.canvas_width;
  float cy = (local.y / h) * (float)p.canvas_height;
  float max_x = (p.canvas_width > 0) ? (float)(p.canvas_width - 1) : 0.0f;
  float max_y = (p.canvas_height > 0) ? (float)(p.canvas_height - 1) : 0.0f;
  if (cx < 0.0f)
    cx = 0.0f;
  else if (cx > max_x)
    cx = max_x;
  if (cy < 0.0f)
    cy = 0.0f;
  else if (cy > max_y)
    cy = max_y;
  return ImVec2 (cx, cy);
}

/* Drop any in-progress drag without committing it to Pulse. Used when
 * the user toggles drawing-mode off mid-drag (otherwise the buffered
 * point lists would survive the toggle and re-appear when drawing-mode
 * is re-enabled). */
static void
_paint_cancel_active_drag (PexNinja * application)
{
  auto & p = application->state.paint;
  if (!p.active_drag && p.live_points_screen.empty () && p.live_points_canvas.empty ())
    return;
  p.active_drag = false;
  p.live_points_screen.clear ();
  p.live_points_canvas.clear ();
}

/* Regenerate the live point lists for a rubber-band tool (line,
 * rectangle, ellipse) from the press anchor to the current cursor.
 * Called every frame the mouse is held while one of those tools is
 * active. Both buffers are filled in parallel so the on-screen
 * preview and the points eventually shipped to Pulse describe the
 * same shape.
 *
 * Shapes are emitted as polylines (not filled) so the existing
 * preview/commit paths handle them with no special-casing — the
 * thickness slider doubles as the outline weight. Closed shapes
 * repeat the first vertex at the end so the outline joins cleanly. */
static void
_paint_rebuild_rubberband (PexNinjaState::PexNinjaPaint & p, ImVec2 cursor_screen, ImVec2 cursor_canvas)
{
  const ImVec2 a_s = p.drag_anchor_screen;
  const ImVec2 a_c = p.drag_anchor_canvas;
  const ImVec2 b_s = cursor_screen;
  const ImVec2 b_c = cursor_canvas;

  p.live_points_screen.clear ();
  p.live_points_canvas.clear ();

  switch (p.tool) {
    case PexNinjaState::PexNinjaPaint::kLine:
      p.live_points_screen.push_back (a_s);
      p.live_points_canvas.push_back (a_c);
      p.live_points_screen.push_back (b_s);
      p.live_points_canvas.push_back (b_c);
      break;

    case PexNinjaState::PexNinjaPaint::kRectangle:
    {
      /* Anchor and cursor are opposite corners — derive the other two
       * from the bounding box and trace TL → TR → BR → BL → TL. */
      auto rect = [] (ImVec2 a, ImVec2 b, ImVec2 out[5]) {
        float x0 = std::min (a.x, b.x), x1 = std::max (a.x, b.x);
        float y0 = std::min (a.y, b.y), y1 = std::max (a.y, b.y);
        out[0] = ImVec2 (x0, y0);
        out[1] = ImVec2 (x1, y0);
        out[2] = ImVec2 (x1, y1);
        out[3] = ImVec2 (x0, y1);
        out[4] = out[0];
      };
      ImVec2 sc[5], cc[5];
      rect (a_s, b_s, sc);
      rect (a_c, b_c, cc);
      for (int i = 0; i < 5; ++i) {
        p.live_points_screen.push_back (sc[i]);
        p.live_points_canvas.push_back (cc[i]);
      }
      break;
    }

    case PexNinjaState::PexNinjaPaint::kEllipse:
    {
      /* Anchor and cursor define opposite corners of the ellipse's
       * bounding box. We sample the ellipse parametrically in both
       * coordinate spaces independently so the preview and the
       * shipped polyline agree under any non-uniform widget scaling. */
      constexpr int kSegments = 48;
      const ImVec2 cs ((a_s.x + b_s.x) * 0.5f, (a_s.y + b_s.y) * 0.5f);
      const ImVec2 rs (fabsf (b_s.x - a_s.x) * 0.5f, fabsf (b_s.y - a_s.y) * 0.5f);
      const ImVec2 cc ((a_c.x + b_c.x) * 0.5f, (a_c.y + b_c.y) * 0.5f);
      const ImVec2 rc (fabsf (b_c.x - a_c.x) * 0.5f, fabsf (b_c.y - a_c.y) * 0.5f);
      for (int i = 0; i <= kSegments; ++i) {
        const float t = (float)i * (2.0f * IM_PI) / (float)kSegments;
        const float ct = cosf (t);
        const float st = sinf (t);
        p.live_points_screen.push_back (ImVec2 (cs.x + rs.x * ct, cs.y + rs.y * st));
        p.live_points_canvas.push_back (ImVec2 (cc.x + rc.x * ct, cc.y + rc.y * st));
      }
      break;
    }

    default:
      /* Pencil should never reach here — it has its own per-move
       * append path. Fall through to leaving both buffers empty so
       * nothing renders if a future tool is mis-routed here. */
      break;
  }
}

/* Called from inside the self-view ImGui window after the video image
 * has been drawn. Lays an InvisibleButton over the image to capture
 * pointer events without occluding the texture. Renders the in-progress
 * preview into the foreground draw list. */
static void
paint_overlay_handle (PexNinja * application, ImVec2 image_origin, ImVec2 image_size)
{
  auto & p = application->state.paint;
  if (!p.drawing_mode || image_size.x <= 0.0f || image_size.y <= 0.0f) {
    /* Drawing-mode toggled off (or the widget collapsed) mid-drag —
     * drop the in-progress preview so it does not silently re-appear
     * the next time drawing-mode is enabled. */
    _paint_cancel_active_drag (application);
    return;
  }
  if (_paint_ensure_annotation_input (application) != PULSE_SUCCESS)
    return;

  /* Cover the freshly-drawn image with an invisible hit-target. */
  ImGui::SetCursorScreenPos (image_origin);
  ImGui::InvisibleButton ("##paint_overlay", image_size, ImGuiButtonFlags_MouseButtonLeft);
  bool hovered = ImGui::IsItemHovered ();
  bool active = ImGui::IsItemActive (); /* mouse button held over us */

  ImVec2 mouse = ImGui::GetIO ().MousePos;
  ImVec2 local = ImVec2 (mouse.x - image_origin.x, mouse.y - image_origin.y);
  ImVec2 canvas = _paint_screen_to_canvas (application, local, image_size);

  /* Begin a new stroke on press. */
  if (active && !p.active_drag && hovered && ImGui::IsMouseClicked (ImGuiMouseButton_Left)) {
    p.active_drag = true;
    p.drag_anchor_screen = mouse;
    p.drag_anchor_canvas = canvas;
    p.live_points_screen.clear ();
    p.live_points_canvas.clear ();
    p.live_points_screen.push_back (mouse);
    p.live_points_canvas.push_back (canvas);
  }

  if (p.active_drag && active) {
    if (p.tool == PexNinjaState::PexNinjaPaint::kPencil) {
      /* Pencil: append every fresh sample. */
      if (p.live_points_screen.empty () || p.live_points_screen.back ().x != mouse.x ||
          p.live_points_screen.back ().y != mouse.y) {
        p.live_points_screen.push_back (mouse);
        p.live_points_canvas.push_back (canvas);
      }
    } else {
      /* Rubber-band tools (line, rectangle, ellipse): regenerate the
       * point list from the press position to the current cursor on
       * every move. */
      _paint_rebuild_rubberband (p, mouse, canvas);
    }
  }

  /* Commit on release. */
  if (p.active_drag && ImGui::IsMouseReleased (ImGuiMouseButton_Left)) {
    _paint_commit_active_stroke (application);
  }

  /* Render the preview. We draw onto the *window* draw list with a
   * clip rect equal to the image bounds so we never spill over the
   * widget edges. */
  if (!p.live_points_screen.empty ()) {
    ImDrawList * dl = ImGui::GetWindowDrawList ();
    ImU32 col = ImGui::GetColorU32 (p.color);
    /* Convert the canvas-space thickness to screen pixels so the
     * preview matches what the gfx pipeline will produce. */
    float scale = image_size.x / (float)std::max (1, p.canvas_width);
    float screen_thickness = std::max (1.0f, (float)p.thickness * scale);

    dl->PushClipRect (image_origin, ImVec2 (image_origin.x + image_size.x, image_origin.y + image_size.y), true);
    if (p.live_points_screen.size () == 1) {
      dl->AddCircleFilled (p.live_points_screen[0], screen_thickness * 0.5f, col);
    } else {
      dl->AddPolyline (p.live_points_screen.data (), (int)p.live_points_screen.size (), col, ImDrawFlags_None,
                       screen_thickness);
    }
    dl->PopClipRect ();
  }
}

static inline void
configure_window_paint_tools (PexNinja * application)
{
  auto & p = application->state.paint;

  ImGui::SetNextWindowSize (ImVec2 (260, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin ("Paint Tools", &application->state.windows.show_paint_tools, ImGuiWindowFlags_AlwaysAutoResize);

  ImGui::Checkbox ("Drawing mode", &p.drawing_mode);
  ImGui::SameLine ();
  ImGui::TextDisabled ("(intercepts mouse over self-view)");

  ImGui::Separator ();

  ImGui::ColorEdit4 ("Colour", (float *)&p.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
  ImGui::SliderInt ("Thickness", &p.thickness, 1, 32, "%d px");

  ImGui::Separator ();

  ImGui::TextUnformatted ("Tool:");
  ImGui::SameLine ();
  ImGui::RadioButton ("Pencil", &p.tool, (int)PexNinjaState::PexNinjaPaint::kPencil);
  ImGui::SameLine ();
  ImGui::RadioButton ("Line", &p.tool, (int)PexNinjaState::PexNinjaPaint::kLine);
  ImGui::SameLine ();
  ImGui::RadioButton ("Rect", &p.tool, (int)PexNinjaState::PexNinjaPaint::kRectangle);
  ImGui::SameLine ();
  ImGui::RadioButton ("Ellipse", &p.tool, (int)PexNinjaState::PexNinjaPaint::kEllipse);

  ImGui::Separator ();

  auto & vm = application->state.video_mix;
  /* Buttons are visible at all times; if no gfx input has been
   * acquired yet the Pulse calls return INVALID_PARAMETER which is
   * harmless. */
  if (ImGui::Button ("Undo"))
    pulse_annotation_undo (application->client, vm.annotation_input);
  ImGui::SameLine ();
  if (ImGui::Button ("Redo"))
    pulse_annotation_redo (application->client, vm.annotation_input);
  ImGui::SameLine ();
  if (ImGui::Button ("Clear"))
    pulse_annotation_clear (application->client, vm.annotation_input);

  ImGui::TextDisabled ("Canvas: %d x %d", p.canvas_width, p.canvas_height);
  if (vm.annotation_input == PULSE_VIDEO_MIX_INPUT_ID_NONE)
    ImGui::TextDisabled ("(annotation input not yet acquired)");

  ImGui::End ();
}
static void
_set_background_replacement (PexNinja * application, PexNinjaState::PexNinjaVideoMix & vm, bool enable)
{
  if (enable)
    vm.camera_layout.videoproc_mask = (PulseVideoProcessTypeMask)(PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION);
  else
    vm.camera_layout.videoproc_mask = PULSE_VIDEO_PROCESS_TYPE_NONE;

  _reconnect_video_mix (application, vm);
}
static void
bg_replacement_on_mix (PexNinja * application)
{
  /* first, we check video_mix for main video */
  auto & vm = application->state.video_mix;
  if (application->config.options.enable_bg_replacement) {
    /* default background is the desktop display */
    if (vm.desktop_input == PULSE_VIDEO_MIX_INPUT_ID_NONE) {
      PulseError err = pulse_video_mix_input_from_desktop (application->client, 0, PULSE_DISPLAY, &vm.desktop_input);
      if (err != PULSE_SUCCESS) {
        PEX_LOG_WARNING ("Failed to acquire desktop input for background replacement: %s", pulse_strerror (err));
        /* Do not enable or restart the mix when we have no valid desktop input */
        return;
      }
      vm.desktop_input_kind = PexNinjaState::PexNinjaVideoMix::kBackgroundDesktop;
    }
    _set_background_replacement (application, vm, true);
    // If not presenting, start the main video mix now with bg replacement enabled.
    if (!application->state.presenting && !vm.active) {
      pexninja_start_video_mix (application, vm, PULSE_MEDIA_CONTENT_MAIN);
    }
  } else {
    /* we just disable bg replacement on a mix that's already running */
    _set_background_replacement (application, vm, false);
  }
  /* we then check bg replacement on the presentation if it is running. We do NOT
  start presentation if it's not active */
  auto & pm = application->state.preso_mix;
  _set_background_replacement (application, pm, application->config.options.enable_bg_replacement);
}

static inline void
configure_menu_settings (PexNinja * application, GLTextureContext * preflight_ctx)
{
  bool show_config = application->state.windows.show_config;
  ImGui::Begin ("Settings", &show_config, ImGuiWindowFlags_AlwaysAutoResize);
  set_show_settings (application, show_config);

  if (ImGui::CollapsingHeader ("Device settings", ImGuiTreeNodeFlags_DefaultOpen)) {
    render_gl_ctx_image (application->client, preflight_ctx);
    render_device_selection (application);
  }

  if (ImGui::CollapsingHeader ("Video options", ImGuiTreeNodeFlags_None)) {
    if (ImGui::Checkbox ("Blur", &application->config.options.enable_blur)) {
      pulse_options_set_background_blur (application->client, application->config.options.enable_blur);
    }
    ImGui::SameLine ();
    if (ImGui::Checkbox ("Background Replacement", &application->config.options.enable_bg_replacement)) {
      bg_replacement_on_mix (application);
    }
    ImGui::Separator ();
    if (ImGui::Checkbox ("Scrambler", &application->config.options.enable_scrambler)) {
      pulse_options_set_video_scrambling (application->client, application->config.options.enable_scrambler);
    }
    ImGui::Separator ();
    if (ImGui::Checkbox ("enable FECC/PTZ support", &application->config.options.enable_fecc_support)) {
      _write_config_if_needed (application);
      pulse_options_set_fecc_mode (application->client, application->config.options.enable_fecc_support);
    }

#if defined(HOST_LINUX)
    if (ImGui::Checkbox ("use pulse internal PTZ", &application->config.options.use_pulse_internal_ptz)) {
      _write_config_if_needed (application);
    }
#else
    /* Leave it visible, but disabled */
    imgui_begin_disabled_state ();
    if (ImGui::Checkbox ("use pulse internal PTZ", &application->config.options.use_pulse_internal_ptz)) {
      _write_config_if_needed (application);
    }
    imgui_end_disabled_state ();
#endif
  }
  if (ImGui::CollapsingHeader ("Audio options", ImGuiTreeNodeFlags_None)) {
    if (ImGui::Checkbox ("AGC", &application->config.options.enable_agc)) {
      pulse_options_set_automatic_gain_control (application->client, application->config.options.enable_agc);
    }
    ImGui::SameLine ();
    if (ImGui::Checkbox ("Denoise", &application->config.options.enable_denoise)) {
      pulse_options_set_denoise (application->client, application->config.options.enable_denoise);
    }
    ImGui::Separator ();
    if (ImGui::Checkbox ("Mute audio on startup", &application->config.options.mute_audio_on_startup)) {
      _write_config_if_needed (application);
    }
  }

  if (ImGui::CollapsingHeader ("Network settings", ImGuiTreeNodeFlags_None)) {
    network_menu (application->client);

    if (ImGui::CollapsingHeader ("Resolving options", ImGuiTreeNodeFlags_None)) {
      ImGui::Text ("Resolving options:");

      if (ImGui::Checkbox ("allow direct FQDN connection (no SRV)",
                           &application->config.options.allow_direct_fqdn_connection)) {
        _write_config_if_needed (application);
        pulse_options_set_allow_direct_fqdn_connect (application->client,
                                                     application->config.options.allow_direct_fqdn_connection);
      }

      if (ImGui::Checkbox ("allow direct ip connection (no SRV)",
                           &application->config.options.allow_direct_ip_connection)) {
        _write_config_if_needed (application);
        pulse_options_set_allow_direct_ip_connect (application->client,
                                                   application->config.options.allow_direct_ip_connection);
      }
    }

    if (ImGui::CollapsingHeader ("stun/turn settings", ImGuiTreeNodeFlags_None)) {
      ImGui::Text ("stun/turn settings:");
      if (ImGui::Checkbox ("disable STUN server support", &application->config.options.disable_stun_server_support)) {
        _write_config_if_needed (application);
        pulse_options_set_stun_server_support (application->client,
                                               !application->config.options.disable_stun_server_support);
      }
      if (ImGui::Checkbox ("disable TURN server support", &application->config.options.disable_turn_server_support)) {
        _write_config_if_needed (application);
        pulse_options_set_turn_server_support (application->client,
                                               !application->config.options.disable_turn_server_support);
      }

      if (application->config.options.disable_turn_server_support) {
        imgui_begin_disabled_state ();
      }
      if (ImGui::Checkbox ("disable TURN_443 server support",
                           &application->config.options.disable_turn_443_server_support)) {
        _write_config_if_needed (application);
        pulse_options_set_turn_443_server_support (application->client,
                                                   !application->config.options.disable_turn_443_server_support);
      }
      if (application->config.options.disable_turn_server_support) {
        imgui_end_disabled_state ();
      }
    }

    if (ImGui::CollapsingHeader ("TLS settings", ImGuiTreeNodeFlags_None)) {
      ImGui::Text ("TLS settings:");
      if (ImGui::Checkbox ("disable TLS hostname verification",
                           &application->config.options.disable_tls_hostname_verification)) {
        _write_config_if_needed (application);
        pulse_options_set_tls_hostname_verification (application->client,
                                                     !application->config.options.disable_tls_hostname_verification);
      }
      if (ImGui::Checkbox ("disable TLS peer verification",
                           &application->config.options.disable_tls_peer_verification)) {
        _write_config_if_needed (application);
        pulse_options_set_tls_peer_verification (application->client,
                                                 !application->config.options.disable_tls_peer_verification);
      }
    }

    if (ImGui::CollapsingHeader ("Proxy settings", ImGuiTreeNodeFlags_None)) {
      ImGui::Text ("Proxy settings:");
      if (ImGui::Checkbox ("enable proxy server", &application->config.options.enable_proxy_server)) {
        _write_config_if_needed (application);
      }
      if (application->config.options.enable_proxy_server) {
        ImGui::Text ("(applies to HTTP/HTTPS communication only)");
        if (ImGui::BeginCombo ("Proxy type",
                               pulse_type_mapping_proxy_server_type_to_string (application->config.options.proxy_type),
                               ImGuiComboFlags_None)) {
          for (int n = PULSE_PROXY_SERVER_HTTP; n <= PULSE_PROXY_SERVER_SOCKS5H; n++) {
            bool is_selected = (application->config.options.proxy_type == n);
            if (ImGui::Selectable (pulse_type_mapping_proxy_server_type_to_string ((PulseProxyServerType)n),
                                   is_selected)) {
              application->config.options.proxy_type = (PulseProxyServerType)n;
              _write_config_if_needed (application);
            }
            if (is_selected)
              ImGui::SetItemDefaultFocus ();
          }
          ImGui::EndCombo ();
        }
        if (ImGui::InputText ("server", application->config.options.proxy_server, 256)) {
          _write_config_if_needed (application);
        }
        int port = application->config.options.proxy_port;
        if (ImGui::InputInt ("port", &port)) {
          if (port < 0)
            port = 0;
          if (port > 65535)
            port = 65535;
          application->config.options.proxy_port = (uint16_t)port;
        }
        if (application->config.options.proxy_type == PULSE_PROXY_SERVER_HTTP ||
            application->config.options.proxy_type == PULSE_PROXY_SERVER_HTTP_1_0 ||
            application->config.options.proxy_type == PULSE_PROXY_SERVER_HTTPS) {
          if (ImGui::Checkbox ("use proxy server authentication",
                               &application->config.options.enable_proxy_server_authentication)) {
            _write_config_if_needed (application);
          }
          if (application->config.options.enable_proxy_server_authentication) {
            if (ImGui::InputText ("username", application->config.options.proxy_username, 256)) {
              _write_config_if_needed (application);
            }
            if (ImGui::InputText ("password", application->config.options.proxy_password, 256)) {
              _write_config_if_needed (application);
            }
          }
        } else {
          application->config.options.enable_proxy_server_authentication = false;
        }
      }
    }
  }
  ImGui::End ();
}

static inline void
configure_menu_registration (PexNinja * application)
{
  bool register_clicked = false;

  ImGui::SetNextWindowSize (ImVec2 (300, 200), ImGuiCond_FirstUseEver);
  ImGui::Begin ("Registration", &application->state.windows.show_registration, ImGuiWindowFlags_NoResize);
  ImGui::InputText ("alias", application->config.registration.alias, 256);
  ImGui::InputText ("host", application->config.registration.host, 256);
  ImGui::Checkbox ("Authenticate with SSO", &application->config.registration.use_sso);
  if (application->config.registration.use_sso == false) {
    ImGui::InputText ("username", application->config.registration.username, 256);
    ImGui::InputText ("password", application->config.registration.password, 128);
  }
  ImGui::Checkbox ("automatically register on startup", &application->config.registration.enabled);
  register_clicked = ImGui::Button ("Register");
  ImGui::End ();

  if (register_clicked || application->state.windows.show_registration == false) {
    _write_config_if_needed (application);
  }

  if (register_clicked) {
    if (application->state.registered == false) {
      pulseimgui_perform_registration (application);
    }
  }
}

static bool
is_ready_to_connect (PexNinja * application, const char * lookup_conference_alias, const char * lookup_device_alias)
{
  if (strlen (application->config.connection.displayName) == 0) {
    return false;
  }

  switch (application->state.connection_setup_type) {
    case 0:
      // Lookup conference alias
      return strlen (lookup_conference_alias) > 0;
    case 1:
      // Lookup device alias
      return strlen (lookup_device_alias) > 0;
    default:
      return (strlen (application->config.connection.server) > 0 &&
              strlen (application->config.connection.conference) > 0);
  }

  return false;
}

static inline void
configure_menu_connection (PexNinja * application)
{
  bool connect_clicked = false;
  bool pressed_enter = false;
  bool ready_to_connect = false;
  static char lookup_conference_alias[1024];
  static char lookup_device_alias[1024];

  ImGui::SetNextWindowSize (ImVec2 (400, 0), ImGuiCond_Always);
  ImGui::Begin ("Connection", &application->state.windows.show_connection, ImGuiWindowFlags_NoResize);

  pressed_enter |= ImGui::InputTextWithHint ("display_name", "Your name", application->config.connection.displayName,
                                             256, ImGuiInputTextFlags_EnterReturnsTrue);

  if (application->state.registered) {
    ImGui::RadioButton ("Lookup conference", &application->state.connection_setup_type, 0);
    ImGui::SameLine ();
    ImGui::RadioButton ("Lookup device", &application->state.connection_setup_type, 1);
    ImGui::SameLine ();
    ImGui::RadioButton ("Manual setup", &application->state.connection_setup_type, 2);
  } else {
    application->state.connection_setup_type = 2;
  }

  if (application->state.registered && application->state.connection_setup_type == 0) {
    pressed_enter |= ImGui::InputTextWithHint ("conference", "Conference alias", lookup_conference_alias, 256,
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackEdit,
                                               _update_conference_alias_list_cb, application);
    const bool is_input_text_active = ImGui::IsItemActive ();
    const bool is_input_text_activated = ImGui::IsItemActivated ();

    if (is_input_text_activated)
      ImGui::OpenPopup ("##popup_lookup_conference");

    if (application->state.conference_alias_list && application->state.conference_alias_list->size > 0) {
      ImGui::SetNextWindowPos (ImVec2 (ImGui::GetItemRectMin ().x, ImGui::GetItemRectMax ().y));
      ImGui::SetNextWindowSizeConstraints (ImVec2 (0, 0), ImVec2 (1000, 300));
      if (ImGui::BeginPopup ("##popup_lookup_conference", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_ChildWindow)) {
        for (size_t i = 0; i < application->state.conference_alias_list->size; i++) {
          char buf[1024];
          snprintf (buf, sizeof (buf), "%s (%s)", application->state.conference_alias_list->list[i]->alias,
                    strlen (application->state.conference_alias_list->list[i]->description) > 0
                      ? application->state.conference_alias_list->list[i]->description
                      : "no description");
          if (ImGui::Selectable (buf)) {
            ImGui::ClearActiveID ();
            strncpy (lookup_conference_alias, application->state.conference_alias_list->list[i]->alias, 1023);
          }
        }

        if (ImGui::IsKeyPressed (ImGuiKey_Enter) ||
            ImGui::IsKeyPressed (ImGuiKey_KeypadEnter)) {
          if (application->state.conference_alias_list->size > 0) {
            strncpy (lookup_conference_alias, application->state.conference_alias_list->list[0]->alias, 1023);
          }
          ImGui::CloseCurrentPopup ();
        }

        if (pressed_enter || (!is_input_text_active && !ImGui::IsWindowFocused ()))
          ImGui::CloseCurrentPopup ();

        ImGui::EndPopup ();
      }
    }
  } else if (application->state.registered && application->state.connection_setup_type == 1) {
    pressed_enter |= ImGui::InputTextWithHint ("device", "Device alias", lookup_device_alias, 256,
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackEdit,
                                               _update_device_alias_list_cb, application);
    const bool is_input_text_active = ImGui::IsItemActive ();
    const bool is_input_text_activated = ImGui::IsItemActivated ();

    if (is_input_text_activated)
      ImGui::OpenPopup ("##popup_lookup_device");

    if (application->state.device_alias_list && application->state.device_alias_list->size > 0) {
      ImGui::SetNextWindowPos (ImVec2 (ImGui::GetItemRectMin ().x, ImGui::GetItemRectMax ().y));
      ImGui::SetNextWindowSizeConstraints (ImVec2 (0, 0), ImVec2 (1000, 300));
      if (ImGui::BeginPopup ("##popup_lookup_device", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_ChildWindow)) {
        for (size_t i = 0; i < application->state.device_alias_list->size; i++) {
          char buf[1024];
          snprintf (buf, sizeof (buf), "%s (%s)", application->state.device_alias_list->list[i]->alias,
                    strlen (application->state.device_alias_list->list[i]->description) > 0
                      ? application->state.device_alias_list->list[i]->description
                      : "no description");
          if (ImGui::Selectable (buf)) {
            ImGui::ClearActiveID ();
            strncpy (lookup_device_alias, application->state.device_alias_list->list[i]->alias, 1023);
          }
        }

        if (ImGui::IsKeyPressed (ImGuiKey_Enter) ||
            ImGui::IsKeyPressed (ImGuiKey_KeypadEnter)) {
          if (application->state.device_alias_list->size > 0) {
            strncpy (lookup_device_alias, application->state.device_alias_list->list[0]->alias, 1023);
          }
          ImGui::CloseCurrentPopup ();
        }

        if (pressed_enter || (!is_input_text_active && !ImGui::IsWindowFocused ()))
          ImGui::CloseCurrentPopup ();

        ImGui::EndPopup ();
      }
    }

  } else {
    pressed_enter |=
      ImGui::InputTextWithHint ("conference", "Conference alias", application->config.connection.conference, 256,
                                ImGuiInputTextFlags_EnterReturnsTrue);
    pressed_enter |=
      ImGui::InputTextWithHint ("server", "Hostname or IP address", application->config.connection.server, 256,
                                ImGuiInputTextFlags_EnterReturnsTrue);

    pressed_enter |= ImGui::InputTextWithHint ("pin", "Pin code or leave blank", application->config.connection.pin, 12,
                                               ImGuiInputTextFlags_EnterReturnsTrue);
  }

  if (application->state.async_op.op == ASYNC_OP_NONE) {
    ready_to_connect = is_ready_to_connect (application, lookup_conference_alias, lookup_device_alias);

    if (!ready_to_connect)
      imgui_begin_disabled_state ();

    connect_clicked = ImGui::Button ("Connect");

    if (!ready_to_connect)
      imgui_end_disabled_state ();
  }

  ImGui::Separator ();
  ImGui::Text ("Options:");
  if (ImGui::Checkbox ("Enable direct-media support", &application->config.options.enable_direct_media_support)) {
    pulse_options_set_direct_media_supported (application->client,
                                              application->config.options.enable_direct_media_support);
  }
  ImGui::End ();

  if (connect_clicked || (pressed_enter && ready_to_connect) || application->state.windows.show_connection == false) {
    _write_config_if_needed (application);
  }

  if (connect_clicked || (pressed_enter && ready_to_connect)) {
    application->state.error_msg = NULL;
    application->state.async_op = ASYNC_OP_DATA_INIT;
    if (application->state.conn_status == PULSE_CONNECTION_STATUS_DISCONNECTED) {
      _configure_proxy_server (application);

      std::string conference_alias;
      std::string conference_server;
      const char * conference_pin = NULL;
      if (application->state.registered && application->state.connection_setup_type == 0) {
        conference_alias = std::string (lookup_conference_alias);

        std::size_t found = conference_alias.find_last_of ("@");
        if (found != std::string::npos) {
          conference_server = conference_alias.substr (found + 1);
        } else {
          conference_server = std::string (application->config.registration.host);
          conference_alias.append ("@");
          conference_alias.append (application->config.registration.host);
        }
      } else if (application->state.registered && application->state.connection_setup_type == 1) {
        conference_alias = std::string (lookup_device_alias);

        std::size_t found = conference_alias.find_last_of ("@");
        if (found != std::string::npos) {
          conference_server = conference_alias.substr (found + 1);
        } else {
          conference_server = std::string (application->config.registration.host);
          conference_alias.append ("@");
          conference_alias.append (application->config.registration.host);
        }
      } else {
        conference_server = std::string (application->config.connection.server);
        conference_alias = std::string (application->config.connection.conference);
        conference_pin = application->config.connection.pin;
      }

      PulseRestConnectionConfig cfg;
      memset (&cfg, 0, sizeof (PulseRestConnectionConfig));
      cfg.server_address = conference_server.c_str ();
      cfg.conference_name = conference_alias.c_str ();
      cfg.display_name = application->config.connection.displayName;
      cfg.pin_code = conference_pin;

      PulseAsyncOperationResultCallbackConfig async_op_cb_config = {.func = _pulse_async_operation_result_cb,
                                                                    .user_context = &application->state.async_op};
      PulseOperationProgressCallbackConfig progress_config = {.func = _progress_callback_conference,
                                                              .user_context = application};

      PulseError err = pulse_connect_with_rest_async (application->client, &cfg, &async_op_cb_config, &progress_config);
      if (err != PULSE_SUCCESS) {
        PEX_LOG_DEBUG ("pulse_connect_with_rest failed: %s\n", pulse_strerror (err));
        _update_conference_status_msg (application, "Failed to connect");
        if (err != PULSE_ERROR_PROCESS_ABORTED)
          application->state.error_msg = pulse_strerror (err);
      } else {
        application->state.async_op.op = ASYNC_OP_CONNECT;
        application->state.windows.show_connection = false;
      }
    }
    // lookup_conference_alias[0] = '\0';
  }
}

static void
pexninja_set_presenting (PexNinja * application, bool presenting)
{
  if (presenting != application->state.presenting) {
    application->state.presenting = presenting;

    if (presenting)
      pulse_participant_control_take_floor (application->client, NULL);
    else
      pulse_participant_control_release_floor (application->client, NULL);
  }
}

static void
pexninja_set_sharing_desktop_audio (PexNinja * application, bool sharing_desktop_audio)
{
  if (application->state.sharing_desktop_audio != sharing_desktop_audio) {
    PulseError err = sharing_desktop_audio ? pulse_device_session_connect_desktop_audio_input (application->client, 0)
                                           : pulse_device_session_disconnect_desktop_audio_input (application->client);

    if (err != PULSE_SUCCESS) {
      PEX_LOG_WARNING ("%s desktop-audio failed: %s", sharing_desktop_audio ? "Enabling" : "Disabling",
                   pulse_strerror (err));
    }

    application->state.sharing_desktop_audio = sharing_desktop_audio;
  }
}
static void
_start_presentation_mix_session (PexNinja * application,
                                 PulseMediaContent mix_media_content = PULSE_MEDIA_CONTENT_PRESENTATION)
{
  auto & pm = application->state.preso_mix;

  // Set the presentation-specific layout (camera as small PiP in lower right corner)
  pm.desktop_layout = {0, 1.0, 1.0, 0.5, 0.5, PULSE_VIDEO_PROCESS_TYPE_NONE};
  pm.camera_layout = {1, 0.25, 0.25, 1.0, 1.0, PULSE_VIDEO_PROCESS_TYPE_NONE};

  /* if bg replacement is enabled already, make sure the new config has that turned on */
  if (application->config.options.enable_bg_replacement)
    pm.camera_layout.videoproc_mask = (PulseVideoProcessTypeMask)(PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION);

  pexninja_start_video_mix (application, pm, mix_media_content);
}
static void
_stop_presentation_mix_session (PexNinja * application)
{
  auto & pm = application->state.preso_mix;
  if (!pm.active)
    return;
  pulse_video_mix_disconnect (application->client, pm.media_content);
  if (pm.desktop_input != PULSE_VIDEO_MIX_INPUT_ID_NONE) {
    pulse_video_mix_input_release (application->client, pm.desktop_input);
    pm.desktop_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
    pm.desktop_input_kind = PexNinjaState::PexNinjaVideoMix::kBackgroundNone;
  }
  /* we keep the camera input ID! */
  PEX_LOG_DEBUG ("Stopping presentation mix session. Camera input has ID: %u", pm.camera_input);

  pm.active = false;
}

static void
configure_presentation_displays (PexNinja * application)
{
  std::vector<PexNinjaDisplayHandle> handles = enumerate_displays ();

  if (handles.empty ()) {
    ImGui::Text ("No available displays to share");
  } else {
    ImGui::Text ("Click on a display item to start sharing");
  }

  for (auto handle : handles) {
    auto name = get_display_name (handle);

    if (name.size () == 0)
      continue;

    if (ImGui::MenuItem (name.c_str (), NULL)) {
      auto & pm = application->state.preso_mix;
      if (application->state.presenting) {
        /* stop video mix that uses dekstop if active */
        _stop_presentation_mix_session (application);
        /* disconnect any-previous one */
        pexninja_set_presenting (application, false);
      }

      PulseError err = pulse_video_mix_input_from_desktop (application->client, (uint64_t)(uintptr_t)handle,
                                                           PULSE_DISPLAY, &pm.desktop_input);
      if (err != PULSE_SUCCESS) {
        PEX_LOG_WARNING ("Connecting desktop-video input failed: %s", pulse_strerror (err));
      } else {
        pm.desktop_input_kind = PexNinjaState::PexNinjaVideoMix::kBackgroundDesktop;
        pexninja_set_presenting (application, true);
        _start_presentation_mix_session (application);
      }
    }
  }
}

static void
configure_presentation_windows (PexNinja * application)
{
  std::vector<PexNinjaWindowHandle> handles = enumerate_desktop_windows ();

  if (handles.empty ()) {
    ImGui::Text ("No available windows to share");
  } else {
    ImGui::Text ("Click on a window item to start sharing");
  }

  for (auto handle : handles) {
    auto windowName = get_window_handle_name (handle);

    if (windowName.size () == 0)
      continue;

    if (ImGui::MenuItem (windowName.c_str (), NULL)) {
      PEX_LOG_DEBUG ("configure_presentation_windows: MenuItem clicked for handle=%zu presenting=%s", (uintptr_t)handle,
                 application->state.presenting ? "true" : "false");
      auto & pm = application->state.preso_mix;
      if (application->state.presenting) {
        /* stop video mix if active */
        _stop_presentation_mix_session (application);
        /* disconnect any-previous one */
        pexninja_set_presenting (application, false);
      }

      PulseError err =
        pulse_video_mix_input_from_desktop (application->client, (uint64_t)handle, PULSE_WINDOW, &pm.desktop_input);
      if (err != PULSE_SUCCESS) {
        PEX_LOG_WARNING ("Connecting desktop-video input failed: %s", pulse_strerror (err));
      } else {
        pm.desktop_input_kind = PexNinjaState::PexNinjaVideoMix::kBackgroundWindow;
        pexninja_set_presenting (application, true);
        _start_presentation_mix_session (application);
      }
    }
  }
}

static inline void
configure_menu_presentation_control (PexNinja * application)
{
  if (application->state.async_op.op != ASYNC_OP_NONE ||
      application->state.conn_status != PULSE_CONNECTION_STATUS_CONNECTED) {
    return;
  }

  if (ImGui::BeginMenu ("Presentation")) {

#if 0 // Re-enable "streamer-mode" at some point 
      if (ImGui::MenuItem ("Streamer Mode", NULL)) {
        pulse_start_streaming_mode (application->client);
        application->state.streaming = true;
      }
      if (ImGui::MenuItem ("Streamer Audio Mode", NULL)) {
        pulse_start_audio_streaming_mode (application->client);
        application->state.streaming = true;
      }
#endif
    if (!application->state.sharing_desktop_audio) {
      if (ImGui::MenuItem ("Share desktop-audio", NULL))
        pexninja_set_sharing_desktop_audio (application, true);
    } else {
      if (ImGui::MenuItem ("Stop sharing desktop-audio", NULL))
        pexninja_set_sharing_desktop_audio (application, false);
    }

    ImGui::Separator ();

    if (application->state.presenting) {
      if (ImGui::MenuItem ("Stop sharing", NULL)) {
        /* stop video mix if active */
        _stop_presentation_mix_session (application);
        pexninja_set_presenting (application, false);

        /*If background replacement is still enabled, activate the main video mix so that the toggle continues to
         * work.*/
        auto & vm = application->state.video_mix;
        if (application->config.options.enable_bg_replacement && !vm.active) {
          pexninja_start_video_mix (application, vm, PULSE_MEDIA_CONTENT_MAIN);
        }
      }
      ImGui::Separator ();
    }

    configure_presentation_displays (application);
    ImGui::Separator ();
    configure_presentation_windows (application);
    ImGui::EndMenu ();
  }

#if 0 // Re-enable "streamer-mode" at some point 
  if (!application->state.presenting) {

  } else {
    if (ImGui::MenuItem ("Stop Presenting", NULL)) {
      pexninja_set_presenting (application, false);
      auto &pm = application->state.preso_mix;
       /* stop video mix if active */
      _stop_presentation_mix_session (application);
      pulse_device_session_disconnect_desktop_video_input (application->client);
     

    }
  }
#endif
}

static inline void
configure_menu_change_layout (PexNinja * application)
{
  if (ImGui::BeginMenu ("Change layout")) {
    static PulseConferenceControlTransformLayoutRequest req_buf;
    PulseConferenceControlTransformLayoutRequest * req = &req_buf;

    bool change = false;

    if (application->state.available_layouts) {
      for (size_t i = 0; i < application->state.available_layouts->list_size; i++) {
        assert (application->state.available_layouts->list[i]);
        if (ImGui::MenuItem (application->state.available_layouts->list[i], NULL,
                             (pex_strcmp0 (application->state.layout.layout.c_str (),
                                         application->state.available_layouts->list[i]) == 0),
                             true)) {
          application->state.layout.layout = std::string (application->state.available_layouts->list[i]);
          req_buf.layout = application->state.available_layouts->list[i];
          change = true;
        }
      }
    }

    if (ImGui::BeginMenu ("Layout options")) {
      if (ImGui::Checkbox ("Enable Extended AC", &application->state.layout.enable_extended_ac)) {
        req_buf.enable_extended_ac = &application->state.layout.enable_extended_ac;
        change = true;
      }

      if (ImGui::Checkbox ("Show streaming indicator", &application->state.layout.streaming_indicator)) {
        req_buf.streaming_indicator = &application->state.layout.streaming_indicator;
        change = true;
      }

      if (ImGui::Checkbox ("Show recording indicator", &application->state.layout.recording_indicator)) {
        req_buf.recording_indicator = &application->state.layout.recording_indicator;
        change = true;
      }

      if (ImGui::Checkbox ("Show transcribing indicator", &application->state.layout.transcribing_indicator)) {
        req_buf.transcribing_indicator = &application->state.layout.transcribing_indicator;
        change = true;
      }

      if (ImGui::Checkbox ("Enable active speaker indication",
                           &application->state.layout.enable_active_speaker_indication)) {
        req_buf.enable_active_speaker_indication = &application->state.layout.enable_active_speaker_indication;
        change = true;
      }

      if (ImGui::Checkbox ("Enable overlay text", &application->state.layout.enable_overlay_text)) {
        req_buf.enable_overlay_text = &application->state.layout.enable_overlay_text;
        change = true;
      }

      if (ImGui::Checkbox ("Enable plus n indicator", &application->state.layout.plus_n_pip_enabled)) {
        req_buf.plus_n_pip_enabled = &application->state.layout.plus_n_pip_enabled;
        change = true;
      }

      ImGui::EndMenu ();
    }

    if (ImGui::MenuItem ("Reset to defaults", NULL)) {
      memset (req, 0, sizeof (PulseConferenceControlTransformLayoutRequest));
      application->state.layout.enable_extended_ac = false;
      application->state.layout.streaming_indicator = false;
      application->state.layout.recording_indicator = false;
      application->state.layout.transcribing_indicator = false;
      application->state.layout.enable_active_speaker_indication = false;
      application->state.layout.enable_overlay_text = false;
      application->state.layout.plus_n_pip_enabled = false;
      req = NULL;
      change = true;
    }

    if (change) {
      pulse_conference_control_transform_layout (application->client, req);
    }
    ImGui::EndMenu ();
  }
}

static inline void
configure_menu_breakout_rooms_guest_unlocked (PexNinja * application)
{
  // The application->room_list.mutex should be held when entering this function!

  if (application->room_list.current_room->conference_status.breakout_rooms.enabled) {
    if (ImGui::BeginMenu ("Breakout rooms")) {
      if (ImGui::MenuItem (application->room_list.current_room->conference_status.breakout_rooms.buzz
                             ? "Cancel request for help"
                             : "Request help",
                           NULL)) {
        if ((application->room_list.current_room->conference_status.breakout_rooms.buzz =
               !application->room_list.current_room->conference_status.breakout_rooms.buzz))
          pulse_conference_control_breakouts_buzz (application->client);
        else
          pulse_conference_control_breakouts_clearbuzz (application->client);
      }
      if (application->room_list.current_room->conference_status.breakout_rooms.guests_allowed_to_leave) {
        if (ImGui::MenuItem ("Leave breakout", NULL)) {
          pulse_conference_control_breakouts_leavebreakout (application->client);
        }
      }
      ImGui::EndMenu ();
    }
  }
}

static inline void
configure_menu_breakout_rooms_host_unlocked (PexNinja * application)
{
  // The application->room_list.mutex should be held when entering this function!
  if (application->room_list.current_room->conference_status.breakout_rooms.enabled) {
    if (ImGui::BeginMenu ("Breakout rooms")) {
      if (ImGui::MenuItem (application->room_list.current_room->conference_status.breakout_rooms.buzz
                             ? "Cancel request for help"
                             : "Request help",
                           NULL)) {
        if ((application->room_list.current_room->conference_status.breakout_rooms.buzz =
               !application->room_list.current_room->conference_status.breakout_rooms.buzz))
          pulse_conference_control_breakouts_buzz (application->client);
        else
          pulse_conference_control_breakouts_clearbuzz (application->client);
      }
      ImGui::EndMenu ();
    }
  }

  if (application->room_list.room_map[0]->conference_status.breakout_rooms_supported) {
    if (ImGui::BeginMenu ("Breakout room controls")) {
      if (ImGui::MenuItem ("Open breakout room", NULL)) {
        application->state.breakout_rooms.popup_new_flagged = true;
      }

      if (ImGui::MenuItem ("Move participants", NULL)) {
        application->state.breakout_rooms.popup_participant_move_flagged = true;
      }

      if (ImGui::BeginMenu ("Close brekaout room")) {
        for (auto it = application->room_list.room_map.begin (); it != application->room_list.room_map.end (); it++) {
          if (it->first == PULSE_ROOM_ID_MAIN)
            continue;
          if (ImGui::MenuItem (it->second->conference_status.breakout_rooms.name.c_str (), NULL)) {
            pulse_conference_control_breakout_close (application->client, it->first);
          }
        }
        ImGui::EndMenu ();
      }

      if (ImGui::MenuItem ("Empty breakouts", NULL)) {
        pulse_conference_control_breakouts_empty (application->client);
      }
      ImGui::EndMenu ();
    }
  }
}

/* Standalone "RTMP Server" window — the place to configure all
 * listener settings (path, port, TLS, auth) for the shared RTMP
 * listener. The Pulse RTMP API today exposes one path per listener,
 * so `path` lives here as a single server-wide value rather than
 * per-source.
 *
 * Connect / Disconnect manage the same shared listener that the
 * Compositor lazy-starts on demand. If the user opens this window
 * and presses Connect first, the listener stays up across source
 * lifecycles (lazy_started == false); if the Compositor brought it
 * up implicitly, the source release tears it down. */
static inline void
configure_menu_rtmp_input (PexNinja * application)
{
  auto & srv = application->state.rtmp_server;

  ImGui::SetNextWindowSize (ImVec2 (500, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin ("RTMP Server", &application->state.windows.show_rtmp_input, ImGuiWindowFlags_AlwaysAutoResize);

  /* Server-wide listener settings. They are locked while the
   * listener is up because Pulse does not expose live reconfigure;
   * the user must Disconnect first to change path / port / TLS / auth. */
  if (srv.is_connected)
    imgui_begin_disabled_state ();

  ImGui::InputText ("path", srv.path, sizeof (srv.path));

  int port = (int)srv.listening_port;
  if (ImGui::InputInt ("listening_port", &port)) {
    if (port < 0)
      port = 0;
    if (port > 65535)
      port = 65535;
    srv.listening_port = (uint16_t)port;
  }

  ImGui::Checkbox ("use_tls", &srv.use_tls);
  ImGui::SameLine ();
  ImGui::Checkbox ("support_audio", &srv.support_audio);
  ImGui::SameLine ();
  ImGui::Checkbox ("support_video", &srv.support_video);

  ImGui::Separator ();
  ImGui::Checkbox ("Use auth config", &srv.use_auth);
  if (srv.use_auth) {
    ImGui::InputText ("auth username", srv.auth_username, sizeof (srv.auth_username));
    ImGui::InputText ("auth password", srv.auth_password, sizeof (srv.auth_password));
  }

  ImGui::Separator ();
  ImGui::Checkbox ("Use TLS input config", &srv.use_tls_config);
  if (srv.use_tls_config) {
    ImGui::InputText ("cert_file", srv.tls_cert_file, sizeof (srv.tls_cert_file));
    ImGui::InputText ("key_file", srv.tls_key_file, sizeof (srv.tls_key_file));
    ImGui::InputText ("ciphers", srv.tls_ciphers, sizeof (srv.tls_ciphers));
  }

  if (srv.is_connected)
    imgui_end_disabled_state ();

  ImGui::Separator ();

  /* Advertised host: not part of the listener config — used only to
   * build the Publish URL string the user copies and pastes into
   * publishers. Editable any time. On Windows, and when gethostname()
   * fails or returns something that isn't routable from the
   * publisher's machine, the user must override the default for the
   * displayed URL to be usable from another host. */
  ImGui::InputText ("advertised host", srv.advertised_host, sizeof (srv.advertised_host));
  ImGui::TextDisabled ("Used in the Publish URL displayed on each Compositor RTMP source.");
  ImGui::TextDisabled ("Override if the auto-detected hostname isn't reachable from your publisher.");

  ImGui::Separator ();

  /* Status line + Connect / Disconnect. */
  if (srv.is_connected) {
    ImGui::TextColored (green_color, "● listener up on port %u (%s)", srv.listening_port,
                        srv.use_tls ? "rtmps" : "rtmp");
    if (srv.lazy_started)
      ImGui::TextDisabled ("Started automatically by the Compositor.");
    {
      std::lock_guard<std::mutex> lock (srv.mu);
      ImGui::TextDisabled ("Path: %s  •  Owned by source: %s  •  Active publishers: %d",
                           srv.path[0] != '\0' ? srv.path : "(unset)", srv.owner_source_id != 0 ? "yes" : "no",
                           srv.live_publishers);
    }
  } else {
    ImGui::TextColored (ImVec4 (0.6f, 0.6f, 0.6f, 1.0f), "○ listener idle");
  }

  if (srv.is_connected)
    imgui_begin_disabled_state ();
  bool connect_clicked = ImGui::Button ("Connect");
  if (srv.is_connected)
    imgui_end_disabled_state ();

  ImGui::SameLine ();

  if (!srv.is_connected)
    imgui_begin_disabled_state ();
  bool disconnect_clicked = ImGui::Button ("Disconnect");
  if (!srv.is_connected)
    imgui_end_disabled_state ();

  ImGui::End ();

  if (connect_clicked && !srv.is_connected) {
    /* Explicit user-driven start — not lazy, so the listener will
     * outlive any individual Compositor source. */
    rtmp_server::_start (application, /*lazy=*/false);
  }

  if (disconnect_clicked && srv.is_connected) {
    /* Disconnect even if the Compositor still has registered paths
     * — the Compositor sources will read `is_connected` and re-bind
     * on next materialise. Nothing on the wire breaks: the existing
     * PulseVideoMixInputIDs simply stop receiving frames. */
    rtmp_server::_stop (application);
  }
}

static void
configure_menu (PexNinja * application)
{
  if (ImGui::BeginMenu ("Menu")) {

    bool show_config = ImGui::MenuItem ("Settings", NULL);
    set_show_settings (application, show_config);

    if (ImGui::MenuItem ("RTMP Server", NULL)) {
      application->state.windows.show_rtmp_input = true;
    }

    if (application->state.async_op.op == ASYNC_OP_NONE) {
      if (ImGui::MenuItem (application->state.registered ? "Deregister" : "Register", NULL)) {
        if (application->state.registered) {
          PulseAsyncOperationResultCallbackConfig async_op_cb_config = {.func = _pulse_async_operation_result_cb,
                                                                        .user_context = &application->state.async_op};
          PulseOperationProgressCallbackConfig progress_config = {.func = _progress_callback_registration,
                                                                  .user_context = application};

          PulseError err = pulse_deregister_async (application->client, &async_op_cb_config, &progress_config);
          if (err != PULSE_SUCCESS) {
            PEX_LOG_DEBUG ("pulse_deregister failed: %s\n", pulse_strerror (err));
            _update_registration_status_msg (application, "Failed to deregister");
            if (err != PULSE_ERROR_PROCESS_ABORTED)
              application->state.error_msg = pulse_strerror (err);
          } else {
            application->state.async_op.op = ASYNC_OP_DEREGISTER;
          }
        } else {
          application->state.windows.show_registration = true;
        }
      }
    } else {
      ImGui::MenuItem (application->state.registered ? "Deregister (disabled)" : "Register (disabled)", NULL);
    }
    bool video_muted = application->state.video_mute;
    ImGui::MenuItem ("Show self-view", NULL, &application->state.windows.show_self_view, !video_muted);
    ImGui::MenuItem ("Swap big/small video views", NULL, &application->state.windows.swap_video_views, !video_muted);
    ImGui::MenuItem ("Paint Tools", NULL, &application->state.windows.show_paint_tools, true);
    /* Compositor: the unified authoring surface for sources, regions
     * and the Program/Preview split. Folds in the standalone Source
     * Library + Patchbay windows that shipped in Phase 2a. */
    ImGui::MenuItem ("Compositor", NULL, &application->state.windows.show_compositor, true);

    if (ImGui::BeginMenu ("Debug")) {
      ImGui::MenuItem ("PexNinja Info", NULL, &application->state.windows.show_pexninja_info, true);
      ImGui::MenuItem ("Pulse Info", NULL, &application->state.windows.show_pulse_info, true);
      ImGui::MenuItem ("Metrics", NULL, &application->state.windows.show_metrics, true);

      if (ImGui::MenuItem ("Enable verbose logging (curl)", NULL, &application->state.enable_verbose_logging, true)) {
        pulse_options_set_verbose_logging (application->client, application->state.enable_verbose_logging);
      }
      ImGui::EndMenu ();
    }
    application->state.quit = ImGui::MenuItem ("Exit", NULL);
    ImGui::EndMenu ();
  }
}

static void
configure_menu_mute (PexNinja * application)
{
  bool disabled = (application->state.conn_status == PULSE_CONNECTION_STATUS_RECONNECTING);
  if (disabled)
    imgui_begin_disabled_state ();

  if (ImGui::BeginMenu ("Mute")) {
    if (application->state.audio_mute) {
      if (ImGui::MenuItem ("Audio unmute", NULL)) {
        application->state.audio_mute = false;
        pulse_mute_audio_input (application->client, application->state.audio_mute);
      }
    } else {
      if (ImGui::MenuItem ("Audio mute", NULL)) {
        application->state.audio_mute = true;
        pulse_mute_audio_input (application->client, application->state.audio_mute);
      }
    }
    if (application->state.video_mute) {
      if (ImGui::MenuItem ("Video unmute", NULL)) {
        application->state.video_mute = false;
        pulse_mute_video_input (application->client, application->state.video_mute);
      }
    } else {
      if (ImGui::MenuItem ("Video mute", NULL)) {
        application->state.video_mute = true;
        pulse_mute_video_input (application->client, application->state.video_mute);
      }
    }

    if (ImGui::MenuItem ("Disconnect Main Audio", NULL)) {
      application->config.devices.microphone_set = false;
      application->config.devices.speaker_set = false;
      application->config.devices.microphone_id = -1;
      application->config.devices.speaker_id = -1;
      application->config.devices.speaker_use_default = false;
      application->config.devices.microphone_use_default = false;
      pulse_device_session_disconnect_main_audio (application->client);
    }

    if (ImGui::MenuItem ("Disconnect Main Video", NULL)) {
      application->config.devices.camera_set = false;
      application->config.devices.camera_id = -1;
      pulse_device_session_disconnect_main_video (application->client, PULSE_MEDIA_CONTENT_MAIN, PULSE_MEDIA_INPUT);
    }

    ImGui::EndMenu ();
  }

  if (disabled)
    imgui_end_disabled_state ();
}

static void
configure_menu_connection_state (PexNinja * application)
{
  bool can_abort = application->state.async_op.op == ASYNC_OP_CONNECT && application->state.abort == false;
  bool is_reconnecting = application->state.conn_status == PULSE_CONNECTION_STATUS_RECONNECTING;

  if (can_abort || is_reconnecting) {
    const char * button_text = is_reconnecting ? "Abort-Reconnect" : "Abort-Connect";
    if (ImGui::MenuItem (button_text, NULL)) {
      _update_conference_status_msg (application, "Attempting to abort connection...");
      pulse_cancel_request (application->client);
      application->state.abort = true;
    }
    return;
  }

  /* Hide the connect menu item when already connected */
  if (application->state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED)
    return;

  bool disabled = (application->state.async_op.op != ASYNC_OP_NONE);
  if (disabled)
    imgui_begin_disabled_state ();

  if (ImGui::MenuItem (" Connect ", NULL)) {
    application->state.windows.show_connection = !application->state.windows.show_connection;
    if (application->state.windows.show_connection) {
      _init_conference_alias_list (application);
      _init_device_alias_list (application);
    }
  }

  if (disabled)
    imgui_end_disabled_state ();
}

static void
configure_menu_controls (PexNinja * application)
{
  if (application->state.conn_status != PULSE_CONNECTION_STATUS_CONNECTED) {
    application->state.buzz = false;
    application->state.windows.show_roster_list = false;
  }

  ImGui::Separator ();
  PulseConferenceRole role = PULSE_CONFERENCE_ROLE_GUEST;

  if (application->state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
    PulseError err = pulse_session_get_role (application->client, &role);
    if (err != PULSE_SUCCESS) {
      if (err != PULSE_ERROR_NOT_CONNECTED) {
        PEX_LOG_DEBUG ("Failed to call pulse_session_get_role: %s\n", pulse_strerror (err));
      }
      role = PULSE_CONFERENCE_ROLE_GUEST;
    }
  }

  if (ImGui::BeginMenu ("Controls")) {
    if (application->state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
      if (ImGui::BeginMenu ("Participant")) {
        if (application->state.buzz) {
          if (ImGui::MenuItem ("Lower hand", NULL)) {
            application->state.buzz = false;
            pulse_participant_control_clearbuzz (application->client, NULL);
          }
        } else {
          if (ImGui::MenuItem ("Raise hand", NULL)) {
            application->state.buzz = true;
            pulse_participant_control_buzz (application->client, NULL);
          }
        }
        if (application->state.live_captions) {
          if (ImGui::MenuItem ("Hide live captions", NULL)) {
            application->state.live_captions = false;
            pulse_participant_control_hide_live_captions (application->client, NULL);
          }
        } else {
          if (ImGui::MenuItem ("Show live captions", NULL)) {
            application->state.live_captions = true;
            pulse_participant_control_show_live_captions (application->client, NULL);
          }
        }
        ImGui::EndMenu ();
      }
      if (role == PULSE_CONFERENCE_ROLE_GUEST) {
        std::lock_guard<std::mutex> lock (application->room_list.mutex);
        configure_menu_breakout_rooms_guest_unlocked (application);
      }

      if (role == PULSE_CONFERENCE_ROLE_GUEST &&
          application->state.current_service_type == PULSE_CONFERENCE_SERVICE_TYPE_WAITING_ROOM) {
        if (ImGui::BeginMenu ("Conference")) {
          if (ImGui::MenuItem ("Send host PIN code", NULL)) {
            application->state.dtmf.send_to_recepient = false;
            application->state.popup_send_dtmf_sequence = true;
          }
          ImGui::EndMenu ();
        }
      }

      if (role == PULSE_CONFERENCE_ROLE_HOST) {
        if (ImGui::BeginMenu ("Conference")) {
          if (ImGui::MenuItem ("Add participant", NULL)) {
            application->state.windows.show_add_participant_window = true;
            _init_device_alias_list (application);
          }

          std::lock_guard<std::mutex> lock (application->room_list.mutex);
          if (ImGui::MenuItem ("Lock conference", NULL, &application->room_list.current_room->conference_status.locked,
                               true)) {
            // Value will have been toggled by button-click!
            if (application->room_list.current_room->conference_status.locked)
              pulse_conference_control_lock (application->client);
            else
              pulse_conference_control_unlock (application->client);
          }

          if (ImGui::MenuItem ("Mute guests", NULL,
                               &application->room_list.current_room->conference_status.guests_muted, true)) {
            // Value will have been toggled by button-click!
            if (application->room_list.current_room->conference_status.guests_muted)
              pulse_conference_control_mute_guests (application->client);
            else
              pulse_conference_control_unmute_guests (application->client);
          }

          if (ImGui::MenuItem ("Guests allowed to unmute", NULL,
                               &application->room_list.current_room->conference_status.set_guests_can_unmute, true)) {
            // Value will have been toggled by button-click!
            pulse_conference_control_set_guests_can_unmute (
              application->client, application->room_list.current_room->conference_status.set_guests_can_unmute);
          }

          if (ImGui::MenuItem ("Lower all raised hands", NULL)) {
            pulse_conference_control_clearallbuzz (application->client);
          }
          configure_menu_change_layout (application);
          configure_menu_breakout_rooms_host_unlocked (application);

          ImGui::EndMenu ();
        }
      }
    } /* If connected */

    if (ImGui::BeginMenu ("Camera")) {
      ImGui::MenuItem ("PTZ controls", NULL, &application->state.windows.show_camera_controls,
                       application->config.options.use_pulse_internal_ptz);
      if (ImGui::BeginMenu ("Rotation")) {
        if (ImGui::MenuItem ("0 degrees", NULL, application->state.rotation == PULSE_MEDIA_ROTATION_0, true)) {
          application->state.rotation = PULSE_MEDIA_ROTATION_0;
          pulse_media_input_main_set_rotation (application->client, PULSE_MEDIA_ROTATION_0);
        }
        if (ImGui::MenuItem ("90 degrees", NULL, application->state.rotation == PULSE_MEDIA_ROTATION_90, true)) {
          application->state.rotation = PULSE_MEDIA_ROTATION_90;
          pulse_media_input_main_set_rotation (application->client, PULSE_MEDIA_ROTATION_90);
        }
        if (ImGui::MenuItem ("180 degrees", NULL, application->state.rotation == PULSE_MEDIA_ROTATION_180, true)) {
          application->state.rotation = PULSE_MEDIA_ROTATION_180;
          pulse_media_input_main_set_rotation (application->client, PULSE_MEDIA_ROTATION_180);
        }
        if (ImGui::MenuItem ("270 degrees", NULL, application->state.rotation == PULSE_MEDIA_ROTATION_270, true)) {
          application->state.rotation = PULSE_MEDIA_ROTATION_270;
          pulse_media_input_main_set_rotation (application->client, PULSE_MEDIA_ROTATION_270);
        }
        ImGui::EndMenu ();
      }
      if (ImGui::MenuItem ("Blur", NULL, &application->config.options.enable_blur)) {
        pulse_options_set_background_blur (application->client, application->config.options.enable_blur);
      }
      if (ImGui::MenuItem ("Background Replacement", NULL, &application->config.options.enable_bg_replacement)) {
        bg_replacement_on_mix (application);
      }
      if (ImGui::MenuItem ("Scrambler", NULL, &application->config.options.enable_scrambler)) {
        pulse_options_set_video_scrambling (application->client, application->config.options.enable_scrambler);
      }

      ImGui::EndMenu ();
    }

    if (application->state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
      if (ImGui::BeginMenu ("Audio mixer")) {
        if (ImGui::BeginMenu ("Receive")) {
          std::lock_guard<std::mutex> lock (application->room_list.mutex);
          if (application->room_list.current_room->audio_mixers_list.data) {
            for (size_t i = 0; i < application->room_list.current_room->audio_mixers_list.data->audio_mixers_list_size;
                 i++) {
              const char * s = application->room_list.current_room->audio_mixers_list.data->audio_mixers_list[i];
              bool eq = application->state.audio_mixer.recv_mix_name.compare (s) == 0;
              if (ImGui::MenuItem (s, NULL, eq, true)) {
                if (pulse_participant_control_receive_from_audio_mix (application->client, NULL, s) == PULSE_SUCCESS) {
                  application->state.audio_mixer.recv_mix_name = std::string (s);
                }
              }
            }
          }
          ImGui::EndMenu ();
        }
        if (ImGui::BeginMenu ("Send")) {
          std::lock_guard<std::mutex> lock (application->room_list.mutex);
          if (application->room_list.current_room->audio_mixers_list.data) {
            for (size_t i = 0; i < application->room_list.current_room->audio_mixers_list.data->audio_mixers_list_size;
                 i++) {
              const char * s = application->room_list.current_room->audio_mixers_list.data->audio_mixers_list[i];
              bool eq = application->state.audio_mixer.send_mix_name.compare (s) == 0;
              if (ImGui::MenuItem (s, NULL, eq, true)) {
                if (pulse_participant_control_send_to_audio_mix (
                      application->client, NULL, s, application->state.audio_mixer.prominent) == PULSE_SUCCESS) {
                  application->state.audio_mixer.send_mix_name = std::string (s);
                }
              }
            }
          }
          if (ImGui::MenuItem ("<create new mixer>", NULL)) {
            application->state.audio_mixer.popup_new_flagged = true;
          }
          if (ImGui::BeginMenu ("Prominent")) {
            if (ImGui::MenuItem ("On", NULL, application->state.audio_mixer.prominent == true, true)) {
              if (pulse_participant_control_send_to_audio_mix (application->client, NULL,
                                                               application->state.audio_mixer.send_mix_name.c_str (),
                                                               true) == PULSE_SUCCESS) {
                application->state.audio_mixer.prominent = true;
              }
            }
            if (ImGui::MenuItem ("Off", NULL, application->state.audio_mixer.prominent == false, true)) {
              if (pulse_participant_control_send_to_audio_mix (application->client, NULL,
                                                               application->state.audio_mixer.send_mix_name.c_str (),
                                                               false) == PULSE_SUCCESS) {
                application->state.audio_mixer.prominent = false;
              }
            }
            ImGui::EndMenu ();
          }
          ImGui::EndMenu ();
        }
        ImGui::EndMenu ();
      }
    }
    ImGui::EndMenu ();
  }
}

static void
configure_menu_info (PexNinja * application)
{
  if (application->state.conn_status != PULSE_CONNECTION_STATUS_CONNECTED) {
    return;
  }
  if (ImGui::BeginMenu ("Info")) {
    if (application->state.live_captions) {
      if (ImGui::MenuItem ("Show live captions", NULL)) {
        application->state.windows.show_live_captions_window = !application->state.windows.show_live_captions_window;
      }
    }
    if (ImGui::MenuItem ("Show chat messages", NULL)) {
      application->state.windows.show_chat_window = !application->state.windows.show_chat_window;
    }
    if (ImGui::MenuItem ("Show roster list", NULL)) {
      application->state.windows.show_roster_list = !application->state.windows.show_roster_list;
    }
    if (ImGui::MenuItem ("Pmx Media Stats", NULL))
      application->state.windows.show_pmx_media_stats = true;
    ImGui::EndMenu ();
  }
}

#define SEPARATE_IF_NEEDED(sep)                                                                                        \
  {                                                                                                                    \
    if (sep)                                                                                                           \
      ImGui::Separator ();                                                                                             \
    sep = true;                                                                                                        \
  }

static void
configure_bottom_bar_status (PexNinja * application)
{
  bool sep_state = false;

  /* Conference status */
  if (application->state.async_op.op == ASYNC_OP_CONNECT || application->state.async_op.op == ASYNC_OP_DISCONNECT) {
    SEPARATE_IF_NEEDED (sep_state);
    std::lock_guard<std::mutex> lock (application->state.status_lock);
    ImGui::TextColored (yellow_color, "%s", application->state.conference_status);
  } else if (application->state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
    SEPARATE_IF_NEEDED (sep_state);
    bool direct_media = false;
    std::lock_guard<std::mutex> lock (application->room_list.mutex);
    if (application->room_list.current_room) {
      direct_media = application->room_list.current_room->conference_status.direct_media;
    }

    if (direct_media) {
      ImGui::TextColored (green_color, "connected:direct-media");
    } else if (application->state.current_service_type == PULSE_CONFERENCE_SERVICE_TYPE_CONFERENCE) {
      ImGui::TextColored (green_color, "connected:conference");
    } else {
      ImGui::TextColored (green_color, "connected:%s",
                          pulse_type_mapping_service_type_to_string (application->state.current_service_type));
    }
  } else if (application->state.conn_status == PULSE_CONNECTION_STATUS_RECONNECTING) {
    SEPARATE_IF_NEEDED (sep_state);
    ImGui::TextColored (yellow_red_shift, "connected");
  }

  /* Registration status */
  if (application->state.async_op.op == ASYNC_OP_REGISTER || application->state.async_op.op == ASYNC_OP_DEREGISTER) {
    SEPARATE_IF_NEEDED (sep_state);
    std::lock_guard<std::mutex> lock (application->state.status_lock);
    ImGui::TextColored (yellow_color, "%s", application->state.registration_status);
  } else if (application->state.registered) {
    SEPARATE_IF_NEEDED (sep_state);
    if (application->state.reg_status == PULSE_CONNECTION_STATUS_RECONNECTING)
      ImGui::TextColored (yellow_red_shift, "registered");
    else
      ImGui::TextColored (green_color, "registered");
  }

  if (application->state.audio_temporarily_unmuted) {
    SEPARATE_IF_NEEDED (sep_state);
    ImGui::TextColored (yellow_green_shift, "audio-temporarily-unmuted");
  } else if (application->state.audio_mute) {
    SEPARATE_IF_NEEDED (sep_state);
    ImGui::TextColored (yellow_color, "audio-muted");
  }
  if (application->state.video_mute) {
    SEPARATE_IF_NEEDED (sep_state);
    ImGui::TextColored (yellow_color, "video-muted");
  }
  if (application->state.buzz) {
    SEPARATE_IF_NEEDED (sep_state);
    ImGui::TextColored (yellow_color, "hand raised");
  }

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  if (application->room_list.current_room &&
      application->room_list.current_room->chat_messages.chat_messages_unread > 0) {
    ImGui::Separator ();
    ImGui::TextColored (yellow_red_shift, "%u unread messages",
                        application->room_list.current_room->chat_messages.chat_messages_unread);
  }
}

static float
calc_network_info_width (PexNinja * application)
{
  float width = 0.0f;
  float space = ImGui::GetStyle ().ItemSpacing.x;

  width += ImGui::CalcTextSize ("Network:").x + space;

  if (application->state.network_status.connectivity != PULSE_NETWORK_CONNECTIVITY_FULL) {
    width += ImGui::CalcTextSize ("not available").x + space;
  } else {
    bool has_ipv4 = application->state.network_status.has_ipv4_address;
    bool has_ipv6 = application->state.network_status.has_ipv6_address;
    if (has_ipv4) {
      width += ImGui::CalcTextSize (application->state.network_status.ipv4_address).x + space;
    }
    if (has_ipv6) {
      if (has_ipv4)
        width += space + 1.0f; // vertical separator line
      width += ImGui::CalcTextSize (application->state.network_status.ipv6_address).x + space;
    }
  }

  return width;
}

static void
configure_bottom_bar_network (PexNinja * application)
{
  bool sep_state = false;
  ImGui::TextColored (clear_color, "Network:");
  if (application->state.network_status.connectivity != PULSE_NETWORK_CONNECTIVITY_FULL) {
    SEPARATE_IF_NEEDED (sep_state);
    ImGui::TextColored (red_color, "not available");
  } else {
    if (application->state.network_status.has_ipv4_address) {
      SEPARATE_IF_NEEDED (sep_state);
      ImGui::TextColored (green_color, "%s", application->state.network_status.ipv4_address);
    }
    if (application->state.network_status.has_ipv6_address) {
      SEPARATE_IF_NEEDED (sep_state);
      ImGui::TextColored (green_color, "%s", application->state.network_status.ipv6_address);
    }
  }
}

static void
configure_bottom_bar (PexNinja * application)
{
  ImGuiViewport * viewport = ImGui::GetMainViewport ();
  float bar_h = (float)bottombar_height;

  ImGui::SetNextWindowPos (ImVec2 (viewport->Pos.x, viewport->Pos.y + viewport->Size.y - bar_h));
  ImGui::SetNextWindowSize (ImVec2 (viewport->Size.x, bar_h));

  ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar;

  ImGui::PushStyleVar (ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_WindowMinSize, ImVec2 (0, 0));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (0, 0));

  if (ImGui::Begin ("##BottomBar", NULL, flags)) {
    if (ImGui::BeginMenuBar ()) {
      configure_bottom_bar_status (application);

      float network_width = calc_network_info_width (application);
      float bar_max_x = ImGui::GetWindowContentRegionMax ().x;
      float target_x = bar_max_x - network_width;
      float current_x = ImGui::GetCursorPosX ();
      if (target_x > current_x)
        ImGui::SetCursorPosX (target_x);

      configure_bottom_bar_network (application);

      ImGui::EndMenuBar ();
    }
  }
  ImGui::End ();
  ImGui::PopStyleVar (3);
}

static void
configure_menu_presentation_status (PexNinja * application)
{
  if (application->state.async_op.op != ASYNC_OP_NONE ||
      application->state.conn_status != PULSE_CONNECTION_STATUS_CONNECTED) {
    return;
  }

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  if (!application->room_list.current_room->presentation.preso_started)
    return;

  ImGui::Text ("%s is presenting", application->room_list.current_room->presentation.presenter_name);
}

static inline void
configure_main_menu (PexNinja * application)
{
  ImGui::BeginMainMenuBar ();

  configure_menu (application);
  configure_menu_mute (application);
  configure_menu_connection_state (application);
  configure_menu_presentation_control (application);
  configure_menu_controls (application);
  configure_menu_info (application);
  ImGui::Separator ();
  configure_menu_presentation_status (application);

  ImGui::EndMainMenuBar ();
}

static inline void
_configure_popup_referal_request (PexNinja * application)
{
  if (ImGui::BeginPopupModal ("Referal request", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->referal_request.popup_state == POPUP_STATE_HIDDEN) {
      ImGui::CloseCurrentPopup ();
    } else {
      ImGui::SetItemDefaultFocus ();

      ImGui::Text ("You will be transferred to breakout room '%s' in : %u seconds",
                   application->referal_request.breakout_name.c_str (), (application->referal_request.timeout));

      if (ImGui::Button ("Transfer now")) {
        application->referal_request.popup_state = POPUP_STATE_HIDDEN;
        ImGui::CloseCurrentPopup ();
      }

      if (application->referal_request.popup_state == POPUP_STATE_HIDDEN) {
        ImGui::CloseCurrentPopup ();
      }
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_audio_mute_request (PexNinja * application)
{
  if (ImGui::BeginPopupModal ("Accept remote audio unmute request", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->audio_unmute.popup_state == POPUP_STATE_HIDDEN) {
      _audio_unmute_approval_callback_complete (application, false);
      ImGui::CloseCurrentPopup ();
    } else {
      ImGui::SetItemDefaultFocus ();
      ImGui::Text ("A remote host is requesting that you are unmuted.");
      ImGui::Text ("");
      ImGui::Text ("Accept unmute?");

      if (ImGui::Button ("Accept")) {
        _audio_unmute_approval_callback_complete (application, true);
        ImGui::CloseCurrentPopup ();
      }
      ImGui::SameLine ();
      if (ImGui::Button ("Deny")) {
        _audio_unmute_approval_callback_complete (application, false);
        ImGui::CloseCurrentPopup ();
      }
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_tls_degrade (PexNinja * application)
{
  if (ImGui::BeginPopupModal ("Accept TLS degrade", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->tls_degrade.popup_state == POPUP_STATE_HIDDEN) {
      _tls_degrade_approval_callback_complete (application, false);
      ImGui::CloseCurrentPopup ();
    } else {
      ImGui::SetItemDefaultFocus ();
      ImGui::Text ("Cannot make a secure connection to server: %s", application->tls_degrade.host);
      ImGui::Text ("Reason: %s", application->tls_degrade.reason);
      ImGui::Text ("");
      ImGui::Text ("Accept degraded TLS connection? (This is unsafe!)");

      if (ImGui::Button ("Accept")) {
        _tls_degrade_approval_callback_complete (application, true);
        ImGui::CloseCurrentPopup ();
      }
      ImGui::SameLine ();
      if (ImGui::Button ("Deny")) {
        _tls_degrade_approval_callback_complete (application, false);
        ImGui::CloseCurrentPopup ();
      }
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_pin_code_request (PexNinja * application)
{
  if (ImGui::BeginPopupModal ("Pin code request", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->pin_code_request.popup_state == POPUP_STATE_HIDDEN) {
      _pin_code_request_callback_complete (application, false);
      ImGui::CloseCurrentPopup ();
    } else {
      ImGui::SetItemDefaultFocus ();

      if (application->pin_code_request.needs_pin_input_set == false &&
          application->pin_code_request.guest_pin_required == false) {

        ImGui::Text ("Are you joining as a guest or a host participant?");

        if (ImGui::Button ("Guest")) {
          application->pin_code_request.needs_pin_input = false;
          application->pin_code_request.needs_pin_input_set = true;
          _pin_code_request_callback_complete (application, true);
          ImGui::CloseCurrentPopup ();
        }

        ImGui::SameLine ();

        if (ImGui::Button ("Host")) {
          application->pin_code_request.needs_pin_input = true;
          application->pin_code_request.needs_pin_input_set = true;
        }
      }

      if ((application->pin_code_request.needs_pin_input_set && application->pin_code_request.needs_pin_input) ||
          application->pin_code_request.guest_pin_required) {
        ImGui::Text ("Enter PIN code:");

        if (ImGui::InputText ("##pin_code", application->pin_code_request.pin_code,
                              sizeof (application->pin_code_request.pin_code) - 1,
                              ImGuiInputTextFlags_EnterReturnsTrue)) {
          if (strlen (application->pin_code_request.pin_code) > 0) {
            _pin_code_request_callback_complete (application, true);
            ImGui::CloseCurrentPopup ();
          }
        }

        if (strlen (application->pin_code_request.pin_code) == 0)
          imgui_begin_disabled_state ();

        if (ImGui::Button ("OK")) {
          _pin_code_request_callback_complete (application, true);
          ImGui::CloseCurrentPopup ();
        }
        if (strlen (application->pin_code_request.pin_code) == 0)
          imgui_end_disabled_state ();

        ImGui::SameLine ();

        if (ImGui::Button ("Join as guest")) {
          application->pin_code_request.needs_pin_input = false;
          application->pin_code_request.needs_pin_input_set = true;
          _pin_code_request_callback_complete (application, true);
          ImGui::CloseCurrentPopup ();
        }

        ImGui::SameLine ();

        if (ImGui::Button ("Cancel joining")) {
          _pin_code_request_callback_complete (application, false);
          ImGui::CloseCurrentPopup ();
        }
      }
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_conference_extension_request (PexNinja * application)
{
  if (ImGui::BeginPopupModal ("Conference extension request", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->conference_extension_request.popup_state == POPUP_STATE_HIDDEN) {
      _conference_extension_request_callback_complete (application, false);
      ImGui::CloseCurrentPopup ();
    } else {
      ImGui::SetItemDefaultFocus ();

      ImGui::Text ("Enter conference extension:");

      if (ImGui::InputText ("##conference_extension", application->conference_extension_request.conference_extension,
                            sizeof (application->conference_extension_request.conference_extension) - 1,
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (strlen (application->conference_extension_request.conference_extension) > 0) {
          _conference_extension_request_callback_complete (application, true);
          ImGui::CloseCurrentPopup ();
        }
      }

      bool disable_commit = strlen (application->conference_extension_request.conference_extension) == 0;

      if (disable_commit)
        imgui_begin_disabled_state ();

      if (ImGui::Button ("OK")) {
        _conference_extension_request_callback_complete (application, true);
        ImGui::CloseCurrentPopup ();
      }

      if (disable_commit)
        imgui_end_disabled_state ();

      ImGui::SameLine ();
      if (ImGui::Button ("Cancel")) {
        _conference_extension_request_callback_complete (application, false);
        ImGui::CloseCurrentPopup ();
      }
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_connect_error (PexNinja * application)
{
  if (application->state.error_msg) {
    ImGui::OpenPopup ("Failed to connect");
  }

  if (ImGui::BeginPopupModal ("Failed to connect", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetItemDefaultFocus ();
    ImGui::Text ("Failed to connect: %s", application->state.error_msg);
    if (ImGui::Button ("Close")) {
      application->state.error_msg = NULL;
      ImGui::CloseCurrentPopup ();
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_incoming_call (PexNinja * application)
{
  if (ImGui::BeginPopupModal ("Incoming call", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->incoming_call.popup_state == POPUP_STATE_HIDDEN) {
      _registrations_event_incoming_callback_complete (application, false);
      ImGui::CloseCurrentPopup ();
    } else {
      assert (application->incoming_call.event != NULL);
      if (application->incoming_call.event->cancelled) {
        _registrations_event_incoming_callback_complete (application, false);
        ImGui::CloseCurrentPopup ();
      } else {
        ImGui::SetItemDefaultFocus ();
        const char * source_type = (application->incoming_call.event->source_type == 0) ? "device" : "vmr";
        ImGui::Text ("Incoming %s call from %s", source_type, application->incoming_call.event->conference_alias);
        ImGui::Text ("You were invited to join the call by %s (%s)",
                     application->incoming_call.event->remote_display_name,
                     application->incoming_call.event->remote_alias);
        if (application->state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
          ImGui::Text ("Note: If you accept the invite, you will be disconnected from your "
                       "ongoing call.");
        }
        if (ImGui::Button ("Accept")) {
          _registrations_event_incoming_callback_complete (application, true);
          ImGui::CloseCurrentPopup ();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Deny")) {
          _registrations_event_incoming_callback_complete (application, false);
          ImGui::CloseCurrentPopup ();
        }
      }
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_missed_call (PexNinja * application)
{
  if (ImGui::BeginPopupModal ("Missed call", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->incoming_call_cancelled.popup_state == POPUP_STATE_HIDDEN) {
      application->incoming_call_cancelled.origin.clear ();
      ImGui::CloseCurrentPopup ();
    } else {
      ImGui::Text ("Missed call from %s", application->incoming_call_cancelled.origin.c_str ());

      if (ImGui::Button ("Close")) {
        application->incoming_call_cancelled.origin.clear ();
        application->incoming_call_cancelled.popup_state = POPUP_STATE_HIDDEN;
        ImGui::CloseCurrentPopup ();
      }
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_sso_provider_selection (PexNinja * application)
{
  if (ImGui::BeginPopupModal ("Authentication needed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->sso_provider.popup_state == POPUP_STATE_HIDDEN) {
      _sso_selection_callback_complete (application, -1, true);
      ImGui::CloseCurrentPopup ();
    } else {
      ImGui::SetItemDefaultFocus ();

      static int index = 0;
      if (application->sso_provider.list->num == 1) {
        ImGui::Text ("The %s server requests that we %s with %s.",
                     application->sso_provider.list->request_type == PULSE_SSO_REQUEST_CONFERENCE ? "conference"
                                                                                                  : "registration",
                     application->sso_provider.list->is_reconnect ? "re-authenticate" : "authenticate",
                     application->sso_provider.list->providers[0].name);
      } else {
        ImGui::Text ("The %s server requires you to choose a SSO provider from the list below:",
                     application->sso_provider.list->request_type == PULSE_SSO_REQUEST_CONFERENCE ? "conference"
                                                                                                  : "registration");
        for (int i = 0; i < application->sso_provider.list->num; i++) {
          ImGui::RadioButton (application->sso_provider.list->providers[i].name, &index, i);
        }
      }

      bool accept = ImGui::Button ("Accept");
      if (ImGui::IsKeyPressed (ImGuiKey_Enter) ||
          ImGui::IsKeyPressed (ImGuiKey_KeypadEnter))
        accept = true;

      if (accept) {
        _sso_selection_callback_complete (application, index, false);
        ImGui::CloseCurrentPopup ();
      }
      ImGui::SameLine ();
      if (ImGui::Button ("Deny")) {
        _sso_selection_callback_complete (application, -1, true);
        ImGui::CloseCurrentPopup ();
      }
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_new_audio_mixer (PexNinja * application)
{
  if (application->state.audio_mixer.popup_new_flagged) {
    ImGui::OpenPopup ("Create new audio mixer");
  }
  if (ImGui::BeginPopupModal ("Create new audio mixer", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetItemDefaultFocus ();

    ImGui::Text ("Enter audio mixer name:");

    static char name[1024];
    bool acknowledged = false;
    if (ImGui::InputText ("##audio_mixer_name", name, sizeof (name) - 1, ImGuiInputTextFlags_EnterReturnsTrue)) {
      if (strlen (name) > 0) {
        // Setup stuff.
        acknowledged = true;
        ImGui::CloseCurrentPopup ();
      }
    }
    if (strlen (name) == 0)
      imgui_begin_disabled_state ();

    if (ImGui::Button ("OK")) {
      acknowledged = true;
      ImGui::CloseCurrentPopup ();
    }
    if (strlen (name) == 0)
      imgui_end_disabled_state ();

    ImGui::SameLine ();
    if (ImGui::Button ("Cancel")) {
      application->state.audio_mixer.popup_new_flagged = false;
      ImGui::CloseCurrentPopup ();
    }

    if (acknowledged) {
      if (pulse_participant_control_send_to_audio_mix (application->client, NULL, name,
                                                       application->state.audio_mixer.prominent) == PULSE_SUCCESS) {
        application->state.audio_mixer.send_mix_name = std::string (name);
      }
      application->state.audio_mixer.popup_new_flagged = false;
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_new_breakout_room (PexNinja * application)
{
  if (application->state.breakout_rooms.popup_new_flagged) {
    ImGui::OpenPopup ("Create new breakout room");
  }
  if (ImGui::BeginPopupModal ("Create new breakout room", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetItemDefaultFocus ();

    bool acknowledged = false;
    static char name[1024];
    ImGui::Text ("Enter room name:");
    if (ImGui::InputText ("##room_name", name, sizeof (name) - 1, ImGuiInputTextFlags_EnterReturnsTrue)) {
      //
    }

    ImGui::Separator ();
    ImGui::Text ("Room Settings::");

    static int duration = 0;
    ImGui::Text ("Duration (0=indefinitely):");
    ImGui::InputInt ("minutes", &duration);

    static int end_action = BREAKOUT_ROOM_END_ACTION_TRANSFER;
    ImGui::Text ("End action:");
    ImGui::RadioButton ("Transfer", &end_action, BREAKOUT_ROOM_END_ACTION_TRANSFER);
    ImGui::SameLine ();
    ImGui::RadioButton ("Disconnect", &end_action, BREAKOUT_ROOM_END_ACTION_DISCONNECT);

    static bool guests_allowed_to_leave = false;
    ImGui::Checkbox ("Guests allowed to leave", &guests_allowed_to_leave);

    if (strlen (name) == 0)
      imgui_begin_disabled_state ();

    if (ImGui::Button ("OK")) {
      acknowledged = true;
      application->state.breakout_rooms.popup_new_flagged = false;
      ImGui::CloseCurrentPopup ();
    }

    if (strlen (name) == 0)
      imgui_end_disabled_state ();

    ImGui::SameLine ();
    if (ImGui::Button ("Cancel")) {
      application->state.breakout_rooms.popup_new_flagged = false;
      ImGui::CloseCurrentPopup ();
    }

    if (acknowledged) {
      PulseConferenceControlBreakoutsCreateRequest req;
      memset (&req, 0, sizeof (PulseConferenceControlBreakoutsCreateRequest));
      req.breakout_name = name;
      req.duration = duration;
      req.end_action = (PulseConferenceStatusBreakoutRoomEndAction)end_action;
      req.transfer_participants_num = 0;
      req.transfer_participants = NULL;
      req.guests_allowed_to_leave = guests_allowed_to_leave;

      PulseRoomId room_id = 0;
      pulse_conference_control_breakout_create (application->client, &req, &room_id);
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_participant_move (PexNinja * application)
{
  if (application->state.breakout_rooms.popup_participant_move_flagged) {
    ImGui::OpenPopup ("Move participants between rooms");
  }
  if (ImGui::BeginPopupModal ("Move participants between rooms", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetItemDefaultFocus ();

    std::lock_guard<std::mutex> lock (application->room_list.mutex);

    static PulseRoomId source_room_id = PULSE_ROOM_ID_MAIN;
    static PulseRoomId destination_room_id = (PulseRoomId)-1;
    // static std::vector<bool> chosen_participants;
    static std::set<const char *> chosen_participants;
    // bool initialize_chosen_participants = false;
    bool is_ui_disabled_state = false;

    ImGui::Text ("Source room:");
    PexNinjaRoom * source_room = application->room_list.room_map[source_room_id];
    const char * source_room_name = (source_room->conference_status.breakout_rooms.enabled)
                                      ? source_room->conference_status.breakout_rooms.name.c_str ()
                                      : "Main room";
    if (ImGui::BeginCombo ("Source room", source_room_name, ImGuiComboFlags_None)) {
      PulseRoomId local_destination_room_id = -1;
      bool source_room_changed = false;
      for (auto it = application->room_list.room_map.begin (); it != application->room_list.room_map.end (); it++) {
        if (it->first == destination_room_id)
          continue;
        if (it->second->roster_list.active_participants == 0)
          continue;
        const char * name = (it->second->conference_status.breakout_rooms.enabled)
                              ? it->second->conference_status.breakout_rooms.name.c_str ()
                              : "Main room";
        if (ImGui::Selectable (name, (source_room_id == it->first))) {
          source_room_id = it->first;
          source_room_changed = true;
          chosen_participants.clear ();
          // initialize_chosen_participants = true;
        } else if (local_destination_room_id == (PulseRoomId)-1) {
          local_destination_room_id = it->first;
        }
      }
      if (source_room_changed) {
        PEX_LOG_ERROR ("Source room changed to %u, setting destination to %u", source_room_id, local_destination_room_id);
        destination_room_id = local_destination_room_id;
      }
      ImGui::EndCombo ();
    }

    PexNinjaRoom * room = application->room_list.room_map[source_room_id];
    assert (room);
    if (room->roster_list.active_participants == 0) {
      ImGui::Text ("The room is empty.");
    } else {
      for (int i = 0; i < (int)room->roster_list.data->participant_list_size; i++) {
        if (room->roster_list.data->participant_list[i]->is_active_participant == false)
          continue;
        auto search = chosen_participants.find (room->roster_list.data->participant_list[i]->uuid);
        bool state = (search != chosen_participants.end ());
        const char * name = room->roster_list.data->participant_list[i]->display_name;
        if (ImGui::MenuItem (name, NULL, &state, true)) {
          if (state)
            chosen_participants.insert (room->roster_list.data->participant_list[i]->uuid);
          else
            chosen_participants.erase (room->roster_list.data->participant_list[i]->uuid);
          PEX_LOG_ERROR ("%s participant %s", state ? "Selected" : "Unselected", name);
        }
      }
    }

    if (destination_room_id == (PulseRoomId)-1) {
      destination_room_id = source_room_id;
      imgui_begin_disabled_state ();
      is_ui_disabled_state = true;
    }

    ImGui::Separator ();
    ImGui::Text ("Destination room:");
    PexNinjaRoom * destination_room = application->room_list.room_map[destination_room_id];
    const char * destination_room_name = (destination_room->conference_status.breakout_rooms.enabled)
                                           ? destination_room->conference_status.breakout_rooms.name.c_str ()
                                           : "Main room";
    if (ImGui::BeginCombo ("Destination room", destination_room_name, ImGuiComboFlags_None)) {
      for (auto it = application->room_list.room_map.begin (); it != application->room_list.room_map.end (); it++) {
        const char * name = (it->second->conference_status.breakout_rooms.enabled)
                              ? it->second->conference_status.breakout_rooms.name.c_str ()
                              : "Main room";
        if (ImGui::Selectable (name, (destination_room_id == it->first))) {
          destination_room_id = it->first;
        }
      }
      ImGui::EndCombo ();
    }

    if (!is_ui_disabled_state && (destination_room_id == source_room_id || chosen_participants.size () == 0)) {
      is_ui_disabled_state = true;
      imgui_begin_disabled_state ();
    }

    if (ImGui::Button ("Move selected participants")) {
      PulseConferenceControlBreakoutsTransferRequest req;
      memset (&req, 0, sizeof (PulseConferenceControlBreakoutsTransferRequest));
      req.source_room_id = source_room_id;
      req.destination_room_id = destination_room_id;
      req.transfer_participants_num = chosen_participants.size ();
      req.transfer_participants = (const char **)calloc (req.transfer_participants_num, sizeof (const char *));

      int index = 0;
      std::set<const char *>::iterator it;
      for (it = chosen_participants.begin (); it != chosen_participants.end (); ++it) {
        const char * element = *it;
        req.transfer_participants[index++] = element;
      }

      pulse_conference_control_breakouts_transfer_participants (application->client, &req);
      application->state.breakout_rooms.popup_participant_move_flagged = false;
      ImGui::CloseCurrentPopup ();
    }
    if (is_ui_disabled_state)
      imgui_end_disabled_state ();

    ImGui::SameLine ();
    if (ImGui::Button ("Cancel")) {
      application->state.breakout_rooms.popup_participant_move_flagged = false;
      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_send_dtmf_sequence (PexNinja * application)
{
  if (application->state.popup_send_dtmf_sequence) {
    application->state.popup_send_dtmf_sequence = false;
    application->state.dtmf.digits = std::string ();
    ImGui::OpenPopup ("Send DTMF sequence");
  }

  if (ImGui::BeginPopupModal ("Send DTMF sequence", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->state.dtmf.send_to_recepient) {
      ImGui::Text ("recepient: %s\nuuid: %s", application->state.dtmf.participant_display_name.c_str (),
                   application->state.dtmf.participant_uuid.c_str ());
    }

    ImGui::SetItemDefaultFocus ();
    ImGui::InputText ("dtmf_digits", &application->state.dtmf.digits, ImGuiInputTextFlags_None);

    bool input_valid = (application->state.dtmf.digits.length () > 0);
    if (input_valid) {
      for (size_t i = 0; i < application->state.dtmf.digits.length (); i++) {
        char c = application->state.dtmf.digits.at (i);
        if (!isdigit ((int)c) && c != '*' && c != '#') {
          ImGui::TextColored (red_color, "Invalid DTMF token '%c' at position %zu!", c, i);
          break;
        }
      }
    }

    if (!input_valid)
      imgui_begin_disabled_state ();

    bool acknowledged = false;
    if (ImGui::Button ("Send")) {
      acknowledged = true;
    }

    if (!input_valid)
      imgui_end_disabled_state ();

    ImGui::SameLine ();
    if (ImGui::Button ("Cancel")) {
      ImGui::CloseCurrentPopup ();
    }

    if (acknowledged) {
      const char * recepient =
        (application->state.dtmf.send_to_recepient) ? application->state.dtmf.participant_uuid.c_str () : NULL;
      PulseError err =
        pulse_participant_control_dtmf (application->client, recepient, application->state.dtmf.digits.c_str ());
      if (err != PULSE_SUCCESS) {
        PEX_LOG_ERROR ("Failed to send DTMF to recepient '%s': %s", recepient, pulse_strerror (err));
      }
      ImGui::CloseCurrentPopup ();
    }
    ImGui::EndPopup ();
  }
}

static inline void
_configure_popup_disconnected (PexNinja * application)
{
  if (ImGui::BeginPopupModal ("Call disconnected", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (application->disconnected.popup_state == POPUP_STATE_HIDDEN) {
      ImGui::CloseCurrentPopup ();
    } else {
      ImGui::Text ("You were disconnected from the call: %s", application->disconnected.reason.c_str ());

      if (ImGui::Button ("Acknowledge")) {
        application->disconnected.popup_state = POPUP_STATE_HIDDEN;
        ImGui::CloseCurrentPopup ();
      }
      ImGui::SetItemDefaultFocus ();
    }
    ImGui::EndPopup ();
  }
}

static void
configure_popups (PexNinja * application)
{
  if (atomic_cas_int (&application->incoming_call.popup_state, POPUP_STATE_QUEUED,
                                         POPUP_STATE_SHOWING)) {
    ImGui::OpenPopup ("Incoming call");
  } else if (atomic_cas_int (&application->incoming_call_cancelled.popup_state, POPUP_STATE_QUEUED,
                                                POPUP_STATE_SHOWING)) {
    ImGui::OpenPopup ("Missed call");
  } else if (atomic_cas_int (&application->sso_provider.popup_state, POPUP_STATE_QUEUED,
                                                POPUP_STATE_SHOWING)) {
    ImGui::OpenPopup ("Authentication needed");
  } else if (atomic_cas_int (&application->pin_code_request.popup_state, POPUP_STATE_QUEUED,
                                                POPUP_STATE_SHOWING)) {
    ImGui::OpenPopup ("Pin code request");
  } else if (atomic_cas_int (&application->conference_extension_request.popup_state,
                                                POPUP_STATE_QUEUED, POPUP_STATE_SHOWING)) {
    ImGui::OpenPopup ("Conference extension request");
  } else if (atomic_cas_int (&application->tls_degrade.popup_state, POPUP_STATE_QUEUED,
                                                POPUP_STATE_SHOWING)) {
    ImGui::OpenPopup ("Accept TLS degrade");
  } else if (atomic_cas_int (&application->audio_unmute.popup_state, POPUP_STATE_QUEUED,
                                                POPUP_STATE_SHOWING)) {
    ImGui::OpenPopup ("Accept remote audio unmute request");
  } else if (atomic_cas_int (&application->referal_request.popup_state, POPUP_STATE_QUEUED,
                                                POPUP_STATE_SHOWING)) {
    ImGui::OpenPopup ("Referal request");
  } else if (atomic_cas_int (&application->disconnected.popup_state, POPUP_STATE_QUEUED,
                                                POPUP_STATE_SHOWING)) {
    ImGui::OpenPopup ("Call disconnected");
  }

  _configure_popup_incoming_call (application);
  _configure_popup_missed_call (application);
  _configure_popup_sso_provider_selection (application);
  _configure_popup_pin_code_request (application);
  _configure_popup_conference_extension_request (application);
  _configure_popup_tls_degrade (application);
  _configure_popup_audio_mute_request (application);
  _configure_popup_referal_request (application);
  _configure_popup_disconnected (application);

  /* This popup is handled a bit differently */
  _configure_popup_connect_error (application);

  /* These should be rewritten as windows, not pop-ups */
  _configure_popup_new_audio_mixer (application);
  _configure_popup_new_breakout_room (application);
  _configure_popup_participant_move (application);
  _configure_popup_send_dtmf_sequence (application);
}

#if defined(HOST_LINUX)

/* The original implementation decoded the PNG via a small GStreamer pipeline
 * (filesrc ! pngdec ! videoconvert ! appsink). GStreamer has been removed from
 * pexninja because it clashes with the copy statically linked into libpexlgpl,
 * so this is now a no-op: returning NULL simply leaves the window icon unset,
 * which set_window_icon() already handles gracefully. */
static uint8_t *
load_png (const char * filepath, int width, int height)
{
  (void) filepath;
  (void) width;
  (void) height;
  return NULL;
}

static void
set_window_icon (GLFWwindow * window, const char * filepath)
{
  uint8_t * data;
  GLFWimage img;

  memset (&img, 0, sizeof (GLFWimage));
  data = load_png (filepath, 128, 128);
  if (data) {
    img.width = 128;
    img.height = 128;
    img.pixels = (unsigned char *)data;
    glfwSetWindowIcon (window, 1, &img);
    free (data);
  }
}

#endif // defined (HOST_LINUX)

static void
setup_env ()
{
  /* Route Pulse's internal logging through our native logger (see
   * _logger_callback), which writes to stdout/stderr. Set PEXNINJA_LOG_FILE to
   * redirect it to a file instead. */
  FILE * log_file = nullptr;
  const char * log_path = getenv ("PEXNINJA_LOG_FILE");
  if (log_path != nullptr && log_path[0] != '\0')
    log_file = fopen (log_path, "a"); /* deliberately kept open for the process lifetime */

  pulse_global_logger_callback (_logger_callback, log_file);
}

void
pull_media_stats (PexNinja * application)
{
  if (application->state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
    static std::time_t t_last_pull = 0;
    std::time_t t_now = std::time (nullptr);
    if (t_last_pull < t_now) {
      PulseMediaStats * media_stats =
        pulse_media_stats_get (application->client, application->state.media_stats_window_secs, true);
      if (media_stats) {
        std::lock_guard<std::mutex> lock (application->state.media_stats_lock);

        // Free previous media_stats, and swap with the latest.
        if (application->state.media_stats) {
          pulse_media_stats_free (application->state.media_stats);
        }
        application->state.media_stats = media_stats;

        if (application->state.media_stats_entries.start_ts == 0) {
          application->state.media_stats_entries.start_ts = t_now;

          // Set inital size of entires to be able to hold 30 minutes (1800 samples) of stats history without any need
          // for reallocations.
          assert (application->state.media_stats_entries.entries.size () == 0);
          application->state.media_stats_entries.entries.reserve (30 * 60);
        }

        PexNinjaMediaStatsEntry s = {};
        s.rel_ts = t_now - application->state.media_stats_entries.start_ts;

        s.stats[STAT_SRC_AUDIO][STAT_DIR_RX][STATS_ENTRY_BITRATE] = media_stats->audio_rx.total_bitrate / 1000;
        s.stats[STAT_SRC_AUDIO][STAT_DIR_RX][STATS_ENTRY_PACKETLOSS] = media_stats->audio_rx.total_packets_actual_lost;
        s.stats[STAT_SRC_AUDIO][STAT_DIR_RX][STATS_ENTRY_JITTER] =
          (ImU64)(media_stats->audio_rx.total_jitter_ms * 1000);
        s.stats[STAT_SRC_AUDIO][STAT_DIR_RX][STATS_ENTRY_RTX_RECEIVED] = media_stats->audio_rx.total_rtx_received;
        s.stats[STAT_SRC_AUDIO][STAT_DIR_RX][STATS_ENTRY_RTX_SUCCESS] = media_stats->audio_rx.total_rtx_success;

        s.stats[STAT_SRC_VIDEO][STAT_DIR_RX][STATS_ENTRY_BITRATE] = media_stats->video_rx.total_bitrate / 1000;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_RX][STATS_ENTRY_PACKETLOSS] = media_stats->video_rx.total_packets_actual_lost;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_RX][STATS_ENTRY_JITTER] =
          (ImU64)(media_stats->video_rx.total_jitter_ms * 1000);
        s.stats[STAT_SRC_VIDEO][STAT_DIR_RX][STATS_ENTRY_RTX_RECEIVED] = media_stats->video_rx.total_rtx_received;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_RX][STATS_ENTRY_RTX_SUCCESS] = media_stats->video_rx.total_rtx_success;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_RX][STATS_ENTRY_RESOLUTION_X] = media_stats->video_rx.width;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_RX][STATS_ENTRY_RESOLUTION_Y] = media_stats->video_rx.height;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_RX][STATS_ENTRY_FPS] = (ImU64)media_stats->video_rx.framerate;

        s.stats[STAT_SRC_SLIDES][STAT_DIR_RX][STATS_ENTRY_BITRATE] = media_stats->slides_rx.total_bitrate / 1000;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_RX][STATS_ENTRY_PACKETLOSS] =
          media_stats->slides_rx.total_packets_actual_lost;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_RX][STATS_ENTRY_JITTER] =
          (ImU64)(media_stats->slides_rx.total_jitter_ms * 1000);
        s.stats[STAT_SRC_SLIDES][STAT_DIR_RX][STATS_ENTRY_RTX_RECEIVED] = media_stats->slides_rx.total_rtx_received;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_RX][STATS_ENTRY_RTX_SUCCESS] = media_stats->slides_rx.total_rtx_success;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_RX][STATS_ENTRY_RESOLUTION_X] = media_stats->slides_rx.width;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_RX][STATS_ENTRY_RESOLUTION_Y] = media_stats->slides_rx.height;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_RX][STATS_ENTRY_FPS] = (ImU64)media_stats->slides_rx.framerate;

        s.stats[STAT_SRC_AUDIO][STAT_DIR_TX][STATS_ENTRY_BITRATE] = media_stats->audio_tx.total_bitrate / 1000;
        s.stats[STAT_SRC_AUDIO][STAT_DIR_TX][STATS_ENTRY_PACKETLOSS] = media_stats->audio_tx.total_packets_lost;
        s.stats[STAT_SRC_AUDIO][STAT_DIR_TX][STATS_ENTRY_JITTER] =
          (ImU64)(media_stats->audio_tx.total_jitter_ms * 1000);

        s.stats[STAT_SRC_VIDEO][STAT_DIR_TX][STATS_ENTRY_BITRATE] = media_stats->video_tx.total_bitrate / 1000;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_TX][STATS_ENTRY_PACKETLOSS] = media_stats->video_tx.total_packets_lost;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_TX][STATS_ENTRY_JITTER] =
          (ImU64)(media_stats->video_tx.total_jitter_ms * 1000);
        s.stats[STAT_SRC_VIDEO][STAT_DIR_TX][STATS_ENTRY_RESOLUTION_X] = media_stats->video_tx.width;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_TX][STATS_ENTRY_RESOLUTION_Y] = media_stats->video_tx.height;
        s.stats[STAT_SRC_VIDEO][STAT_DIR_TX][STATS_ENTRY_FPS] = (ImU64)media_stats->video_tx.framerate;

        s.stats[STAT_SRC_SLIDES][STAT_DIR_TX][STATS_ENTRY_BITRATE] = media_stats->slides_tx.total_bitrate / 1000;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_TX][STATS_ENTRY_PACKETLOSS] = media_stats->slides_tx.total_packets_lost;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_TX][STATS_ENTRY_JITTER] =
          (ImU64)(media_stats->slides_tx.total_jitter_ms * 1000);
        s.stats[STAT_SRC_SLIDES][STAT_DIR_TX][STATS_ENTRY_RESOLUTION_X] = media_stats->slides_tx.width;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_TX][STATS_ENTRY_RESOLUTION_Y] = media_stats->slides_tx.height;
        s.stats[STAT_SRC_SLIDES][STAT_DIR_TX][STATS_ENTRY_FPS] = (ImU64)media_stats->slides_tx.framerate;

        for (int ss = 0; ss < __STAT_SRC_MAX__; ss++)
          for (int sd = 0; sd < __STATS_DIR_MAX__; sd++)
            for (int se = 0; se < __STATS_ENTRY_MAX__; se++)
              if (s.stats[ss][sd][se])
                application->state.media_stats_entries.has_stats[ss][sd][se] = true;

        application->state.media_stats_entries.entries.push_back (s);

        t_last_pull = t_now;
      }
    }
  } else {
    std::lock_guard<std::mutex> lock (application->state.media_stats_lock);
    if (application->state.media_stats != NULL) {
      pulse_media_stats_free (application->state.media_stats);
      application->state.media_stats = NULL;

      application->state.media_stats_entries.start_ts = 0;

      // Force a cleanup to guarantee reallocation.
      std::vector<PexNinjaMediaStatsEntry> entries;
      application->state.media_stats_entries.entries.swap (entries);
    }
  }
}

static void
setup_pulse_options (PexNinja * application)
{
  pulse_options_set_tls_hostname_verification (application->client,
                                               !application->config.options.disable_tls_hostname_verification);
  pulse_options_set_tls_peer_verification (application->client,
                                           !application->config.options.disable_tls_peer_verification);
  pulse_options_set_stun_server_support (application->client, !application->config.options.disable_stun_server_support);
  pulse_options_set_turn_server_support (application->client, !application->config.options.disable_turn_server_support);
  pulse_options_set_turn_443_server_support (application->client,
                                             !application->config.options.disable_turn_443_server_support);
  pulse_options_set_allow_direct_fqdn_connect (application->client,
                                               application->config.options.allow_direct_fqdn_connection);
  pulse_options_set_allow_direct_ip_connect (application->client,
                                             application->config.options.allow_direct_ip_connection);
  pulse_options_set_fecc_mode (application->client, application->config.options.enable_fecc_support);
  pulse_options_set_direct_media_supported (application->client,
                                            application->config.options.enable_direct_media_support);
  pulse_options_set_automatic_gain_control (application->client, application->config.options.enable_agc);

  _configure_proxy_server (application);
}

static void
setup_pulse_callbacks (PexNinja * application, Pulse * client)
{
  PulseVersionCallbackConfig version_callback_config = {
    .func = _version_callback,
    .user_context = application,
  };

  PulseConferenceStatusCallbackConfig conference_status_callback_config = {
    .func = _conference_status_info_callback,
    .user_context = application,
  };

  PulseRegistrationStatusCallbackConfig registration_status_callback_config = {
    .func = _registration_status_info_callback,
    .user_context = application,
  };

  PulseNetworkStatusCallbackConfig network_status_callback_config = {
    .func = _network_status_info_callback,
    .user_context = application,
  };

  PulseTLSDegradeApprovalCallbackConfig tls_degrade_config = {.func = _tls_degrade_approval_callback,
                                                              .user_context = application};
  PulsePinCodeRequestCallbackConfig pin_code_request_config = {.func = _pin_code_request_callback,
                                                               .user_context = application};

  PulseConferenceExtensionRequestCallbackConfig conference_extension_request_config = {
    .func = _conference_extension_request_callback, .user_context = application};

#if defined(HOST_WINDOWS)
  PulseSSOProviderCallbackConfig sso_config = {
    .selection_callback = _sso_selection_callback,
    .selection_callback_user_context = application,
    .request_callback = _sso_request_callback,
    .request_callback_user_context = application,
  };

  assert (pulse_options_set_sso_provider_callbacks (client, &sso_config) == PULSE_SUCCESS);
#endif
  PulseStorageCallbackConfig storage_callback_config = {
    .set_data_callback = _pexninja_storage_set_callback,
    .set_data_callback_user_context = application,
    .get_data_callback = _pexninja_storage_get_callback,
    .get_data_callback_user_context = application,
  };
  assert (pulse_options_set_storage_callbacks (client, &storage_callback_config) == PULSE_SUCCESS);

  assert (pulse_options_set_application_user_agent_string (client, "PexNinja/" VERSION) == PULSE_SUCCESS);

  assert (pulse_options_set_version_callback (client, &version_callback_config) == PULSE_SUCCESS);

  assert (pulse_options_set_conference_state_callback (client, &conference_status_callback_config) == PULSE_SUCCESS);

  assert (pulse_options_set_registration_state_callback (client, &registration_status_callback_config) ==
          PULSE_SUCCESS);

  assert (pulse_options_set_network_state_callback (client, &network_status_callback_config) == PULSE_SUCCESS);

  assert (pulse_options_set_conference_event_message_received_callback (client, _server_event_message_received_callback,
                                                                        (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_conference_update_callback (
            client, _server_event_conference_update_callback, (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_participant_list_updated_callback (
            client, _server_event_participant_update_callback, (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_participant_create_callback (
            client, _server_event_participant_create_callback, (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_participant_delete_callback (
            client, _server_event_participant_delete_callback, (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_remote_disconnect_callback (
            client, _server_event_remote_disconnect_callback, (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_presentation_start_callback (
            client, _server_event_presentation_start_callback, (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_presentation_stop_callback (
            client, _server_event_presentation_stop_callback, (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_layout_callback (client, _server_event_layout_callback,
                                                              (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_stage_callback (client, _server_event_stage_callback,
                                                             (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_audio_mixer_list_callback (client, _server_event_audio_mixer_list_callback,
                                                                        (void *)application) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_live_captions_callback (client, _server_event_live_captions_callback,
                                                                     (void *)application) == PULSE_SUCCESS);

  assert (pulse_options_set_tls_degrade_approval_callback (client, &tls_degrade_config) == PULSE_SUCCESS);

  assert (pulse_options_set_pin_code_request_callbacks (client, &pin_code_request_config) == PULSE_SUCCESS);

  assert (pulse_options_set_conference_extension_request_callback (client, &conference_extension_request_config) ==
          PULSE_SUCCESS);

  assert (pulse_options_set_audio_unmute_approval_callback (client, _audio_unmute_approval_callback, application) ==
          PULSE_SUCCESS);
  assert (pulse_options_set_audio_mute_state_changed_callback (client, _audio_mute_state_changed_callback,
                                                               application) == PULSE_SUCCESS);

  assert (pulse_options_set_breakout_room_pre_transfer_callback (
            client, _server_event_breakout_room_pre_transfer_callback, application) == PULSE_SUCCESS);
  assert (pulse_options_set_breakout_room_post_transfer_callback (
            client, _server_event_breakout_room_post_transfer_callback, application) == PULSE_SUCCESS);
  assert (pulse_options_set_breakout_room_transfer_cancelled_callback (
            client, _server_event_breakout_room_transfer_cancelled_callback, application) == PULSE_SUCCESS);
  assert (pulse_options_set_breakout_room_created_callback (client, _server_event_breakout_room_created_callback,
                                                            application) == PULSE_SUCCESS);
  assert (pulse_options_set_breakout_room_destroyed_callback (client, _server_event_breakout_room_destroyed_callback,
                                                              application) == PULSE_SUCCESS);

  assert (pulse_options_set_conference_event_fecc_callback (client, _server_event_fecc_callback, application) ==
          PULSE_SUCCESS);
}

static void
clear_pulse_callbacks (Pulse * client)
{
  assert (pulse_deregister_device_list_changed_callback (client, PULSE_MEDIA_VIDEO) == PULSE_SUCCESS);
  assert (pulse_deregister_device_list_changed_callback (client, PULSE_MEDIA_AUDIO) == PULSE_SUCCESS);

  assert (pulse_options_set_sso_provider_callbacks (client, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_storage_callbacks (client, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_version_callback (client, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_state_callback (client, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_registration_state_callback (client, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_network_state_callback (client, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_message_received_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_conference_update_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_participant_list_updated_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_participant_create_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_participant_delete_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_remote_disconnect_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_presentation_start_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_presentation_stop_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_layout_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_stage_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_audio_mixer_list_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_live_captions_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_tls_degrade_approval_callback (client, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_pin_code_request_callbacks (client, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_extension_request_callback (client, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_audio_unmute_approval_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_audio_mute_state_changed_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_breakout_room_pre_transfer_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_breakout_room_post_transfer_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_breakout_room_transfer_cancelled_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_breakout_room_created_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_breakout_room_destroyed_callback (client, NULL, NULL) == PULSE_SUCCESS);
  assert (pulse_options_set_conference_event_fecc_callback (client, NULL, NULL) == PULSE_SUCCESS);
}

static void
get_video_mix_input_from_device (PexNinja * application, PexNinjaState::PexNinjaVideoMix & vm, PulseDevice * device)
{
  PulseError err = pulse_video_mix_input_from_device (application->client, device, &vm.camera_input);
  if (err != PULSE_SUCCESS) {
    PEX_LOG_WARNING ("Failed to acquire camera input: %s", pulse_strerror (err));
    vm.camera_input = PULSE_VIDEO_MIX_INPUT_ID_NONE;
    return;
  }
}
static void
connect_selected_devices (PexNinja * application, Pulse * client)
{
  std::lock_guard<std::mutex> lock (devices_mutex);
  camera_devices = enumerate_devices (client, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT);
  speaker_devices = enumerate_devices (client, PULSE_MEDIA_AUDIO, PULSE_MEDIA_OUTPUT);
  microphone_devices = enumerate_devices (client, PULSE_MEDIA_AUDIO, PULSE_MEDIA_INPUT);

  static int selected_cam_idx = 0;
  if (application->config.devices.camera_set) {
    int max = (int)camera_devices.size ();
    for (int i = 0; i < max; i++) {
      if (pulse_device_get_id (camera_devices[i]) == application->config.devices.camera_id) {
        selected_cam_idx = i;
      }
    }
  }

  static int selected_speaker_idx = 0;
  if (application->config.devices.speaker_set) {
    int max = (int)speaker_devices.size ();
    for (int i = 0; i < max; i++) {
      if (pulse_device_get_id (speaker_devices[i]) == application->config.devices.speaker_id) {
        selected_speaker_idx = i;
      }
    }
  }

  static int selected_microphone_idx = 0;
  if (application->config.devices.microphone_set) {
    int max = (int)microphone_devices.size ();
    for (int i = 0; i < max; i++) {
      if (pulse_device_get_id (microphone_devices[i]) == application->config.devices.microphone_id) {
        selected_microphone_idx = i;
      }
    }
  }

  if (!camera_devices.empty ()) {
    pulse_device_session_connect_device (client, camera_devices[selected_cam_idx], PULSE_MEDIA_CONTENT_MAIN);
    auto & vm = application->state.video_mix;
    auto & pm = application->state.preso_mix;
    get_video_mix_input_from_device (application, vm, camera_devices[selected_cam_idx]);
    get_video_mix_input_from_device (application, pm, camera_devices[selected_cam_idx]);
  }

  if (application->config.devices.speaker_use_default) {
    pulse_device_session_connect_system_default (client, PULSE_MEDIA_CONTENT_MAIN, PULSE_MEDIA_AUDIO,
                                                 PULSE_MEDIA_OUTPUT);
  } else if (!speaker_devices.empty ()) {
    pulse_device_session_connect_device (client, speaker_devices[selected_speaker_idx], PULSE_MEDIA_CONTENT_MAIN);
  }

  if (application->config.devices.microphone_use_default) {
    pulse_device_session_connect_system_default (client, PULSE_MEDIA_CONTENT_MAIN, PULSE_MEDIA_AUDIO,
                                                 PULSE_MEDIA_INPUT);
  } else if (!microphone_devices.empty ()) {
    pulse_device_session_connect_device (client, microphone_devices[selected_microphone_idx], PULSE_MEDIA_CONTENT_MAIN);
  }
}

void
update_color_shift (void)
{
  static bool shift_rotation = true;
  static float shift_value = 0;
  shift_value += (shift_rotation) ? 0.1f : -0.1f;
  if (shift_value <= 0.0f || shift_value >= 1.0f)
    shift_rotation = !shift_rotation;

  yellow_red_shift = ImVec4 (1.0f, shift_value, 0.0f, 1.00f);
  yellow_green_shift = ImVec4 (shift_value, 1.0f, 0.0f, 1.00f);
  black_to_white_shift = ImVec4 (shift_value, shift_value, shift_value, 1.00f);
  white_to_black_shift = ImVec4 (1.0f - shift_value, 1.0f - shift_value, 1.0f - shift_value, 1.00f);
}

void
update_fade_alpha (void)
{
  static ImVec2 last_mouse_pos = ImGui::GetMousePos ();
  static float fade_timer = 0.0f;

  ImGuiIO & io = ImGui::GetIO ();
  ImVec2 cur_mouse_pos = io.MousePos;

  bool mouse_clicked = ImGui::IsMouseClicked (ImGuiMouseButton_Left) ||
                       ImGui::IsMouseClicked (ImGuiMouseButton_Right) ||
                       ImGui::IsMouseClicked (ImGuiMouseButton_Middle);

  if (cur_mouse_pos.x != last_mouse_pos.x || cur_mouse_pos.y != last_mouse_pos.y || mouse_clicked) {
    fade_timer = 0.0f;
    fade_alpha = 1.0f;
    last_mouse_pos = cur_mouse_pos;
  } else {
    fade_timer += io.DeltaTime;
  }

  const float fade_delay = 1.0f;
  const float fade_duration = 2.0f;

  if (fade_timer > fade_delay) {
    float elapsed = fade_timer - fade_delay;
    fade_alpha = 1.0f - (elapsed / fade_duration);
    if (fade_alpha < 0.2f)
      fade_alpha = 0.2f;
    else if (fade_alpha > 1.0f)
      fade_alpha = 1.0f;
  }
}

static void
register_alarm (PexNinja * application, const char * msg, uint32_t display_seconds, ImVec4 color)
{
  if (!msg)
    return;

  std::lock_guard<std::mutex> lock (application->alarms.mutex);

  /* If an alarm with the same message already exists, remove it first */
  auto it = application->alarms.lookup.find (msg);
  if (it != application->alarms.lookup.end ()) {
    application->alarms.entries.erase (it->second);
    application->alarms.lookup.erase (it);
  }

  AlarmEntry entry;
  entry.msg = msg;
  entry.color = color;
  if (display_seconds > 0) {
    entry.expires = true;
    entry.valid_until = std::chrono::steady_clock::now () + std::chrono::seconds (display_seconds);
  } else {
    entry.expires = false;
    entry.valid_until = {};
  }

  application->alarms.entries.push_back (std::move (entry));
  auto list_it = std::prev (application->alarms.entries.end ());
  application->alarms.lookup[msg] = list_it;
}

static bool
cancel_alarm (PexNinja * application, const char * msg, uint32_t display_seconds)
{
  if (!msg)
    return false;

  std::lock_guard<std::mutex> lock (application->alarms.mutex);

  auto it = application->alarms.lookup.find (msg);
  if (it == application->alarms.lookup.end ())
    return false;

  if (display_seconds > 0) {
    it->second->expires = true;
    it->second->valid_until = std::chrono::steady_clock::now () + std::chrono::seconds (display_seconds);
  } else {
    application->alarms.entries.erase (it->second);
    application->alarms.lookup.erase (it);
  }

  return true;
}

static void
draw_overlay_alarms (PexNinja * application)
{
  std::lock_guard<std::mutex> lock (application->alarms.mutex);

  if (application->alarms.entries.empty ())
    return;

  auto now = std::chrono::steady_clock::now ();

  /* Remove expired entries */
  for (auto it = application->alarms.entries.begin (); it != application->alarms.entries.end ();) {
    if (it->expires && it->valid_until < now) {
      application->alarms.lookup.erase (it->msg);
      it = application->alarms.entries.erase (it);
    } else {
      ++it;
    }
  }

  if (application->alarms.entries.empty ())
    return;

  ImGuiViewport * viewport = ImGui::GetMainViewport ();
  ImDrawList * draw_list = ImGui::GetForegroundDrawList ();
  ImFont * font = ImGui::GetFont ();
  float font_size = font->FontSize;

  float padding_x = 10.0f;
  float padding_y = 4.0f;
  float rounding = 8.0f;
  float spacing = 4.0f;
  float start_y = viewport->Pos.y + (float)mainmenubar_height + 8.0f;

  /* Calculate the widest entry to use a uniform background width */
  float max_text_w = 0.0f;
  for (auto & entry : application->alarms.entries) {
    ImVec2 text_size = font->CalcTextSizeA (font_size, FLT_MAX, 0.0f, entry.msg.c_str ());
    if (text_size.x > max_text_w)
      max_text_w = text_size.x;
  }

  float bg_w = max_text_w + padding_x * 2.0f;
  float bg_h = font_size + padding_y * 2.0f;
  float centre_x = viewport->Pos.x + viewport->Size.x * 0.5f;

  const float alpha = 0.7f;
  ImU32 bg_color = IM_COL32 (0, 0, 0, (int)(alpha * 255));

  /* Collect messages to cancel on click (outside the loop to avoid modifying while iterating) */
  std::vector<std::string> clicked;

  float y = start_y;
  for (auto & entry : application->alarms.entries) {
    float x0 = centre_x - bg_w * 0.5f;
    float y0 = y;
    float x1 = x0 + bg_w;
    float y1 = y0 + bg_h;

    draw_list->AddRectFilled (ImVec2 (x0, y0), ImVec2 (x1, y1), bg_color, rounding);

    ImVec2 text_size = font->CalcTextSizeA (font_size, FLT_MAX, 0.0f, entry.msg.c_str ());
    float text_x = x0 + (bg_w - text_size.x) * 0.5f;
    float text_y = y0 + padding_y;
    ImU32 text_color = ImGui::ColorConvertFloat4ToU32 (entry.color);
    draw_list->AddText (ImVec2 (text_x, text_y), text_color, entry.msg.c_str ());

    /* Check for click to dismiss */
    ImVec2 mouse_pos = ImGui::GetMousePos ();
    if (ImGui::IsMouseClicked (ImGuiMouseButton_Left) && mouse_pos.x >= x0 && mouse_pos.x <= x1 && mouse_pos.y >= y0 &&
        mouse_pos.y <= y1) {
      clicked.push_back (entry.msg);
    }

    y += bg_h + spacing;
  }

  /* Remove clicked entries */
  for (auto & msg : clicked) {
    auto it = application->alarms.lookup.find (msg);
    if (it != application->alarms.lookup.end ()) {
      application->alarms.entries.erase (it->second);
      application->alarms.lookup.erase (it);
    }
  }
}

static void
draw_overlay_presentation_info (PexNinja * application)
{
  if (application->state.async_op.op != ASYNC_OP_NONE ||
      application->state.conn_status != PULSE_CONNECTION_STATUS_CONNECTED) {
    return;
  }

  /* Hide entirely once sufficiently faded */
  if (fade_alpha < 0.3f)
    return;

  std::lock_guard<std::mutex> lock (application->room_list.mutex);
  if (!application->room_list.current_room->presentation.preso_started)
    return;

  /* Helper: scale the alpha channel of a colour by the fade factor */
  auto fade_col = [&] (ImU32 col) -> ImU32 {
    int a = (int)((float)((col >> 24) & 0xFF) * fade_alpha);
    return (col & 0x00FFFFFF) | ((ImU32)a << 24);
  };

  ImGuiViewport * viewport = ImGui::GetMainViewport ();
  ImDrawList * draw_list = ImGui::GetBackgroundDrawList ();
  ImFont * font = ImGui::GetFont ();
  float font_size = font->FontSize;

  /* Same dimensions and spacing as the overlay menu box (fixed size) */
  float menu_size = 128.0f;
  float margin = 8.0f;
  float bar_h = (float)bottombar_height;
  float rounding = 8.0f;

  /* Mirror position: same distance from left edge as the menu box is from the right */
  float x0 = viewport->Pos.x + margin;
  float y0 = viewport->Pos.y + viewport->Size.y - bar_h - menu_size - margin;
  float x1 = x0 + menu_size;
  float y1 = y0 + menu_size;

  /* Semi-transparent white background – same as overlay menu */
  ImU32 bg_color = fade_col (IM_COL32 (255, 255, 255, (int)(0.7f * 255)));
  draw_list->AddRectFilled (ImVec2 (x0, y0), ImVec2 (x1, y1), bg_color, rounding);

  /* 1-pixel black border at the same transparency level as the background */
  ImU32 border_color = fade_col (IM_COL32 (0, 0, 0, (int)(0.7f * 255)));
  draw_list->AddRect (ImVec2 (x0, y0), ImVec2 (x1, y1), border_color, rounding, 0, 1.0f);

  /* Inner layout – identical to overlay menu: two rows with padding */
  float inner_pad = menu_size * 0.12f;
  float row_gap = inner_pad * 0.5f;
  float content_w = menu_size - inner_pad * 2.0f;
  float row_h = (menu_size - inner_pad * 2.0f - row_gap) * 0.5f;

  float row1_y = y0 + inner_pad;
  float row2_y = row1_y + row_h + row_gap;
  float col1_x = x0 + inner_pad;

  /* Build the presenter text */
  char info_text[256];
  snprintf (info_text, sizeof (info_text), "%s is presenting",
            application->room_list.current_room->presentation.presenter_name);

  /* --- Top area: presenter name text (black, word-wrapped) --- */
  {
    ImVec2 text_size = font->CalcTextSizeA (font_size, FLT_MAX, content_w, info_text);
    ImU32 text_color = fade_col (IM_COL32 (30, 30, 30, 255));

    /* Centre the (possibly multi-line) text block in the top row */
    float tx = col1_x + (content_w - text_size.x) * 0.5f;
    float ty = row1_y + (row_h - text_size.y) * 0.5f;
    draw_list->AddText (font, font_size, ImVec2 (tx, ty), text_color, info_text, nullptr, content_w);
  }

  /* --- Bottom row: toggle button (white on black, same placement as Leave) --- */
  {
    float bx0 = col1_x;
    float by0 = row2_y;
    float bx1 = bx0 + content_w;
    float by1 = by0 + row_h;

    ImU32 btn_color = fade_col (IM_COL32 (30, 30, 30, 255));
    draw_list->AddRectFilled (ImVec2 (bx0, by0), ImVec2 (bx1, by1), btn_color, 6.0f);

    const char * line1 = "Show";
    const char * line2 = application->room_list.current_room->presentation.preso_render ? "main-video" : "presentation";
    ImVec2 l1_size = font->CalcTextSizeA (font_size, FLT_MAX, 0.0f, line1);
    ImVec2 l2_size = font->CalcTextSizeA (font_size, FLT_MAX, 0.0f, line2);
    float total_h = l1_size.y + l2_size.y;
    ImU32 white = fade_col (IM_COL32 (255, 255, 255, 255));

    float base_y = by0 + (row_h - total_h) * 0.5f;
    draw_list->AddText (ImVec2 (bx0 + (content_w - l1_size.x) * 0.5f, base_y), white, line1);
    draw_list->AddText (ImVec2 (bx0 + (content_w - l2_size.x) * 0.5f, base_y + l1_size.y), white, line2);

    /* Click handling */
    ImVec2 mouse_pos = ImGui::GetMousePos ();
    if (ImGui::IsMouseClicked (ImGuiMouseButton_Left) && mouse_pos.x >= bx0 && mouse_pos.x <= bx1 &&
        mouse_pos.y >= by0 && mouse_pos.y <= by1) {
      application->room_list.current_room->presentation.preso_render =
        !application->room_list.current_room->presentation.preso_render;
    }
  }
}

static void
draw_overlay_menu (PexNinja * application)
{
  if (application->state.conn_status != PULSE_CONNECTION_STATUS_CONNECTED)
    return;

  /* Hide menu entirely once sufficiently faded */
  if (fade_alpha < 0.3f)
    return;

  /* Helper: scale the alpha channel of a colour by the fade factor */
  auto fade_col = [&] (ImU32 col) -> ImU32 {
    int a = (int)((float)((col >> 24) & 0xFF) * fade_alpha);
    return (col & 0x00FFFFFF) | ((ImU32)a << 24);
  };

  ImGuiViewport * viewport = ImGui::GetMainViewport ();
  ImDrawList * draw_list = ImGui::GetBackgroundDrawList ();

  /* Menu dimensions: fixed-size square (matches default 1280-wide window) */
  float menu_size = 128.0f;
  float margin = 8.0f;
  float bar_h = (float)bottombar_height;

  /* Position: bottom-right corner, inset from edge and above bottom bar */
  float x0 = viewport->Pos.x + viewport->Size.x - menu_size - margin;
  float y0 = viewport->Pos.y + viewport->Size.y - bar_h - menu_size - margin;
  float x1 = x0 + menu_size;
  float y1 = y0 + menu_size;

  /* Semi-transparent white background */
  float rounding = 8.0f;
  ImU32 bg_color = fade_col (IM_COL32 (255, 255, 255, (int)(0.7f * 255)));
  draw_list->AddRectFilled (ImVec2 (x0, y0), ImVec2 (x1, y1), bg_color, rounding);

  /* 1-pixel black border at the same transparency level as the background */
  ImU32 border_color = fade_col (IM_COL32 (0, 0, 0, (int)(0.7f * 255)));
  draw_list->AddRect (ImVec2 (x0, y0), ImVec2 (x1, y1), border_color, rounding, 0, 1.0f);

  /* Layout: two rows inside the square with some inner padding */
  float inner_pad = menu_size * 0.12f;
  float row_gap = inner_pad * 0.5f;
  float content_w = menu_size - inner_pad * 2.0f;
  float row_h = (menu_size - inner_pad * 2.0f - row_gap) * 0.5f;
  float btn_gap = inner_pad * 0.5f;
  float btn_w = (content_w - btn_gap) * 0.5f;

  float row1_y = y0 + inner_pad;
  float row2_y = row1_y + row_h + row_gap;
  float col1_x = x0 + inner_pad;
  float col2_x = col1_x + btn_w + btn_gap;

  ImVec2 mouse_pos = ImGui::GetMousePos ();
  bool clicked = ImGui::IsMouseClicked (ImGuiMouseButton_Left);
  ImFont * font = ImGui::GetFont ();
  float font_size = font->FontSize;

  /* --- Top row: Audio mute button (microphone icon) --- */
  {
    float bx0 = col1_x;
    float by0 = row1_y;
    float bx1 = bx0 + btn_w;
    float by1 = by0 + row_h;

    bool muted = application->state.audio_mute;
    ImU32 btn_color;
    ImU32 icon_color;
    if (muted && application->state.audio_temporarily_unmuted) {
      btn_color = fade_col (ImGui::ColorConvertFloat4ToU32 (black_to_white_shift));
      icon_color = fade_col (ImGui::ColorConvertFloat4ToU32 (white_to_black_shift));
    } else if (muted) {
      btn_color = fade_col (IM_COL32 (255, 255, 255, 255));
      icon_color = fade_col (IM_COL32 (30, 30, 30, 255));
    } else {
      btn_color = fade_col (IM_COL32 (30, 30, 30, 255));
      icon_color = fade_col (IM_COL32 (255, 255, 255, 255));
    }

    draw_list->AddRectFilled (ImVec2 (bx0, by0), ImVec2 (bx1, by1), btn_color, 6.0f);

    /* Microphone icon centred in button */
    float cx = (bx0 + bx1) * 0.5f;
    float cy = (by0 + by1) * 0.5f;
    float s = ImMin (btn_w, row_h) * 0.11f;

    /* Capsule body */
    float cap_w = s * 1.4f;
    float cap_h = s * 2.4f;
    float cap_top = cy - s * 1.8f;
    draw_list->AddRectFilled (ImVec2 (cx - cap_w * 0.5f, cap_top), ImVec2 (cx + cap_w * 0.5f, cap_top + cap_h),
                              icon_color, cap_w * 0.5f);

    /* U-shaped arc around capsule */
    float arc_cy = cap_top + cap_h * 0.55f;
    float arc_r = s * 1.4f;
    draw_list->PathArcTo (ImVec2 (cx, arc_cy), arc_r, 0.0f, IM_PI, 16);
    draw_list->PathStroke (icon_color, ImDrawFlags_None, s * 0.25f);

    /* Vertical stem */
    float stem_top = arc_cy + arc_r;
    float stem_bot = stem_top + s * 0.8f;
    draw_list->AddLine (ImVec2 (cx, stem_top), ImVec2 (cx, stem_bot), icon_color, s * 0.25f);

    /* Horizontal base */
    draw_list->AddLine (ImVec2 (cx - s * 0.7f, stem_bot), ImVec2 (cx + s * 0.7f, stem_bot), icon_color, s * 0.25f);

    /* Diagonal strike-through when muted */
    if (muted) {
      ImU32 strike_color = fade_col (IM_COL32 (200, 50, 50, 255));
      draw_list->AddLine (ImVec2 (bx0 + btn_w * 0.25f, by0 + row_h * 0.2f),
                          ImVec2 (bx0 + btn_w * 0.75f, by0 + row_h * 0.8f), strike_color, s * 0.3f);
    }

    /* Click handling */
    if (clicked && mouse_pos.x >= bx0 && mouse_pos.x <= bx1 && mouse_pos.y >= by0 && mouse_pos.y <= by1) {
      application->state.audio_mute = !application->state.audio_mute;
      pulse_mute_audio_input (application->client, application->state.audio_mute);
    }
  }

  /* --- Top row: Video mute button (camera icon) --- */
  {
    float bx0 = col2_x;
    float by0 = row1_y;
    float bx1 = bx0 + btn_w;
    float by1 = by0 + row_h;

    bool muted = application->state.video_mute;
    ImU32 btn_color = muted ? fade_col (IM_COL32 (255, 255, 255, 255)) : fade_col (IM_COL32 (30, 30, 30, 255));
    ImU32 icon_color = muted ? fade_col (IM_COL32 (30, 30, 30, 255)) : fade_col (IM_COL32 (255, 255, 255, 255));

    draw_list->AddRectFilled (ImVec2 (bx0, by0), ImVec2 (bx1, by1), btn_color, 6.0f);

    /* Camera icon centred in button */
    float cx = (bx0 + bx1) * 0.5f;
    float cy = (by0 + by1) * 0.5f;
    float s = ImMin (btn_w, row_h) * 0.11f;

    /* Camera body (rounded rectangle) */
    float body_w = s * 2.8f;
    float body_h = s * 2.0f;
    float body_x = cx - body_w * 0.5f - s * 0.35f;
    draw_list->AddRectFilled (ImVec2 (body_x, cy - body_h * 0.5f), ImVec2 (body_x + body_w, cy + body_h * 0.5f),
                              icon_color, 3.0f);

    /* Viewfinder triangle on right side */
    float tri_left = body_x + body_w + s * 0.15f;
    float tri_right = tri_left + s * 1.3f;
    draw_list->AddTriangleFilled (ImVec2 (tri_left, cy - s * 0.8f), ImVec2 (tri_right, cy),
                                  ImVec2 (tri_left, cy + s * 0.8f), icon_color);

    /* Diagonal strike-through when muted */
    if (muted) {
      ImU32 strike_color = fade_col (IM_COL32 (200, 50, 50, 255));
      draw_list->AddLine (ImVec2 (bx0 + btn_w * 0.25f, by0 + row_h * 0.2f),
                          ImVec2 (bx0 + btn_w * 0.75f, by0 + row_h * 0.8f), strike_color, s * 0.3f);
    }

    /* Click handling */
    if (clicked && mouse_pos.x >= bx0 && mouse_pos.x <= bx1 && mouse_pos.y >= by0 && mouse_pos.y <= by1) {
      application->state.video_mute = !application->state.video_mute;
      pulse_mute_video_input (application->client, application->state.video_mute);
    }
  }

  /* --- Bottom row: Disconnect button (red, full width, phone icon + Leave) --- */
  {
    float bx0 = col1_x;
    float by0 = row2_y;
    float bx1 = bx0 + content_w;
    float by1 = by0 + row_h;

    ImU32 btn_color = fade_col (IM_COL32 (0xb8, 0x37, 0x37, 255));
    draw_list->AddRectFilled (ImVec2 (bx0, by0), ImVec2 (bx1, by1), btn_color, 6.0f);

    ImU32 white = fade_col (IM_COL32 (255, 255, 255, 255));

    /* Measure "Leave" text to centre icon + gap + text as a group */
    const char * label = "Leave";
    ImVec2 text_size = font->CalcTextSizeA (font_size, FLT_MAX, 0.0f, label);
    float s = font_size * 0.35f;
    float icon_w = s * 3.0f;
    float gap = s * 0.8f;
    float total_w = icon_w + gap + text_size.x;
    float start_x = bx0 + (content_w - total_w) * 0.5f;
    float text_y = by0 + (row_h - text_size.y) * 0.5f;

    /* Phone-down handset icon */
    float icx = start_x + icon_w * 0.5f;
    float icy = by0 + row_h * 0.5f;

    /* Earpiece (left) */
    draw_list->AddRectFilled (ImVec2 (icx - s * 1.3f, icy - s * 0.35f), ImVec2 (icx - s * 0.55f, icy + s * 0.5f), white,
                              2.0f);
    /* Mouthpiece (right) */
    draw_list->AddRectFilled (ImVec2 (icx + s * 0.55f, icy - s * 0.35f), ImVec2 (icx + s * 1.3f, icy + s * 0.5f), white,
                              2.0f);
    /* Connecting arc (curves upward) */
    draw_list->PathArcTo (ImVec2 (icx, icy + s * 0.3f), s * 0.95f, IM_PI, IM_PI * 2.0f, 12);
    draw_list->PathStroke (white, ImDrawFlags_None, s * 0.45f);

    /* "Leave" text */
    draw_list->AddText (ImVec2 (start_x + icon_w + gap, text_y), white, label);

    /* Click handling */
    if (clicked && mouse_pos.x >= bx0 && mouse_pos.x <= bx1 && mouse_pos.y >= by0 && mouse_pos.y <= by1) {
      if (application->state.async_op.op == ASYNC_OP_NONE) {
        application->state.error_msg = NULL;
        application->state.async_op = ASYNC_OP_DATA_INIT;

        PulseAsyncOperationResultCallbackConfig async_op_cb_config = {.func = _pulse_async_operation_result_cb,
                                                                      .user_context = &application->state.async_op};
        PulseOperationProgressCallbackConfig progress_config = {.func = _progress_callback_conference,
                                                                .user_context = application};

        PulseError err = pulse_disconnect_async (application->client, &async_op_cb_config, &progress_config);
        if (err != PULSE_SUCCESS) {
          PEX_LOG_DEBUG ("pulse_disconnect failed: %s\n", pulse_strerror (err));
        } else {
          application->state.async_op.op = ASYNC_OP_DISCONNECT;
        }
      }
    }
  }
}

int
main (int argc, const char ** argv)
{
  setup_env ();

  PexNinja application;

  application.state.conn_status = PULSE_CONNECTION_STATUS_DISCONNECTED;
  application.state.async_op = ASYNC_OP_DATA_INIT;
  application.state.windows.show_self_view = true;
  _update_conference_status_msg (&application, "application init");

#if defined(HOST_WINDOWS)
  if (argc >= 2 && pex_str_has_prefix (argv[1], "pexip-auth://")) {
    char * token = _pexninja_extract_sso_token (argv[1]);
    if (token) {
      if (pulse_ipc_write_line (PEXNINJA_SSO_IPC_NAME, token) != PULSE_SUCCESS) {
        // give a chance to see the error before closing
        getchar ();
      }
      free (token);
    }
    return 0;
  }

  Pulse * client = pulse_new ();
#else
  Pulse * client = pulse_new_with_internal_sso_handling (argc, argv, _sso_selection_callback, &application);
#endif

  application.client = client;

  application.config_file = get_config_file_name ();
  read_config (application.config_file, &application.config);
  _update_config_copy (&application);

  setup_pulse_options (&application);
  setup_pulse_callbacks (&application, client);

  g_glfw_app_context = &application;
  application.window = create_window (&application);

#if defined(HOST_LINUX)
  if (getenv ("RUNNING_FROM_LAUNCHER"))
    set_window_icon (application.window, "/opt/pexninja/img/pexninja.png");
#elif defined(HOST_WINDOWS)
  {
    HRESULT hr = _pexninja_register_app_url_handler (argv[0]);
    if (!SUCCEEDED (hr)) {
      PEX_LOG_ERROR ("app-scheme registration failed with error: %d. SSO handling will not be possible", hr);
    }
  }
#endif

  connect_selected_devices (&application, client);

  pulse_register_device_list_changed_callback (client, PULSE_MEDIA_VIDEO, on_devices_changed, client);
  pulse_register_device_list_changed_callback (client, PULSE_MEDIA_AUDIO, on_devices_changed, client);

  application.state.mic_audio_levels = new AudioLevelQueue ();

  pulse_set_max_bitrate (client, DEFAULT_TX_KBPS * 1000);
  pulse_options_set_direct_chat_supported (client, true);

  PulseDataSessionConfig * config;
  GLTextureContext mainvideo_ctx;
  config = pulse_data_session_config_video_new ();
  init_gl_texture_ctx (&mainvideo_ctx, PULSE_MEDIA_CONTENT_MAIN);
  pulse_data_session_connect_output (client, config, PULSE_MEDIA_CONTENT_MAIN);
  pulse_data_session_config_free (config);

  GLTextureContext selfview_ctx;
  config = pulse_data_session_config_video_new ();
  init_gl_texture_ctx (&selfview_ctx, PULSE_MEDIA_CONTENT_SELFVIEW);
  pulse_data_session_connect_output (client, config, PULSE_MEDIA_CONTENT_SELFVIEW);
  pulse_data_session_config_free (config);

  GLTextureContext preflight_ctx;
  init_gl_texture_ctx (&preflight_ctx, PULSE_MEDIA_CONTENT_PREFLIGHT);

  GLTextureContext preso_ctx;
  config = pulse_data_session_config_video_new ();
  init_gl_texture_ctx (&preso_ctx, PULSE_MEDIA_CONTENT_PRESENTATION);
  pulse_data_session_connect_output (client, config, PULSE_MEDIA_CONTENT_PRESENTATION);
  pulse_data_session_config_free (config);

  const char * background_image_name = getenv ("PEXNINJA_BACKGROUND");
  if (background_image_name == NULL) {
    background_image_name = "./pexninja.png";
#if defined(HOST_WINDOWS)
    // On windows, we fetch the exe path, and patch it with a png suffix.
    char png_path[1024];
    strncpy (png_path, argv[0], sizeof (png_path));
    char * dot_exe = strstr (png_path, ".exe");
    if (dot_exe != NULL) {
      sprintf (dot_exe, ".png");
      background_image_name = png_path;
    }
#endif
  }
  PEX_LOG_INFO ("BACKGROUND_PATH: %s", background_image_name);

  pulse_options_set_background_image (client, background_image_name);
  if (application.config.options.mute_audio_on_startup) {
    application.state.audio_mute = true;
    pulse_mute_audio_input (application.client, application.state.audio_mute);
  }
  bool run_once = true;
  while (!glfwWindowShouldClose (application.window) && !application.state.quit) {
    if (run_once) {
      run_once = false;

      if (application.config.registration.enabled) {
        pulseimgui_perform_registration (&application);
      }
    }

    if (application.state.update_window_title) {
      _set_window_title (&application);
      application.state.update_window_title = false;
    }

    pull_media_stats (&application);

    int display_w, display_h;
    glfwGetFramebufferSize (application.window, &display_w, &display_h);

#if defined(HOST_DARWIN)
    float xscale, yscale;
    glfwGetWindowContentScale (application.window, &xscale, &yscale);

    if (xscale != 0 && yscale != 0) {
      display_w /= xscale;
      display_h /= yscale;
    }
#endif

    glfwPollEvents ();

    ImGui_ImplOpenGL3_NewFrame ();
    ImGui_ImplGlfw_NewFrame ();
    ImGui::NewFrame ();

    if (application.state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
      std::lock_guard<std::mutex> lock (application.room_list.mutex);
      if (application.room_list.current_room->presentation.preso_started &&
          application.room_list.current_room->presentation.preso_render)
        render_gl_ctx_background (client, &preso_ctx, display_w, display_h);
      else if (application.state.windows.swap_video_views)
        /* Swap mode: self-view is promoted to the big background so the
         * user can verify the outgoing patchbay routing at full size. */
        render_gl_ctx_background (client, &selfview_ctx, display_w, display_h);
      else
        render_gl_ctx_background (client, &mainvideo_ctx, display_w, display_h);
    } else {
      if (application.state.windows.swap_video_views)
        render_gl_ctx_background (client, &selfview_ctx, display_w, display_h);
      else
        render_gl_ctx_background (client, &mainvideo_ctx, display_w, display_h);
    }

    if (application.state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED && application.state.audio_mute) {
      const char * msg = "Audio temporarily unmuted";
      if (application.state.audio_temporarily_unmuted == false &&
          ImGui::IsKeyPressed (ImGuiKey_Space)) {
        application.state.audio_temporarily_unmuted = true;
        register_alarm (&application, msg, 0, yellow_color);
        pulse_mute_audio_input (application.client, false);
      } else if (application.state.audio_temporarily_unmuted &&
                 ImGui::IsKeyReleased (ImGuiKey_Space)) {
        pulse_mute_audio_input (application.client, true);
        application.state.audio_temporarily_unmuted = false;
        cancel_alarm (&application, msg, 0);
      }
    }

    update_color_shift ();
    update_fade_alpha ();

    configure_main_menu (&application);
    configure_bottom_bar (&application);
    configure_popups (&application);

    if (application.state.windows.show_registration) {
      configure_menu_registration (&application);
    }

    if (application.state.windows.show_connection) {
      configure_menu_connection (&application);
    }

    if (application.state.windows.show_config) {
      configure_menu_settings (&application, &preflight_ctx);
    } else if (application.state.windows.show_self_view && !application.state.video_mute) {
      /* In swap mode, the small corner window now shows the received
       * MAIN video; the self-view (which the paint overlay belongs to)
       * has been promoted to the background. The fullscreen paint
       * overlay below picks up the drawing in that case. */
      const bool swap = application.state.windows.swap_video_views;
      configure_window_self_view (&application, swap ? &mainvideo_ctx : &selfview_ctx, swap ? "Far end" : "Me! Me! Me!",
                                  /*is_selfview=*/!swap);
    }

    /* Fullscreen paint overlay: when the self-view has been swapped to
     * the background, drawing-mode strokes need to land on the whole
     * viewport rather than on a small corner window. We render an
     * invisible borderless ImGui window that covers the entire viewport
     * and route the existing paint_overlay_handle through it. The
     * function is a no-op when drawing-mode is disabled, so this is
     * cheap when unused. */
    if (application.state.windows.swap_video_views && !application.state.video_mute &&
        !application.state.windows.show_config) {
      const ImGuiViewport * vp = ImGui::GetMainViewport ();
      ImGui::SetNextWindowPos (vp->WorkPos);
      ImGui::SetNextWindowSize (vp->WorkSize);
      ImGui::Begin ("##paint_fullscreen_overlay", NULL,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                      ImGuiWindowFlags_NoSavedSettings);
      paint_overlay_handle (&application, vp->WorkPos, vp->WorkSize);
      ImGui::End ();
    }

    if (application.state.windows.show_rtmp_input) {
      configure_menu_rtmp_input (&application);
    }

    if (application.state.windows.show_pexninja_info) {
      configure_window_show_pexninja_info (&application);
    }

    if (application.state.windows.show_pulse_info) {
      configure_window_show_pulse_info (&application);
    }

    if (application.state.windows.show_compositor) {
      configure_window_compositor (&application);
    }

    /* Render any open file-picker modal. Must run after the windows
     * that may have called file_picker::request_open() this frame, so
     * that the OpenPopup() they issued takes effect on the same
     * frame. Cheap when no request is pending. */
    file_picker::render_pending ();

    if (application.state.windows.show_camera_controls) {
      configure_window_show_camera_controls (&application);
    }

    if (application.state.windows.show_pmx_media_stats) {
      configure_window_show_pmx_media_stats (&application);
    }

    if (application.state.windows.show_metrics) {
      ImGui::ShowMetricsWindow (&application.state.windows.show_metrics);
      // keep it around for UI debugging if we need to:
      // ImGui::ShowDemoWindow (&show_metrics);
    }

    if (application.state.windows.show_roster_list) {
      configure_window_roster_list (&application);
    }

    if (application.state.windows.show_chat_window) {
      configure_window_chat_window (&application);
    }

    if (application.state.windows.show_paint_tools) {
      configure_window_paint_tools (&application);
    }

    if (application.state.windows.show_live_captions_window) {
      configure_window_live_captions_window (&application);
    }

    if (application.state.windows.show_add_participant_window) {
      configure_window_add_participant_window (&application);
    }

    if (application.state.windows.show_fecc_window) {
      configure_window_fecc_window (&application);
    }

    draw_overlay_presentation_info (&application);
    draw_overlay_menu (&application);
    draw_overlay_alarms (&application);

    ImGui::EndFrame ();
    ImGui::Render ();
    glViewport (0, 0, display_w, display_h);
    // Clear to the Dark Frosted shell colour rather than the legacy blue-grey
    // (clear_color doubles as a text tint elsewhere, so it can't change): the
    // theme's surfaces are translucent, so whatever is cleared here reads
    // through every panel that isn't covering video.
    {
      const ImVec4 shell = PexipTheme::Hex (PexipTheme::kShellDeep);
      glClearColor (shell.x, shell.y, shell.z, 1.0f);
    }
    glClear (GL_COLOR_BUFFER_BIT);
    glClearDepth (1.0);

    ImGuiIO & io = ImGui::GetIO ();
#ifdef IMGUI_HAS_VIEWPORT
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      ImGui::UpdatePlatformWindows ();
      ImGui::RenderPlatformWindowsDefault ();
    }
#else
    (void) io; /* multi-viewport support only exists in the docking branch */
#endif

    ImGui_ImplOpenGL3_RenderDrawData (ImGui::GetDrawData ());

    glfwSwapBuffers (application.window);

    handle_async_operation (&application);
  }

  // Wait for any active async operations to complete, before proceeding (we currently
  // have no way of cancelling an async operation.)
  while (application.state.async_op.op != ASYNC_OP_NONE) {
    if (!handle_async_operation (&application)) {
      std::this_thread::sleep_for (100ms);
    }
  }

  if (application.state.registered) {
    pulse_deregister (client, NULL);
    application.state.registered = false;
  }

  if (application.state.conn_status == PULSE_CONNECTION_STATUS_CONNECTED) {
    auto & vm = application.state.video_mix;
    auto & pm = application.state.preso_mix;
    /* stop video mix if active */
    pexninja_stop_video_mix (&application, vm);
    pexninja_stop_video_mix (&application, pm);

    /* Disconnect any video mix sessions that the Compositor connected
     * (the legacy `vm` / `pm` paths above don't know about these
     * because `vm.active` stays false when the compositor is what
     * called pulse_video_mix_connect). Must happen before
     * pulse_disconnect tears the client state down. */
    {
      using CO = PexNinjaState::PexNinjaCompositor;
      auto & comp = application.state.compositor;
      for (int ci = 0; ci < (int)CO::kCanvasCount; ++ci) {
        if (comp.connected_on_wire[ci]) {
          pulse_video_mix_disconnect (application.client, compositor::_canvas_media_content ((CO::CanvasIdx)ci));
          comp.connected_on_wire[ci] = false;
        }
      }
    }

    /* Release any inputs the compositor / source library materialised. */
    pexninja_release_source_library (&application);

    pulse_disconnect (client, NULL);
  }
  pulse_deregister_device_list_changed_callback (client, PULSE_MEDIA_VIDEO);
  pulse_deregister_device_list_changed_callback (client, PULSE_MEDIA_AUDIO);

  pulse_deregister_device_audio_level_callback (client, PULSE_MEDIA_INPUT);
  delete application.state.mic_audio_levels;

  cleanup_devices (camera_devices);
  cleanup_devices (speaker_devices);
  cleanup_devices (microphone_devices);

  std::lock_guard<std::mutex> lock (application.room_list.mutex);
  for (auto it = application.room_list.room_map.begin (); it != application.room_list.room_map.end (); it++) {
    delete it->second;
  }
  application.room_list.room_map.clear ();

  clear_pulse_callbacks (client);
  pulse_free (client);

  ImGui_ImplOpenGL3_Shutdown ();
  ImGui_ImplGlfw_Shutdown ();
  ImPlot::DestroyContext ();
  ImGui::DestroyContext ();

  glfwDestroyWindow (application.window);
  glfwTerminate ();

  _write_config_if_needed (&application);

#if defined(HOST_WINDOWS)
  {
    HRESULT hr = _pexninja_deregister_app_url_handler ();
    if (!SUCCEEDED (hr)) {
      PEX_LOG_ERROR ("app-scheme de-registration failed with error: %d.", hr);
    }
  }
#endif

  return 0;
}