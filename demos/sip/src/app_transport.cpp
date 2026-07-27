// ============================================================================
//  app_transport.cpp - implementation; see app_transport.h for the contract.
// ============================================================================
#include "app_transport.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <pexpulse/pulse_error.h>
#include <pexpulse/pulse_media.h>

namespace doppler {

namespace {

// ---------------------------------------------------------------------------
//  Small SDP helpers - line oriented, just enough to support the rewrite
//  and remote-endpoint extraction the demo needs. Not a general SDP parser.
// ---------------------------------------------------------------------------

// Split an SDP blob into lines, preserving the original CRLF/LF on join.
// We split on '\n', strip a trailing '\r' from each piece, and remember
// whether the original used CRLF so we can put it back on rejoin.
struct SdpLines {
    std::vector<std::string> lines;
    bool                     used_crlf = true;
};

static SdpLines split_sdp(const std::string & sdp)
{
    SdpLines out;
    out.used_crlf = (sdp.find("\r\n") != std::string::npos);
    std::string cur;
    cur.reserve(128);
    for (char c : sdp) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.lines.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.lines.push_back(std::move(cur));
    return out;
}

static std::string join_sdp(const SdpLines & sl)
{
    const char * sep = sl.used_crlf ? "\r\n" : "\n";
    std::string out;
    for (const auto & line : sl.lines) {
        out += line;
        out += sep;
    }
    return out;
}

// Parse "m=<media> <port> ..." and return media token + port; returns
// false if the line isn't a media line or is malformed.
static bool parse_m_line(const std::string & line,
                         std::string &       media_out,
                         unsigned &          port_out)
{
    if (line.size() < 3 || line.compare(0, 2, "m=") != 0) return false;
    const std::string body = line.substr(2);
    std::istringstream iss(body);
    std::string portstr;
    if (!(iss >> media_out >> portstr)) return false;
    try {
        port_out = static_cast<unsigned>(std::stoul(portstr));
    } catch (...) {
        return false;
    }
    return true;
}

// Replace the port in "m=<media> <port> <proto> ..." with new_port,
// preserving everything else on the line.
static std::string rewrite_m_line_port(const std::string & line, unsigned new_port)
{
    // "m=" + media + " " + port + " " + rest
    const std::string body = line.substr(2);
    const size_t p1 = body.find(' ');
    if (p1 == std::string::npos) return line;
    const size_t p2 = body.find(' ', p1 + 1);
    if (p2 == std::string::npos) return line;
    std::string media = body.substr(0, p1);
    std::string rest  = body.substr(p2);  // includes the leading space
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", new_port);
    return "m=" + media + " " + buf + rest;
}

// Replace the IP literal in "c=IN IP4 <ip>"; leave other c= lines (IP6, FQDN)
// alone since we only bind AF_INET in the demo anyway.
static std::string rewrite_c_line_ipv4(const std::string & line,
                                       const std::string & new_ip)
{
    static const char prefix[] = "c=IN IP4 ";
    if (line.compare(0, sizeof(prefix) - 1, prefix) != 0) return line;
    return std::string(prefix) + new_ip;
}

// Parse "a=rtcp:<port>[ ...]" - return just the port.
static bool parse_a_rtcp(const std::string & line, unsigned & port_out)
{
    static const char prefix[] = "a=rtcp:";
    if (line.compare(0, sizeof(prefix) - 1, prefix) != 0) return false;
    try {
        port_out = static_cast<unsigned>(std::stoul(line.substr(sizeof(prefix) - 1)));
    } catch (...) {
        return false;
    }
    return true;
}

// Parse "c=IN IP4 <ip>" - return just the IP.
static bool parse_c_ipv4(const std::string & line, std::string & ip_out)
{
    static const char prefix[] = "c=IN IP4 ";
    if (line.compare(0, sizeof(prefix) - 1, prefix) != 0) return false;
    ip_out = line.substr(sizeof(prefix) - 1);
    // Some SDPs append "/<ttl>" or "/<ttl>/<num>" for multicast - strip.
    const size_t slash = ip_out.find('/');
    if (slash != std::string::npos) ip_out.resize(slash);
    return true;
}

// Walk the SDP m= sections in order; for each section return its media
// token ("audio" / "video" / ...), the port from the m= line, whether
// a=rtcp-mux is in effect for that section, the per-section RTCP port
// (from a=rtcp: or port+1), the connection IP (per-m= c= line if any,
// else the session-level c= passed in), and the indices of the lines
// we'd rewrite (the m= line itself, a c= line if any, an a=rtcp: line
// if any).
struct SdpMediaSection {
    std::string media;          // "audio", "video", ...
    unsigned    rtp_port  = 0;
    unsigned    rtcp_port = 0;  // valid if !mux
    bool        mux       = false;
    std::string conn_ip;        // empty if not present

    // Indices into SdpLines::lines for the rewriter (only valid when
    // their matching has_* flag is true):
    size_t      m_line_idx          = 0;
    size_t      c_line_idx          = 0;
    size_t      a_rtcp_line_idx     = 0;
    bool        has_m_line_idx      = false;
    bool        has_c_line_idx      = false;
    bool        has_a_rtcp_line_idx = false;
};

// Walks the SDP and returns one entry per m= section. The session-level
// c=IN IP4 is propagated as the default conn_ip for sections that don't
// have their own.
static std::vector<SdpMediaSection> scan_sdp(const SdpLines & sl)
{
    std::vector<SdpMediaSection> out;
    std::string session_ip;
    SdpMediaSection * cur = nullptr;

    for (size_t i = 0; i < sl.lines.size(); ++i) {
        const std::string & line = sl.lines[i];
        std::string media; unsigned port = 0;
        if (parse_m_line(line, media, port)) {
            out.push_back({});
            cur = &out.back();
            cur->media = media;
            cur->rtp_port  = port;
            cur->rtcp_port = port + 1;  // RFC 3550 default
            cur->conn_ip   = session_ip;
            cur->m_line_idx = i;
            cur->has_m_line_idx = true;
            continue;
        }
        std::string ip;
        if (parse_c_ipv4(line, ip)) {
            if (cur) {
                cur->conn_ip = ip;
                cur->c_line_idx = i;
                cur->has_c_line_idx = true;
            } else {
                session_ip = ip;
            }
            continue;
        }
        unsigned rtcp = 0;
        if (cur && parse_a_rtcp(line, rtcp)) {
            cur->rtcp_port = rtcp;
            cur->a_rtcp_line_idx = i;
            cur->has_a_rtcp_line_idx = true;
            continue;
        }
        if (cur && line == "a=rtcp-mux") {
            cur->mux = true;
            continue;
        }
    }
    return out;
}

// Map a media section's (m= index, m= token) to a (content, type). Pulse's
// SIP-mode offers walk audio first then video; a second m=video is the
// presentation/content channel.
static bool section_to_content_type(const std::string & media,
                                    int                 audio_seen_so_far,
                                    int                 video_seen_so_far,
                                    PulseMediaContent & content_out,
                                    PulseMediaType &    type_out)
{
    if (media == "audio") {
        type_out    = PULSE_MEDIA_AUDIO;
        content_out = (audio_seen_so_far == 0)
                          ? PULSE_MEDIA_CONTENT_MAIN
                          : PULSE_MEDIA_CONTENT_PRESENTATION;
        return true;
    }
    if (media == "video") {
        type_out    = PULSE_MEDIA_VIDEO;
        content_out = (video_seen_so_far == 0)
                          ? PULSE_MEDIA_CONTENT_MAIN
                          : PULSE_MEDIA_CONTENT_PRESENTATION;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Per-wire state: one bound UDP socket, the channel id Pulse uses to tag
//  packets on this wire, and the remote endpoint we sendto() (populated
//  from the SIP answer).
// ---------------------------------------------------------------------------

struct Channel {
    PulseAppChannelId id{};        // {content, type, kind}
    int               fd = -1;     // bound socket; -1 once closed
    unsigned          local_port = 0;
    sockaddr_in       remote{};    // zero until configure_remote_answer()
    bool              remote_set = false;

    // Live counters. Written from Pulse's worker thread (tx) and our
    // reader thread (rx); read from the UI thread via snapshot(). Atomic
    // so all of that stays lock-free.
    std::atomic<uint64_t> tx_packets{0};
    std::atomic<uint64_t> tx_bytes{0};
    std::atomic<uint64_t> rx_packets{0};
    std::atomic<uint64_t> rx_bytes{0};

    // Outbound drop diagnostics. `cb_calls` counts every callback from
    // Pulse the bridge matched to this channel; the four `tx_drops_*`
    // buckets explain the gap between `cb_calls` and `tx_packets`.
    // `last_send_errno` snapshots errno from the most recent failed
    // sendto() so the UI can show *why* the kernel refused the packet.
    std::atomic<uint64_t> cb_calls         {0};
    std::atomic<uint64_t> tx_drops_no_remote{0};
    std::atomic<uint64_t> tx_drops_bad_fd  {0};
    std::atomic<uint64_t> tx_drops_zero_size{0};
    std::atomic<uint64_t> tx_drops_send_err{0};
    std::atomic<int>      last_send_errno  {0};

    // Channels live in a std::vector that's append-only after
    // configure_local_offer; allow it to grow by giving Channel a
    // move-constructor that copies the atomics' current values.
    Channel() = default;
    Channel(const Channel &) = delete;
    Channel & operator=(const Channel &) = delete;
    Channel(Channel && o) noexcept
      : id(o.id), fd(o.fd), local_port(o.local_port), remote(o.remote),
        remote_set(o.remote_set),
        tx_packets(o.tx_packets.load(std::memory_order_relaxed)),
        tx_bytes  (o.tx_bytes  .load(std::memory_order_relaxed)),
        rx_packets(o.rx_packets.load(std::memory_order_relaxed)),
        rx_bytes  (o.rx_bytes  .load(std::memory_order_relaxed)),
        cb_calls         (o.cb_calls         .load(std::memory_order_relaxed)),
        tx_drops_no_remote(o.tx_drops_no_remote.load(std::memory_order_relaxed)),
        tx_drops_bad_fd  (o.tx_drops_bad_fd  .load(std::memory_order_relaxed)),
        tx_drops_zero_size(o.tx_drops_zero_size.load(std::memory_order_relaxed)),
        tx_drops_send_err(o.tx_drops_send_err.load(std::memory_order_relaxed)),
        last_send_errno  (o.last_send_errno  .load(std::memory_order_relaxed)) {}
    Channel & operator=(Channel && o) noexcept {
        id = o.id; fd = o.fd; local_port = o.local_port;
        remote = o.remote; remote_set = o.remote_set;
        tx_packets.store(o.tx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
        tx_bytes  .store(o.tx_bytes  .load(std::memory_order_relaxed), std::memory_order_relaxed);
        rx_packets.store(o.rx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
        rx_bytes  .store(o.rx_bytes  .load(std::memory_order_relaxed), std::memory_order_relaxed);
        cb_calls         .store(o.cb_calls         .load(std::memory_order_relaxed), std::memory_order_relaxed);
        tx_drops_no_remote.store(o.tx_drops_no_remote.load(std::memory_order_relaxed), std::memory_order_relaxed);
        tx_drops_bad_fd  .store(o.tx_drops_bad_fd  .load(std::memory_order_relaxed), std::memory_order_relaxed);
        tx_drops_zero_size.store(o.tx_drops_zero_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
        tx_drops_send_err.store(o.tx_drops_send_err.load(std::memory_order_relaxed), std::memory_order_relaxed);
        last_send_errno  .store(o.last_send_errno  .load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }
};

static bool channel_id_equal(const PulseAppChannelId & a,
                             const PulseAppChannelId & b)
{
    return a.content == b.content && a.type == b.type && a.kind == b.kind;
}

// Stringify a PulseAppChannelId for logs: "{content,type,kind}". Kept
// short on purpose - these go in stderr lines that the user reads while
// the call is live, alongside Pulse's own one-line debug spam.
static const char * content_str(PulseMediaContent c)
{
    switch (c) {
        case PULSE_MEDIA_CONTENT_MAIN:         return "MAIN";
        case PULSE_MEDIA_CONTENT_PRESENTATION: return "PRES";
    }
    return "?";
}
static const char * type_str(PulseMediaType t)
{
    switch (t) {
        case PULSE_MEDIA_AUDIO: return "audio";
        case PULSE_MEDIA_VIDEO: return "video";
    }
    return "?";
}
static const char * kind_str(PulseAppChannelKind k)
{
    switch (k) {
        case PULSE_APP_CHANNEL_KIND_MUX:  return "MUX";
        case PULSE_APP_CHANNEL_KIND_RTP:  return "RTP";
        case PULSE_APP_CHANNEL_KIND_RTCP: return "RTCP";
    }
    return "?";
}
static std::string channel_id_str(const PulseAppChannelId & id)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{%s,%s,%s}",
                  content_str(id.content), type_str(id.type), kind_str(id.kind));
    return buf;
}

// Rate-limit gate for per-packet log lines: print the first 8 occurrences
// of an event (so the cause is obvious at call setup) and thereafter only
// at exponentially growing intervals (every 8th, 16th, 32nd, ...). The
// caller passes the *new* count after fetch_add+1.
//
// `(n & (n - 1)) == 0` is the standard "n is a power of two" trick: only
// 0b1, 0b10, 0b100, ... clear all bits when ANDed with their predecessor,
// so this fires at n = 16, 32, 64, ... (the n <= 8 short-circuit handles
// the small powers that also satisfy the test).
static bool should_log(uint64_t n)
{
    return n <= 8 || (n & (n - 1)) == 0;
}

// RFC 5761 RTP/RTCP demux for mux packets: version-2 packets with PT in
// 192..223 are RTCP, everything else is treated as RTP.
static PulseAppChannelKind classify_mux_packet_kind(const uint8_t * data, int size)
{
    if (!data || size < 2) return PULSE_APP_CHANNEL_KIND_RTP;
    const uint8_t version = static_cast<uint8_t>(data[0] >> 6);
    if (version != 2) return PULSE_APP_CHANNEL_KIND_RTP;
    const uint8_t pt = data[1];
    if (pt >= 192 && pt <= 223) return PULSE_APP_CHANNEL_KIND_RTCP;
    return PULSE_APP_CHANNEL_KIND_RTP;
}

// Pick the local IPv4 address to advertise in the rewritten SDP.
//
// IMPORTANT: Pulse's stage-1 offer comes back with 127.0.0.1 in its c=
// lines, because the moment we set an app-transport Pulse stops doing
// its own ICE / host-candidate gathering - it has no idea what IP the
// sockets WE own are reachable on. If we ship that 127.0.0.1 to the
// remote SIP peer, the peer will dutifully send RTP to its own
// loopback and we'll never see a packet. So we MUST replace it with
// an address the peer can actually route to. The same value is what
// the UI shows for each bridge's "Local" endpoint.
//
// Resolution order:
//   1) DOPPLER_SIP_LOCAL_IP env var - explicit override, wins always.
//   2) "Connected UDP socket" trick: create a UDP socket and connect()
//      it to a public, non-local address. connect() on a UDP socket
//      sends no packets but forces the kernel to pick the egress
//      interface; getsockname() then yields that interface's IPv4
//      address. This matches what PJSIP picks in pj_gethostip() and
//      is what real deployments want.
//   3) Last-ditch 127.0.0.1 if even the discovery socket failed (no
//      network at all) - the call won't work, but at least the SDP
//      is syntactically valid and the UI surfaces the local-ish IP.
static std::string pick_local_ip()
{
    const char * env = std::getenv("DOPPLER_SIP_LOCAL_IP");
    if (env && env[0]) return env;

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        // 8.8.8.8:53 is a convenient unreachable-from-loopback target.
        // No traffic actually leaves the box - connect() on UDP only
        // primes the routing table entry for subsequent sendto()s,
        // which we never issue. Port 53 is arbitrary.
        sockaddr_in probe{};
        probe.sin_family      = AF_INET;
        probe.sin_port        = htons(53);
        probe.sin_addr.s_addr = htonl(0x08080808u); // 8.8.8.8

        if (::connect(fd, reinterpret_cast<sockaddr *>(&probe),
                      sizeof(probe)) == 0) {
            sockaddr_in local{};
            socklen_t   len = sizeof(local);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&local),
                              &len) == 0) {
                char buf[INET_ADDRSTRLEN] = {0};
                if (::inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf))
                        && buf[0] != '\0') {
                    ::close(fd);
                    return std::string(buf);
                }
            }
        }
        ::close(fd);
    }
    // No usable interface - the call is doomed but don't crash.
    return "127.0.0.1";
}

// Bind a fresh UDP socket on 0.0.0.0, set it non-blocking. Tries to bind
// the port pointed to by `next_port` first; on EADDRINUSE walks up by
// one until a free port is found (or we run out of port space / hit too
// many attempts). On success, `next_port` is advanced to bound_port + 1
// so successive calls produce contiguous numbers — this is what gives
// us the "audio rtp=N, audio rtcp=N+1, video rtp=N+2, ..." layout the
// SDP rewriter then advertises. Returns (fd, port) on success or
// (-1, 0) on failure (in which case `next_port` is left untouched).
static bool bind_udp_socket(int & fd_out, unsigned & port_out,
                            unsigned & next_port)
{
    constexpr unsigned kMaxAttempts = 1024;

    for (unsigned attempt = 0; attempt < kMaxAttempts; ++attempt) {
        unsigned try_port = next_port + attempt;
        if (try_port > 65535) break;     // ran off the top of the port space

        int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return false;

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            ::close(fd);
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(static_cast<uint16_t>(try_port));
        if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            int saved = errno;
            ::close(fd);
            if (saved == EADDRINUSE || saved == EACCES) {
                // Try the next candidate. EACCES covers low-numbered
                // ports we don't own, which shouldn't happen with our
                // 10000+ base but is cheap to handle.
                continue;
            }
            return false;
        }

        sockaddr_in bound{};
        socklen_t blen = sizeof(bound);
        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &blen) < 0) {
            ::close(fd);
            return false;
        }
        fd_out    = fd;
        port_out  = ntohs(bound.sin_port);
        next_port = port_out + 1;
        return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
//  AppTransport::Impl - all the moving parts. Public AppTransport is a
//  thin PIMPL wrapper.
// ---------------------------------------------------------------------------

struct AppTransport::Impl {
    Pulse *              client = nullptr;
    std::vector<Channel> channels;       // append-only; reader thread reads it
    std::string          advertised_ip;

    std::thread          reader;
    std::atomic<bool>    reader_stop{false};
    // Self-pipe lets us unblock poll() at shutdown without depending on
    // signals; we write a byte to wake() and the reader loop drops out.
    int                  wake_r = -1;
    int                  wake_w = -1;

    mutable std::mutex   remote_mtx;     // guards channels[*].remote{,_set}

    // Bridge-wide ("global") diagnostic counters. These are incremented
    // from Pulse's worker thread inside on_outbound() / send_packet() and
    // sampled from the UI thread via AppTransport::totals().
    std::atomic<uint64_t> cb_total       {0};
    std::atomic<uint64_t> cb_no_channel  {0};
    std::atomic<uint64_t> cb_null_data   {0};
    std::atomic<uint64_t> cb_unbound     {0};
    std::atomic<uint64_t> cb_mux_fallback{0};

    // ----- callback plumbing ------------------------------------------------

    static void on_outbound(void * user_context,
                            PulseAppChannelId channel_id,
                            const uint8_t * data,
                            int size)
    {
        auto * self = static_cast<Impl *>(user_context);
        if (!self) {
            // No way to attribute this; just leak a single stderr line.
            std::fprintf(stderr,
                "[app-transport] on_outbound: NULL user_context (size=%d) "
                "- packet dropped\n", size);
            return;
        }
        uint64_t total = self->cb_total.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!self->client) {
            uint64_t n = self->cb_unbound.fetch_add(1, std::memory_order_relaxed) + 1;
            if (should_log(n)) {
                std::fprintf(stderr,
                    "[app-transport] on_outbound %s after unbind "
                    "(size=%d, cb_total=%llu, cb_unbound=%llu) - dropped\n",
                    channel_id_str(channel_id).c_str(), size,
                    static_cast<unsigned long long>(total),
                    static_cast<unsigned long long>(n));
            }
            return;
        }
        if (data == nullptr || size <= 0) {
            uint64_t n = self->cb_null_data.fetch_add(1, std::memory_order_relaxed) + 1;
            if (should_log(n)) {
                std::fprintf(stderr,
                    "[app-transport] on_outbound %s NULL/empty buffer "
                    "(data=%p, size=%d, cb_null=%llu) - dropped\n",
                    channel_id_str(channel_id).c_str(),
                    static_cast<const void *>(data), size,
                    static_cast<unsigned long long>(n));
            }
            return;
        }
        self->send_packet(channel_id, data, size);
    }

    static void on_destroy(void * /*user_context*/)
    {
        // We own the AppTransport lifetime ourselves; nothing to release
        // here. Pulse just calls this to let us know the binding is gone.
    }

    // Outbound: lookup the channel, sendto() its remote endpoint.
    //
    // Every drop path increments a per-channel atomic counter and emits
    // a rate-limited stderr line; the totals are surfaced through the
    // BridgeStat snapshot so the UI shows *why* packets are not making
    // it onto the wire even though Pulse keeps feeding us callbacks.
    void send_packet(const PulseAppChannelId & id, const uint8_t * data, int size)
    {
        sockaddr_in remote{};
        int fd = -1;
        Channel * chan = nullptr;
        bool found      = false;
        bool used_mux_fallback = false;
        bool no_remote  = false;
        bool bad_fd     = false;
        {
            std::lock_guard<std::mutex> lock(remote_mtx);
            for (auto & c : channels) {
                if (channel_id_equal(c.id, id)) {
                    found = true;
                    chan  = &c;
                    if (!c.remote_set) { no_remote = true; break; }
                    if (c.fd < 0)      { bad_fd    = true; break; }
                    remote = c.remote;
                    fd     = c.fd;
                    break;
                }
            }
            if (!found && id.kind == PULSE_APP_CHANNEL_KIND_MUX) {
                const PulseAppChannelKind fallback_kind =
                    classify_mux_packet_kind(data, size);
                for (auto & c : channels) {
                    if (c.id.content == id.content
                        && c.id.type == id.type
                        && c.id.kind == fallback_kind) {
                        found = true;
                        used_mux_fallback = true;
                        chan  = &c;
                        if (!c.remote_set) { no_remote = true; break; }
                        if (c.fd < 0)      { bad_fd    = true; break; }
                        remote = c.remote;
                        fd     = c.fd;
                        break;
                    }
                }
            }
        }

        if (!found) {
            // Pulse handed us a channel id we never registered. This is
            // the "channel-id mismatch" scenario - log the id verbatim
            // so we can see exactly what Pulse is asking for. On the
            // *first* occurrence we also dump every channel id we DID
            // register so the operator can spot the kind/content/type
            // mismatch in one go (e.g. Pulse sending {MAIN,audio,MUX}
            // while we registered {MAIN,audio,RTP}+{MAIN,audio,RTCP}).
            uint64_t n = cb_no_channel.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1) {
                std::fprintf(stderr,
                    "[app-transport] send_packet %s NO MATCHING CHANNEL "
                    "(size=%d) - dropped. Registered channels are:\n",
                    channel_id_str(id).c_str(), size);
                // Re-take the lock just for the dump; cheap, only happens
                // once per Pulse session.
                std::lock_guard<std::mutex> lock(remote_mtx);
                for (const auto & c : channels) {
                    std::fprintf(stderr,
                        "[app-transport]   registered: %s fd=%d port=%u\n",
                        channel_id_str(c.id).c_str(), c.fd, c.local_port);
                }
            } else if (should_log(n)) {
                std::fprintf(stderr,
                    "[app-transport] send_packet %s NO MATCHING CHANNEL "
                    "(size=%d, cb_no_channel=%llu) - dropped\n",
                    channel_id_str(id).c_str(), size,
                    static_cast<unsigned long long>(n));
            }
            return;
        }

        const PulseAppChannelId & route_id = chan->id;
        if (used_mux_fallback) {
            uint64_t n = cb_mux_fallback.fetch_add(1, std::memory_order_relaxed) + 1;
            if (should_log(n)) {
                std::fprintf(stderr,
                    "[app-transport] send_packet %s fallback-routed to %s "
                    "(size=%d, cb_mux_fallback=%llu)\n",
                    channel_id_str(id).c_str(),
                    channel_id_str(route_id).c_str(),
                    size,
                    static_cast<unsigned long long>(n));
            }
        }

        // Matched a channel - count the callback against it regardless
        // of whether it eventually reaches the wire.
        uint64_t cb_n = chan->cb_calls.fetch_add(1, std::memory_order_relaxed) + 1;

        if (no_remote) {
            uint64_t n = chan->tx_drops_no_remote
                .fetch_add(1, std::memory_order_relaxed) + 1;
            if (should_log(n)) {
                std::fprintf(stderr,
                    "[app-transport] send_packet %s remote not yet set "
                    "(size=%d, cb=%llu, drops_no_remote=%llu) - dropped\n",
                    channel_id_str(route_id).c_str(), size,
                    static_cast<unsigned long long>(cb_n),
                    static_cast<unsigned long long>(n));
            }
            return;
        }
        if (bad_fd) {
            uint64_t n = chan->tx_drops_bad_fd
                .fetch_add(1, std::memory_order_relaxed) + 1;
            if (should_log(n)) {
                std::fprintf(stderr,
                    "[app-transport] send_packet %s socket closed "
                    "(size=%d, cb=%llu, drops_bad_fd=%llu) - dropped\n",
                    channel_id_str(route_id).c_str(), size,
                    static_cast<unsigned long long>(cb_n),
                    static_cast<unsigned long long>(n));
            }
            return;
        }
        if (size <= 0) {
            uint64_t n = chan->tx_drops_zero_size
                .fetch_add(1, std::memory_order_relaxed) + 1;
            if (should_log(n)) {
                std::fprintf(stderr,
                    "[app-transport] send_packet %s zero-size buffer "
                    "(cb=%llu, drops_zero=%llu) - dropped\n",
                    channel_id_str(route_id).c_str(),
                    static_cast<unsigned long long>(cb_n),
                    static_cast<unsigned long long>(n));
            }
            return;
        }

        // Best-effort send; the API doc explicitly says outbound packets
        // that can't be sent are dropped.
        ssize_t sent = ::sendto(fd, data, static_cast<size_t>(size), 0,
                                reinterpret_cast<sockaddr *>(&remote), sizeof(remote));
        if (sent < 0) {
            int saved = errno;
            chan->last_send_errno.store(saved, std::memory_order_relaxed);
            uint64_t n = chan->tx_drops_send_err
                .fetch_add(1, std::memory_order_relaxed) + 1;
            if (should_log(n)) {
                char ipbuf[INET_ADDRSTRLEN] = {0};
                ::inet_ntop(AF_INET, &remote.sin_addr, ipbuf, sizeof(ipbuf));
                std::fprintf(stderr,
                    "[app-transport] send_packet %s sendto() failed: "
                    "errno=%d (%s) fd=%d dst=%s:%u size=%d "
                    "(cb=%llu, drops_send_err=%llu) - dropped\n",
                    channel_id_str(route_id).c_str(),
                    saved, std::strerror(saved), fd,
                    ipbuf, ntohs(remote.sin_port), size,
                    static_cast<unsigned long long>(cb_n),
                    static_cast<unsigned long long>(n));
            }
            return;
        }

        // Success. Log the first packet per channel so we can confirm
        // outbound has started flowing without spamming for every one.
        uint64_t tx_n = chan->tx_packets.fetch_add(1, std::memory_order_relaxed) + 1;
        chan->tx_bytes.fetch_add(static_cast<uint64_t>(sent),
                                 std::memory_order_relaxed);
        if (tx_n == 1) {
            char ipbuf[INET_ADDRSTRLEN] = {0};
            ::inet_ntop(AF_INET, &remote.sin_addr, ipbuf, sizeof(ipbuf));
            std::fprintf(stderr,
                "[app-transport] send_packet %s first packet on the wire "
                "(fd=%d dst=%s:%u size=%d cb=%llu)\n",
                channel_id_str(route_id).c_str(), fd,
                ipbuf, ntohs(remote.sin_port), size,
                static_cast<unsigned long long>(cb_n));
        }
    }

    // Inbound reader thread: poll() over all sockets + the wake pipe,
    // recvfrom() any ready socket, push the bytes into Pulse tagged with
    // that socket's channel id.
    void reader_loop()
    {
        std::vector<pollfd> pfds;
        std::vector<PulseAppChannelId> ids;
        std::vector<Channel *> chans;
        // Snapshot of (fd, id, chan*) at thread start; the channels vector
        // itself is fixed by the time we start (configure_local_offer ran).
        for (auto & c : channels) {
            if (c.fd < 0) continue;
            pfds.push_back({ c.fd, POLLIN, 0 });
            ids.push_back(c.id);
            chans.push_back(&c);
        }
        // Wake pipe is the last entry.
        pfds.push_back({ wake_r, POLLIN, 0 });

        std::vector<uint8_t> buf(2048);  // RTP/RTCP MTU-ish
        while (!reader_stop.load(std::memory_order_acquire)) {
            int n = ::poll(pfds.data(), pfds.size(), 250 /*ms*/);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (n == 0) continue;
            // Drain the wake pipe so the next poll() blocks.
            if (pfds.back().revents & POLLIN) {
                uint8_t scratch[16];
                while (::read(wake_r, scratch, sizeof(scratch)) > 0) {}
            }
            for (size_t i = 0; i + 1 < pfds.size(); ++i) {
                if (!(pfds[i].revents & POLLIN)) continue;
                for (;;) {
                    ssize_t got = ::recvfrom(pfds[i].fd, buf.data(), buf.size(),
                                             0, nullptr, nullptr);
                    if (got <= 0) {
                        if (got < 0 && errno == EINTR) continue;
                        break;  // EAGAIN/EWOULDBLOCK or closed
                    }
                    chans[i]->rx_packets.fetch_add(1, std::memory_order_relaxed);
                    chans[i]->rx_bytes.fetch_add(static_cast<uint64_t>(got),
                                                 std::memory_order_relaxed);
                    if (client) {
                        pulse_app_transport_push(client, ids[i],
                                                 buf.data(),
                                                 static_cast<size_t>(got));
                    }
                }
            }
        }
    }

    void wake()
    {
        uint8_t b = 1;
        if (wake_w >= 0) ::write(wake_w, &b, 1);
    }

    // ----- lifecycle --------------------------------------------------------

    bool create_wake_pipe()
    {
        int fds[2];
        if (::pipe(fds) < 0) return false;
        for (int f : fds) {
            int flags = fcntl(f, F_GETFL, 0);
            fcntl(f, F_SETFL, flags | O_NONBLOCK);
        }
        wake_r = fds[0];
        wake_w = fds[1];
        return true;
    }

    void close_sockets()
    {
        for (auto & c : channels) {
            if (c.fd >= 0) { ::close(c.fd); c.fd = -1; }
        }
    }

    void shutdown()
    {
        if (reader.joinable()) {
            reader_stop.store(true, std::memory_order_release);
            wake();
            reader.join();
        }
        if (client) {
            // Clear the binding so Pulse drops its reference to us.
            pulse_options_set_app_transport(client, nullptr, nullptr, nullptr);
            client = nullptr;
        }
        close_sockets();
        if (wake_r >= 0) { ::close(wake_r); wake_r = -1; }
        if (wake_w >= 0) { ::close(wake_w); wake_w = -1; }
    }
};

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

AppTransport::AppTransport() : impl_(std::make_unique<Impl>())
{
    impl_->advertised_ip = pick_local_ip();
}

AppTransport::~AppTransport() { shutdown(); }

PulseError AppTransport::bind_to_pulse(Pulse * client)
{
    impl_->client = client;
    return pulse_options_set_app_transport(client,
                                           &Impl::on_outbound,
                                           impl_.get(),
                                           &Impl::on_destroy);
}

PulseError AppTransport::unbind_from_pulse()
{
    // Mirror of bind_to_pulse() that just clears Pulse's reference to us.
    // The local sockets and reader thread are left alone so a subsequent
    // bind_to_pulse() can re-attach without re-allocating ports (which
    // matters because configure_local_offer() may already have published
    // those ports in an SDP).
    if (!impl_->client) {
        return PULSE_SUCCESS;
    }
    Pulse * client = impl_->client;
    impl_->client = nullptr;
    return pulse_options_set_app_transport(client, nullptr, nullptr, nullptr);
}

std::string AppTransport::configure_local_offer(const std::string & pulse_offer_sdp,
                                                std::string &       out_error)
{
    out_error.clear();
    SdpLines sl = split_sdp(pulse_offer_sdp);
    auto sections = scan_sdp(sl);
    if (sections.empty()) {
        out_error = "no m= sections in Pulse offer";
        return pulse_offer_sdp;
    }
    if (!impl_->create_wake_pipe()) {
        out_error = "pipe() for wake fd failed";
        return pulse_offer_sdp;
    }

    int audio_seen = 0, video_seen = 0;
    // Sequential port allocator. Successive bind_udp_socket() calls
    // walk this up by one, so on a fresh demo run with the default
    // m-section order (audio then video) we get audio RTP=10000,
    // audio RTCP=10001, video RTP=10002, video RTCP=10003, ... If a
    // candidate port is already in use we skip past it - in practice
    // that just shifts the contiguous block up.
    unsigned next_port = 10000;
    for (auto & s : sections) {
        PulseMediaContent content; PulseMediaType type;
        if (!section_to_content_type(s.media, audio_seen, video_seen, content, type)) {
            // Unsupported media (application, message, ...). Skip - Pulse
            // wouldn't have generated one but be defensive.
            continue;
        }
        if (s.media == "audio") ++audio_seen;
        if (s.media == "video") ++video_seen;

        if (s.mux) {
            int fd; unsigned port;
            if (!bind_udp_socket(fd, port, next_port)) {
                out_error = "bind() failed for " + s.media + " mux socket";
                return pulse_offer_sdp;
            }
            Channel ch;
            ch.id = PulseAppChannelId{ content, type, PULSE_APP_CHANNEL_KIND_MUX };
            ch.fd = fd;
            ch.local_port = port;
            impl_->channels.push_back(std::move(ch));
            // Rewrite the m= line port; the rtcp-mux line stays as-is.
            sl.lines[s.m_line_idx] = rewrite_m_line_port(sl.lines[s.m_line_idx], port);
            if (s.has_a_rtcp_line_idx) {
                // Some offers put a=rtcp: even with mux; keep it consistent.
                // Include the connection address per RFC 3605 so receivers
                // that read the a=rtcp line in isolation (rather than
                // inheriting from c=) still know where to send RTCP.
                char buf[64];
                std::snprintf(buf, sizeof(buf), "a=rtcp:%u IN IP4 %s",
                              port, impl_->advertised_ip.c_str());
                sl.lines[s.a_rtcp_line_idx] = buf;
            }
        } else {
            int rtp_fd, rtcp_fd; unsigned rtp_port, rtcp_port;
            if (!bind_udp_socket(rtp_fd, rtp_port, next_port)) {
                out_error = "bind() failed for " + s.media + " RTP socket";
                return pulse_offer_sdp;
            }
            if (!bind_udp_socket(rtcp_fd, rtcp_port, next_port)) {
                ::close(rtp_fd);
                out_error = "bind() failed for " + s.media + " RTCP socket";
                return pulse_offer_sdp;
            }
            Channel rtp_ch;
            rtp_ch.id = PulseAppChannelId{ content, type, PULSE_APP_CHANNEL_KIND_RTP };
            rtp_ch.fd = rtp_fd;
            rtp_ch.local_port = rtp_port;
            impl_->channels.push_back(std::move(rtp_ch));

            Channel rtcp_ch;
            rtcp_ch.id = PulseAppChannelId{ content, type, PULSE_APP_CHANNEL_KIND_RTCP };
            rtcp_ch.fd = rtcp_fd;
            rtcp_ch.local_port = rtcp_port;
            impl_->channels.push_back(std::move(rtcp_ch));

            sl.lines[s.m_line_idx] = rewrite_m_line_port(sl.lines[s.m_line_idx], rtp_port);
            // Per RFC 3605, a=rtcp may carry "<port> <nettype> <addrtype>
            // <addr>". We always emit the full form using the same IP we
            // stamp into c= below, so the RTCP endpoint is unambiguous
            // even for parsers that don't fall back to the c= line.
            char buf[64];
            std::snprintf(buf, sizeof(buf), "a=rtcp:%u IN IP4 %s",
                          rtcp_port, impl_->advertised_ip.c_str());
            if (s.has_a_rtcp_line_idx) {
                sl.lines[s.a_rtcp_line_idx] = buf;
            } else {
                // Insert a=rtcp: right after the m= line for this section.
                sl.lines.insert(sl.lines.begin() + s.m_line_idx + 1, buf);
                // Indices of later sections are now stale; we don't use
                // them again in this loop iteration, but bump anything we
                // still need.
                for (auto & later : sections) {
                    if (&later == &s) continue;
                    if (later.has_m_line_idx     && later.m_line_idx     > s.m_line_idx) ++later.m_line_idx;
                    if (later.has_c_line_idx     && later.c_line_idx     > s.m_line_idx) ++later.c_line_idx;
                    if (later.has_a_rtcp_line_idx && later.a_rtcp_line_idx > s.m_line_idx) ++later.a_rtcp_line_idx;
                }
            }
        }
    }

    // Rewrite every c=IN IP4 line to advertise our local IP. We do this
    // last so the m= rewrites above don't have to track session-level c=
    // ordering quirks.
    for (auto & line : sl.lines) {
        line = rewrite_c_line_ipv4(line, impl_->advertised_ip);
    }

    // Reader thread can start now - sockets exist and the channel id list
    // is final. Pulse may already be pushing outbound packets via the
    // callback (which only needs the channel list + remotes), but inbound
    // won't matter until the answer SDP gives us remotes, so starting
    // the reader here is correct.
    impl_->reader_stop.store(false, std::memory_order_release);
    impl_->reader = std::thread([this] { impl_->reader_loop(); });

    // Dump the final registered channel id list. This is the canonical
    // "what we told Pulse to call us with" reference - any on_outbound
    // callback whose channel id isn't in this list will be dropped and
    // logged as cb_no_channel (see send_packet()).
    std::fprintf(stderr,
        "[app-transport] configure_local_offer: registered %zu channel(s):\n",
        impl_->channels.size());
    for (const auto & c : impl_->channels) {
        std::fprintf(stderr,
            "[app-transport]   channel: %s fd=%d port=%u\n",
            channel_id_str(c.id).c_str(), c.fd, c.local_port);
    }

    return join_sdp(sl);
}

std::string AppTransport::configure_remote_answer(const std::string & remote_answer_sdp)
{
    SdpLines sl = split_sdp(remote_answer_sdp);
    auto sections = scan_sdp(sl);
    if (sections.empty()) return "no m= sections in SIP answer";

    int audio_seen = 0, video_seen = 0;
    std::lock_guard<std::mutex> lock(impl_->remote_mtx);

    for (auto & s : sections) {
        PulseMediaContent content; PulseMediaType type;
        if (!section_to_content_type(s.media, audio_seen, video_seen, content, type)) continue;
        if (s.media == "audio") ++audio_seen;
        if (s.media == "video") ++video_seen;

        if (s.conn_ip.empty()) continue;          // declined or hold; skip
        if (s.rtp_port == 0) continue;            // m= port 0 means rejected

        in_addr ipv4{};
        if (::inet_pton(AF_INET, s.conn_ip.c_str(), &ipv4) != 1) continue;

        auto set_remote = [&](PulseAppChannelKind kind, unsigned port) {
            PulseAppChannelId want{ content, type, kind };
            for (auto & c : impl_->channels) {
                if (channel_id_equal(c.id, want)) {
                    sockaddr_in & r = c.remote;
                    r.sin_family      = AF_INET;
                    r.sin_addr        = ipv4;
                    r.sin_port        = htons(static_cast<uint16_t>(port));
                    c.remote_set      = true;
                    return;
                }
            }
        };

        if (s.mux) {
            set_remote(PULSE_APP_CHANNEL_KIND_MUX, s.rtp_port);
        } else {
            set_remote(PULSE_APP_CHANNEL_KIND_RTP,  s.rtp_port);
            set_remote(PULSE_APP_CHANNEL_KIND_RTCP, s.rtcp_port);
        }
    }
    return {};
}

void AppTransport::shutdown() { if (impl_) impl_->shutdown(); }

namespace {

// Map (content, type) back to the SDP-ish media token we show in the UI.
// Audio is just "audio"; video on MAIN is "video", on PRESENTATION it's
// "slides" (the BFCP/screen-share label most users recognise).
static const char * media_label(PulseMediaContent content, PulseMediaType type)
{
    if (type == PULSE_MEDIA_AUDIO) return "audio";
    if (type == PULSE_MEDIA_VIDEO)
        return (content == PULSE_MEDIA_CONTENT_PRESENTATION) ? "slides" : "video";
    return "?";
}

static const char * kind_label(PulseAppChannelKind kind)
{
    switch (kind) {
        case PULSE_APP_CHANNEL_KIND_MUX:  return "RTP+RTCP";
        case PULSE_APP_CHANNEL_KIND_RTP:  return "RTP";
        case PULSE_APP_CHANNEL_KIND_RTCP: return "RTCP";
    }
    return "?";
}

static std::string ip_port_str(const sockaddr_in & sa)
{
    char ip[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
    char buf[INET_ADDRSTRLEN + 8];
    std::snprintf(buf, sizeof(buf), "%s:%u", ip, ntohs(sa.sin_port));
    return buf;
}

} // namespace

std::vector<BridgeStat> AppTransport::snapshot() const
{
    std::vector<BridgeStat> out;
    if (!impl_) return out;

    std::lock_guard<std::mutex> lock(impl_->remote_mtx);
    out.reserve(impl_->channels.size());
    for (const auto & c : impl_->channels) {
        BridgeStat s;
        s.media = media_label(c.id.content, c.id.type);
        s.kind  = kind_label(c.id.kind);

        char local_buf[64];
        std::snprintf(local_buf, sizeof(local_buf), "%s:%u",
                      impl_->advertised_ip.c_str(), c.local_port);
        s.local_endpoint = local_buf;

        s.remote_endpoint = c.remote_set ? ip_port_str(c.remote)
                                         : std::string("(no remote yet)");

        s.tx_packets = c.tx_packets.load(std::memory_order_relaxed);
        s.tx_bytes   = c.tx_bytes  .load(std::memory_order_relaxed);
        s.rx_packets = c.rx_packets.load(std::memory_order_relaxed);
        s.rx_bytes   = c.rx_bytes  .load(std::memory_order_relaxed);
        s.cb_packets         = c.cb_calls         .load(std::memory_order_relaxed);
        s.tx_drops_no_remote = c.tx_drops_no_remote.load(std::memory_order_relaxed);
        s.tx_drops_bad_fd    = c.tx_drops_bad_fd  .load(std::memory_order_relaxed);
        s.tx_drops_zero_size = c.tx_drops_zero_size.load(std::memory_order_relaxed);
        s.tx_drops_send_err  = c.tx_drops_send_err.load(std::memory_order_relaxed);
        s.last_send_errno    = c.last_send_errno  .load(std::memory_order_relaxed);
        out.push_back(std::move(s));
    }
    return out;
}

TransportTotals AppTransport::totals() const
{
    TransportTotals t;
    if (!impl_) return t;
    t.cb_total      = impl_->cb_total     .load(std::memory_order_relaxed);
    t.cb_no_channel = impl_->cb_no_channel.load(std::memory_order_relaxed);
    t.cb_null_data  = impl_->cb_null_data .load(std::memory_order_relaxed);
    t.cb_unbound    = impl_->cb_unbound   .load(std::memory_order_relaxed);
    return t;
}

} // namespace doppler
