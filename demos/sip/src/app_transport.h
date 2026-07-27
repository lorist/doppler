// ============================================================================
//  app_transport.h - own the RTP/RTCP sockets ourselves, bridge to Pulse via
//                    the application-driven transport API.
// ----------------------------------------------------------------------------
//
//  Pulse's app-transport mode (see pulse_options.h:pulse_options_set_app_transport
//  and pulse.h:pulse_app_transport_push) lets the application replace Pulse's
//  built-in port-/ICE-driven UDP transport with sockets it owns:
//
//      Pulse media -> PulseAppPacketCallback -> we sendto() on our socket
//      our socket -> recvfrom() -> pulse_app_transport_push() -> Pulse media
//
//  Each packet is tagged with a PulseAppChannelId identifying which "wire"
//  it flowed on. For SIP non-bundle (which this demo uses; is_sip=true) every
//  m= section gets either one wire (a=rtcp-mux present) or two wires (split
//  RTP + RTCP). We allocate one UDP socket per wire up front, rewrite the
//  offer SDP Pulse hands us in stage 1 so the SIP peer sees *our* ports, and
//  populate a channel -> remote sockaddr table from the SIP answer SDP so
//  the outbound callback knows where to send.
//
//  Lifecycle: construct after pulse_new_external_rest(); bind_to_pulse()
//  before stage 1; configure_local_offer() with the SDP Pulse returned from
//  stage 1; configure_remote_answer() with the SDP the SIP peer returned in
//  the 200 OK before stage 2; destroy before pulse_free().
//
//  Caveats:
//    * IPv4-only (matches the bulk of SIP video deployments).
//    * No SRTP / DTLS - plain RTP, the only thing the app-transport API
//      surfaces today.
//    * The local IP advertised in the rewritten SDP is auto-detected
//      using a connected-UDP-socket probe against 8.8.8.8 (no packets
//      sent - just primes the kernel route so getsockname() yields the
//      egress interface address). Override with the DOPPLER_SIP_LOCAL_IP
//      env var when the auto-detected address is wrong (multi-homed
//      hosts, VPN routing, etc.). Falls back to 127.0.0.1 only if the
//      box has no network at all.
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <pexpulse/pulse.h>
#include <pexpulse/pulse_types.h>

namespace doppler {

// Snapshot of one bridge's live counters + identifiers, returned by
// AppTransport::snapshot() for UI rendering. Cheap to copy; the UI
// thread polls this once per frame and feeds it into an ImGui table.
struct BridgeStat {
    // Identifiers parsed from the SDP / channel id:
    std::string  media;        // "audio" / "video" / "slides"
    std::string  kind;         // "RTP" / "RTCP" / "RTP+RTCP" (mux)
    std::string  local_endpoint;   // "<advertised_ip>:<local_port>"
    std::string  remote_endpoint;  // "<peer_ip>:<peer_port>" or "(no remote yet)"

    // Live counters (snapshotted from atomics).
    uint64_t     tx_packets = 0;
    uint64_t     tx_bytes   = 0;
    uint64_t     rx_packets = 0;
    uint64_t     rx_bytes   = 0;

    // Outbound drop-bucket diagnostics. `cb_packets` is the number of
    // PulseAppPacketCallback invocations the bridge matched to this
    // channel; `tx_packets` is how many of those actually made it onto
    // the wire via sendto(). The difference is broken down below so the
    // UI can show *why* packets were dropped when papa is producing but
    // nothing leaves the socket.
    uint64_t     cb_packets             = 0; // matched callbacks for this channel
    uint64_t     tx_drops_no_remote     = 0; // configure_remote_answer() hadn't run yet
    uint64_t     tx_drops_bad_fd        = 0; // socket was already closed
    uint64_t     tx_drops_zero_size     = 0; // Pulse handed us an empty buffer
    uint64_t     tx_drops_send_err      = 0; // sendto() returned < 0
    int          last_send_errno        = 0; // errno from the most recent failed sendto()
};

// Bridge-wide ("global") counters that aren't attributable to any single
// channel. Returned by AppTransport::totals(). All counters are monotonic
// from the moment bind_to_pulse() was called.
struct TransportTotals {
    uint64_t cb_total           = 0; // every on_outbound() invocation
    uint64_t cb_no_channel      = 0; // callback for a channel id we don't know
    uint64_t cb_null_data       = 0; // callback with NULL/zero-size buffer
    uint64_t cb_unbound         = 0; // callback fired after we cleared client
};

class AppTransport {
public:
    AppTransport();
    ~AppTransport();

    AppTransport(const AppTransport &)             = delete;
    AppTransport & operator=(const AppTransport &) = delete;

    // Register our PulseAppPacketCallback with Pulse. Pulse only accepts
    // this while the session status is UNINITIALIZED, i.e. strictly
    // before pulse_setup_stage_1_from_structure() / _from_response_buffer()
    // (the barrier named in pulse_options.h). After stage 1 the call
    // returns PULSE_ERROR_ALREADY_CONNECTED. The connect_default_devices /
    // data_session_connect_* calls do NOT count as the barrier, so it's
    // fine to bind any time between pulse_new_external_rest() and the
    // first stage 1 - which is the window the UI checkbox exposes.
    //
    // Returns the PulseError from pulse_options_set_app_transport().
    PulseError bind_to_pulse(Pulse * client);

    // Detach our callback from Pulse without tearing down the local UDP
    // sockets or the reader thread, so a later bind_to_pulse() can re-attach
    // cheaply. Same UNINITIALIZED-only window as bind_to_pulse(); after
    // stage 1 Pulse returns PULSE_ERROR_ALREADY_CONNECTED here too.
    // No-op if we never bound (or were already unbound).
    PulseError unbind_from_pulse();

    // Take the SDP offer Pulse returned from stage 1, allocate one local
    // UDP socket per wire it describes, and return a rewritten SDP whose
    // m= ports / c= IP / a=rtcp: lines advertise our owned sockets. The
    // returned string is what we hand to PJSIP for the INVITE body.
    //
    // On any error (bind() failed, malformed SDP, ...) returns the original
    // SDP unchanged and writes a human-readable reason into out_error.
    std::string configure_local_offer(const std::string & pulse_offer_sdp,
                                      std::string &       out_error);

    // Parse the SDP the SIP peer returned in the 200 OK; resolve each m=
    // section to a (content, type) pair using the same ordering rule we
    // used for the local offer, then record the remote IP+port for each
    // PulseAppChannelId. Returns empty on success or a human-readable
    // error otherwise.
    std::string configure_remote_answer(const std::string & remote_answer_sdp);

    // Snapshot of every bridge's identifiers + live counters, in the
    // order the SDP offer introduced them (audio first, then video,
    // RTP before RTCP within a split section). Safe to call from any
    // thread. Returned vector is freshly allocated.
    std::vector<BridgeStat> snapshot() const;

    // Bridge-wide diagnostic counters (callback fan-in totals + the
    // "unattributable" drop buckets). Safe to call from any thread.
    TransportTotals totals() const;

    // Tear sockets down and unbind from Pulse. Safe to call any number of
    // times. Called automatically by the destructor.
    void shutdown();

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace doppler
