// ============================================================================
//  sip_ua.h — minimal PJSIP wrapper for the doppler sip-demo
// ----------------------------------------------------------------------------
//
//  This wraps just enough of PJSIP to do exactly one thing: place a single
//  outbound SIP INVITE over TCP carrying a caller-supplied SDP offer, get
//  the answer SDP back, and tear it down with BYE on request.
//
//  No REGISTER, no inbound calls, no UDP, no TLS, no SIP credentials, no
//  media — Pulse owns all of that. PJSIP is purely a signalling transport
//  here. TCP-only is a deliberate choice: Pexip Infinity prefers TCP for
//  SIP, and a video INVITE easily exceeds the UDP MTU threshold.
//
//  All PJSIP work happens on an internal worker thread. The user supplies
//  three callbacks (answer / failed / ended) which are invoked from that
//  worker thread, so implementations must be thread-safe.
// ============================================================================
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace doppler {

// What we tell the caller about a successful call setup. The remote SDP
// answer is what we hand to pulse_setup_stage_2_from_structure(), and the
// call_id is the SIP Call-ID header value, which we (for now) use as the
// call_uuid field of the same struct.
struct SipAnswer {
    std::string remote_sdp;
    std::string call_id;
};

class SipUA {
public:
    // Callbacks fire on PJSIP's worker thread.
    using AnswerCallback = std::function<void(const SipAnswer &)>;
    using FailureCallback = std::function<void(const std::string & reason)>;
    using EndedCallback = std::function<void(const std::string & reason)>;

    SipUA();
    ~SipUA();

    SipUA(const SipUA &)             = delete;
    SipUA & operator=(const SipUA &) = delete;

    // Bring PJSIP up (TCP transport, worker thread). Returns empty string
    // on success, or a human-readable error message on failure.
    //
    // user_agent is sent in the SIP User-Agent header.
    // local_port is the TCP port to bind for SIP; 0 picks any free port.
    std::string start(const std::string & user_agent, int local_port = 0);

    // Tear PJSIP back down. Safe to call even after a failed start().
    void stop();

    // Place an outbound call.
    //
    //   target_uri   - "alice@example.com" or "sip:alice@example.com"
    //                  (we prepend "sip:" if missing)
    //   local_offer  - the SDP offer to put in the INVITE body
    //   from_display - what to put in the From header's display name
    //
    // Exactly one of {on_answer, on_failure} will fire per place_call() call:
    // on_answer when the remote sends a 200 OK with an SDP body, on_failure
    // for any error before that (DNS, transport, 4xx/5xx/6xx, timeout, ...).
    // After on_answer fires, on_ended will eventually fire (BYE in/out, or
    // a transport failure mid-call). Returns false synchronously if the
    // call couldn't even be created (eg invalid URI).
    bool place_call(const std::string & target_uri,
                    const std::string & local_offer,
                    const std::string & from_display,
                    AnswerCallback   on_answer,
                    FailureCallback  on_failure,
                    EndedCallback    on_ended);

    // Hang up the currently-active call (sends SIP BYE if confirmed, or
    // CANCEL if still in early dialog). No-op if there is no active call.
    void hangup();

    // PIMPL — Impl is declared public so file-scope helpers in sip_ua.cpp
    // can name `SipUA::Impl*`. Its definition lives in sip_ua.cpp so the
    // PJSIP headers stay out of the public API.
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace doppler
