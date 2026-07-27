// ============================================================================
//  sip_ua.cpp — minimal PJSIP wrapper for the doppler sip-demo
// ----------------------------------------------------------------------------
//
//  The implementation is intentionally tiny: one PJSIP endpoint, one TCP
//  transport, one outbound dialog at a time, no REGISTER. The flow is
//  modelled on PJSIP's own `samples/simpleua.c`, but stripped down to just
//  the UAC (caller) side and with all media handed off to the caller
//  via SDP strings.
// ============================================================================

#include "sip_ua.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// PJSIP headers. The umbrella `pjsua.h` would pull in pjmedia too, which we
// don't need — we only want signalling + SDP parsing.
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjmedia/sdp.h>
#include <pjmedia/sdp_neg.h>

namespace doppler {

// ----------------------------------------------------------------------------
// Internal state
// ----------------------------------------------------------------------------
struct SipUA::Impl {
    // Endpoint + worker plumbing.
    pj_caching_pool   cp{};
    pjsip_endpoint *  endpt        = nullptr;
    pj_pool_t *       pool         = nullptr;     // long-lived pool
    pj_thread_t *     worker       = nullptr;
    std::atomic<bool> quit{false};
    bool              pjlib_inited = false;

    // Transport / identity.
    std::string user_agent;
    std::string local_ip;   // what we put in the Contact header (best-effort)
    int         local_port  = 0;

    // The single active call, if any. Protected by mutex; all PJSIP touches
    // outside the worker thread must hold it (and pj_thread_register first).
    std::mutex          call_mutex;
    pjsip_inv_session * inv         = nullptr;
    std::string         active_call_id;          // populated on outbound INVITE
    bool                got_answer  = false;     // 200 OK with SDP delivered
    AnswerCallback      on_answer;
    FailureCallback     on_failure;
    EndedCallback       on_ended;

    // Registered modules (kept around so we can deregister cleanly).
    pjsip_module mod_app{};
};

// Local aliases for the SipUA member typedefs, so file-scope helpers can
// name them without `SipUA::` qualification everywhere.
using AnswerCallback  = SipUA::AnswerCallback;
using FailureCallback = SipUA::FailureCallback;
using EndedCallback   = SipUA::EndedCallback;

// We need a single back-pointer from PJSIP's invite-callbacks into our Impl.
// The pjsip_inv_session has a `mod_data[mod_id]` slot per module — we use
// our `mod_app` module's id for that. The callback recovers it via a
// linear scan over mod_data (one slot is populated; see cb_on_state_changed).

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

// Make sure the current OS thread can call into PJSIP. Idempotent.
static void register_thread()
{
    static thread_local pj_thread_desc desc;
    static thread_local pj_thread_t *  th = nullptr;
    if (!pj_thread_is_registered())
        pj_thread_register("ext", desc, &th);
}

// Best-effort: figure out a local IPv4 address to use in our Contact / SDP
// origin. Falls back to 127.0.0.1 if PJSIP can't enumerate interfaces.
static std::string discover_local_ip()
{
    pj_sockaddr addr;
    pj_status_t st = pj_gethostip(pj_AF_INET(), &addr);
    if (st != PJ_SUCCESS) return "127.0.0.1";
    char buf[PJ_INET6_ADDRSTRLEN];
    pj_sockaddr_print(&addr, buf, sizeof(buf), 0);
    return buf;
}

// Stringify a PJSIP status code into a readable error message.
static std::string pj_err(pj_status_t st)
{
    char buf[PJ_ERR_MSG_SIZE];
    pj_strerror(st, buf, sizeof(buf));
    return std::string(buf);
}

// pj_str_t (which is not NUL-terminated) -> std::string.
static std::string to_std(const pj_str_t & s)
{
    return s.ptr ? std::string(s.ptr, s.slen) : std::string();
}

// Make sure target is "sip:..." rather than just "alice@example.com", and
// pin the transport to TCP unless the caller has already specified one.
// We only register a TCP SIP transport (see start()), so a URI without a
// transport= parameter would otherwise leave PJSIP guessing (and on a SIP
// URI without parameters it defaults to UDP, which we don't support).
static std::string normalise_uri(const std::string & u)
{
    std::string out = u;
    if (out.rfind("sip:", 0) != 0 && out.rfind("sips:", 0) != 0)
        out = "sip:" + out;
    // sips: implies TLS, leave it alone.
    if (out.rfind("sips:", 0) == 0) return out;
    if (out.find("transport=") == std::string::npos)
        out += ";transport=tcp";
    return out;
}

// ----------------------------------------------------------------------------
// pjsip_inv_callback wiring
// ----------------------------------------------------------------------------
//
// PJSIP fires these from its worker thread (= our worker thread).
// We forward state changes into the user callbacks. The Impl pointer comes
// out of inv->mod_data[mod_id], stuffed there at place_call() time.
// ----------------------------------------------------------------------------

static void cb_on_state_changed(pjsip_inv_session * inv, pjsip_event * /*e*/);
static void cb_on_new_session(pjsip_inv_session *, pjsip_event *) {}
static void cb_on_media_update(pjsip_inv_session * inv, pj_status_t status);

// We don't use designated initializers (they're C++20) and PJSIP's struct
// has more fields than we want to enumerate by order, so we zero it and
// set the two slots we actually care about by name.
static pjsip_inv_callback g_inv_cb = {};
static void install_inv_callbacks()
{
    g_inv_cb.on_state_changed = &cb_on_state_changed;
    g_inv_cb.on_new_session   = &cb_on_new_session;
    g_inv_cb.on_media_update  = &cb_on_media_update;
}

// Pretty status -> string for the user-facing failure callback.
static std::string status_text(int code, const pj_str_t & reason)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%d %.*s",
                  code, (int)reason.slen, reason.ptr ? reason.ptr : "");
    return buf;
}

// Fires both on success and on failure paths. We use the inv state machine:
//   CONFIRMED   -> success (200 OK + ACK sent). Pull SDP + Call-ID.
//   DISCONNECTED-> end. If we never delivered on_answer, this was a failure.
static void cb_on_state_changed(pjsip_inv_session * inv, pjsip_event * /*e*/)
{
    // Recover our Impl* from the inv's mod_data slot. We don't have mod_app's
    // id in scope here, so walk the array — at most one slot is populated.
    SipUA::Impl * impl = nullptr;
    for (unsigned i = 0; i < PJ_ARRAY_SIZE(inv->mod_data); ++i) {
        if (inv->mod_data[i]) {
            impl = static_cast<SipUA::Impl *>(inv->mod_data[i]);
            break;
        }
    }
    if (!impl) return;

    if (inv->state == PJSIP_INV_STATE_CONFIRMED && !impl->got_answer) {
        // Pin the dialog to the TCP transport that carried this call so
        // PJSIP's transport-idle timer can't destroy the connection mid-call.
        //
        // Background: pjsip_tcp_transport_start() installs the usual
        // PJSIP_TRANSPORT_IDLE_TIME watchdog (default 600s). PJSIP only
        // ref-counts the transport for the duration of in-flight
        // transactions — INVITE/ACK hold a ref for ~32s, then drop to zero.
        // With no further SIP traffic between the ACK and the eventual BYE,
        // the idle timer fires after ~10 minutes and we see
        //   "Transport tcp ... is being destroyed due to timeout in idle timer"
        // which kills the signalling leg out from under the active call.
        //
        // pjsip_dlg_set_transport(TPSELECTOR_TRANSPORT) takes a reference
        // on the transport via the dialog's tp_sel; that ref is held until
        // the dialog is destroyed (i.e. when the call ends), which keeps
        // the idle timer disarmed for the entire call lifetime.
        if (inv->invite_tsx && inv->invite_tsx->transport) {
            pjsip_tpselector sel;
            pj_bzero(&sel, sizeof(sel));
            sel.type        = PJSIP_TPSELECTOR_TRANSPORT;
            sel.u.transport = inv->invite_tsx->transport;
            pj_status_t pst = pjsip_dlg_set_transport(inv->dlg, &sel);
            if (pst == PJ_SUCCESS) {
                std::fprintf(stderr,
                    "SIP: pinned dialog to transport %s to keep TCP alive "
                    "for the call lifetime\n",
                    inv->invite_tsx->transport->info);
            } else {
                std::fprintf(stderr,
                    "SIP: pjsip_dlg_set_transport failed (%s); call may be "
                    "torn down by PJSIP's TCP idle timer\n",
                    pj_err(pst).c_str());
            }
        } else {
            std::fprintf(stderr,
                "SIP: no invite_tsx/transport available at CONFIRMED; "
                "skipping transport pin (TCP idle timer may fire mid-call)\n");
        }

        // The negotiator now has an active answer; grab it.
        const pjmedia_sdp_session * remote = nullptr;
        if (inv->neg &&
            pjmedia_sdp_neg_get_active_remote(inv->neg, &remote) == PJ_SUCCESS &&
            remote)
        {
            char  buf[4096];
            int   n = pjmedia_sdp_print(remote, buf, sizeof(buf));
            if (n > 0) {
                std::fprintf(stderr,
                    "===== SIP: incoming answer SDP (%d bytes) =====\n%.*s"
                    "===== end SDP =====\n",
                    n, n, buf);
                SipAnswer ans;
                ans.remote_sdp.assign(buf, static_cast<size_t>(n));
                ans.call_id = impl->active_call_id;
                impl->got_answer = true;
                if (impl->on_answer) impl->on_answer(ans);
            }
        }
    }
    else if (inv->state == PJSIP_INV_STATE_DISCONNECTED) {
        const int code = inv->cause;
        const std::string reason = status_text(code, inv->cause_text);
        // Take and clear the active-call state under the lock so a re-entrant
        // place_call() doesn't race the user callbacks.
        AnswerCallback  ans_cb;
        FailureCallback fail_cb;
        EndedCallback   end_cb;
        bool was_answered = false;
        {
            std::lock_guard<std::mutex> lk(impl->call_mutex);
            if (impl->inv == inv) {
                ans_cb  = std::move(impl->on_answer);
                fail_cb = std::move(impl->on_failure);
                end_cb  = std::move(impl->on_ended);
                was_answered = impl->got_answer;
                impl->on_answer  = {};
                impl->on_failure = {};
                impl->on_ended   = {};
                impl->inv        = nullptr;
                impl->got_answer = false;
                impl->active_call_id.clear();
            }
        }
        if (was_answered) {
            if (end_cb)  end_cb(reason);
        } else {
            if (fail_cb) fail_cb(reason);
        }
    }
}

// PJSIP normally drives its own media transport here. We don't have one, so
// we don't do anything — but if PJSIP reports a negotiation failure we want
// that to surface as a hangup.
static void cb_on_media_update(pjsip_inv_session * inv, pj_status_t status)
{
    if (status == PJ_SUCCESS) return;
    pjsip_tx_data * tdata = nullptr;
    if (pjsip_inv_end_session(inv, 488, nullptr, &tdata) == PJ_SUCCESS && tdata)
        pjsip_inv_send_msg(inv, tdata);
}

// ----------------------------------------------------------------------------
// Worker thread
// ----------------------------------------------------------------------------
//
// All we do here is pump PJSIP events. The endpoint is internally
// thread-safe but it does need *some* thread to call handle_events().
// ----------------------------------------------------------------------------
static int worker_thread_fn(void * arg)
{
    auto * impl = static_cast<SipUA::Impl *>(arg);
    while (!impl->quit.load()) {
        pj_time_val timeout{0, 100};   // 100 ms
        pjsip_endpt_handle_events(impl->endpt, &timeout);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// SipUA implementation
// ----------------------------------------------------------------------------

SipUA::SipUA() : impl_(new Impl()) {}

SipUA::~SipUA() { stop(); }

std::string SipUA::start(const std::string & user_agent, int local_port)
{
    Impl & m = *impl_;
    if (m.endpt) return "SipUA already started";

    pj_status_t st = pj_init();
    if (st != PJ_SUCCESS) return "pj_init: " + pj_err(st);
    m.pjlib_inited = true;

    st = pjlib_util_init();
    if (st != PJ_SUCCESS) { stop(); return "pjlib_util_init: " + pj_err(st); }

    pj_caching_pool_init(&m.cp, &pj_pool_factory_default_policy, 0);

    st = pjsip_endpt_create(&m.cp.factory, nullptr, &m.endpt);
    if (st != PJ_SUCCESS) { stop(); return "pjsip_endpt_create: " + pj_err(st); }

    m.pool = pj_pool_create(&m.cp.factory, "sipua", 4096, 4096, nullptr);

    // TCP transport. Pexip Infinity prefers TCP for SIP signalling, and
    // typical INVITE offers (multiple video codecs + ICE candidates) easily
    // exceed PJSIP's UDP MTU threshold (~1300 bytes), at which point UDP
    // would fail anyway. We therefore use TCP exclusively for SIP — RTP
    // media still flows over UDP, but that's handled by Pulse, not PJSIP.
    pj_sockaddr_in tcp_addr;
    pj_bzero(&tcp_addr, sizeof(tcp_addr));
    tcp_addr.sin_family = pj_AF_INET();
    tcp_addr.sin_addr.s_addr = 0;
    tcp_addr.sin_port = pj_htons(static_cast<pj_uint16_t>(local_port));
    st = pjsip_tcp_transport_start(m.endpt, &tcp_addr, 1, nullptr);
    if (st != PJ_SUCCESS) { stop(); return "pjsip_tcp_transport_start: " + pj_err(st); }

    // DNS resolver. Without one PJSIP falls back to getaddrinfo() (and
    // logs "DNS resolver not available"), which can only return A/AAAA
    // records — no SRV/NAPTR — so it can't honour the _sip._tcp.<domain>
    // SRV records that Pexip (and most SIP infra) publish. Once a
    // pj_dns_resolver is attached to the endpoint, PJSIP's pjsip_resolve()
    // automatically performs RFC 3263 resolution: with ;transport=tcp on
    // the URI that's an SRV lookup of _sip._tcp.<domain> followed by the
    // A/AAAA for the chosen target, which is exactly what we want.
    //
    // We seed the resolver with the system's nameservers parsed out of
    // /etc/resolv.conf; if that file is missing or empty we fall back to
    // a public resolver so we at least get _something_ working.
    {
        pj_dns_resolver * resolver = nullptr;
        st = pjsip_endpt_create_resolver(m.endpt, &resolver);
        if (st != PJ_SUCCESS) {
            stop(); return "pjsip_endpt_create_resolver: " + pj_err(st);
        }

        std::vector<std::string> ns;
        if (FILE * f = std::fopen("/etc/resolv.conf", "r")) {
            char line[512];
            while (std::fgets(line, sizeof(line), f)) {
                if (line[0] == '#' || line[0] == ';') continue;
                char kw[32] = {0}, ip[256] = {0};
                if (std::sscanf(line, "%31s %255s", kw, ip) == 2
                        && std::strcmp(kw, "nameserver") == 0
                        && ip[0] != '\0') {
                    ns.emplace_back(ip);
                }
            }
            std::fclose(f);
        }
        if (ns.empty()) ns.emplace_back("8.8.8.8");

        std::vector<pj_str_t> srv(ns.size());
        for (size_t i = 0; i < ns.size(); ++i)
            srv[i] = pj_str(const_cast<char *>(ns[i].c_str()));

        st = pj_dns_resolver_set_ns(resolver,
                                    static_cast<unsigned>(srv.size()),
                                    srv.data(), nullptr);
        if (st != PJ_SUCCESS) {
            stop(); return "pj_dns_resolver_set_ns: " + pj_err(st);
        }

        st = pjsip_endpt_set_resolver(m.endpt, resolver);
        if (st != PJ_SUCCESS) {
            stop(); return "pjsip_endpt_set_resolver: " + pj_err(st);
        }
    }

    // Init transaction layer + UA + invitation session usage.
    st = pjsip_tsx_layer_init_module(m.endpt);
    if (st != PJ_SUCCESS) { stop(); return "tsx_layer_init: " + pj_err(st); }
    st = pjsip_ua_init_module(m.endpt, nullptr);
    if (st != PJ_SUCCESS) { stop(); return "ua_init: " + pj_err(st); }
    install_inv_callbacks();
    st = pjsip_inv_usage_init(m.endpt, &g_inv_cb);
    if (st != PJ_SUCCESS) { stop(); return "inv_usage_init: " + pj_err(st); }
    st = pjsip_100rel_init_module(m.endpt);
    if (st != PJ_SUCCESS) { stop(); return "100rel_init: " + pj_err(st); }

    // Register a dummy module just so we get a mod_id to stash our Impl
    // pointer in via inv->mod_data[]. The name is kept by reference by
    // PJSIP, so it must outlive the endpoint — we store it in a static
    // mutable buffer rather than const-casting a string literal.
    static char mod_name[] = "sipua-app";
    m.mod_app.name     = pj_str(mod_name);
    m.mod_app.id       = -1;
    m.mod_app.priority = PJSIP_MOD_PRIORITY_APPLICATION;
    st = pjsip_endpt_register_module(m.endpt, &m.mod_app);
    if (st != PJ_SUCCESS) { stop(); return "register_module: " + pj_err(st); }

    m.user_agent = user_agent.empty() ? "doppler-sip" : user_agent;
    m.local_ip   = discover_local_ip();
    m.local_port = local_port;

    // Spin up the event-pumping thread.
    m.quit.store(false);
    st = pj_thread_create(m.pool, "sipua-worker",
                          &worker_thread_fn, &m, 0, 0, &m.worker);
    if (st != PJ_SUCCESS) { stop(); return "pj_thread_create: " + pj_err(st); }

    return {};
}

void SipUA::stop()
{
    Impl & m = *impl_;
    // End any active call cleanly.
    hangup();

    // Stop worker.
    if (m.worker) {
        m.quit.store(true);
        pj_thread_join(m.worker);
        m.worker = nullptr;
    }
    if (m.endpt) {
        register_thread();
        // Give the stack a beat to flush queued messages (BYE etc).
        for (int i = 0; i < 10; ++i) {
            pj_time_val t{0, 50};
            pjsip_endpt_handle_events(m.endpt, &t);
        }
        pjsip_endpt_destroy(m.endpt);
        m.endpt = nullptr;
    }
    if (m.pool) {
        pj_pool_release(m.pool);
        m.pool = nullptr;
    }
    // pj_caching_pool_destroy is safe to call even if init wasn't, but only
    // if pjlib was initialised.
    if (m.pjlib_inited) {
        pj_caching_pool_destroy(&m.cp);
        pj_shutdown();
        m.pjlib_inited = false;
    }
}

bool SipUA::place_call(const std::string & target_uri,
                       const std::string & local_offer,
                       const std::string & from_display,
                       AnswerCallback   on_answer,
                       FailureCallback  on_failure,
                       EndedCallback    on_ended)
{
    Impl & m = *impl_;
    if (!m.endpt) {
        if (on_failure) on_failure("SipUA not started");
        return false;
    }
    register_thread();

    std::lock_guard<std::mutex> lk(m.call_mutex);
    if (m.inv) {
        if (on_failure) on_failure("Another call is already in progress");
        return false;
    }

    const std::string target = normalise_uri(target_uri);
    char from_buf[512];
    std::snprintf(from_buf, sizeof(from_buf),
                  "\"%s\" <sip:doppler@%s:%d>",
                  from_display.empty() ? "doppler" : from_display.c_str(),
                  m.local_ip.c_str(),
                  m.local_port > 0 ? m.local_port : 5060);
    char contact_buf[256];
    std::snprintf(contact_buf, sizeof(contact_buf),
                  "<sip:doppler@%s:%d>",
                  m.local_ip.c_str(),
                  m.local_port > 0 ? m.local_port : 5060);

    pj_str_t from_s    = pj_str(from_buf);
    pj_str_t contact_s = pj_str(contact_buf);
    // pj_str_t is non-const by API; the target buffer lives on our stack so
    // PJSIP can safely keep the reference for the duration of this function
    // (it copies into pool-allocated storage before returning).
    char target_buf[512];
    std::snprintf(target_buf, sizeof(target_buf), "%s", target.c_str());
    pj_str_t target_s  = pj_str(target_buf);

    // Dialog.
    pjsip_dialog * dlg = nullptr;
    pj_status_t st = pjsip_dlg_create_uac(pjsip_ua_instance(),
                                          &from_s, &contact_s,
                                          &target_s, &target_s,
                                          &dlg);
    if (st != PJ_SUCCESS) {
        if (on_failure) on_failure("dlg_create_uac: " + pj_err(st));
        return false;
    }

    // Parse caller-supplied SDP.
    pjmedia_sdp_session * sdp = nullptr;
    st = pjmedia_sdp_parse(dlg->pool, const_cast<char*>(local_offer.data()),
                           local_offer.size(), &sdp);
    if (st != PJ_SUCCESS) {
        // Couldn't even parse the offer — bail. The dialog's pool gets
        // reclaimed when the dialog is destroyed, which happens because
        // we never associated it with an inv session.
        pjsip_dlg_terminate(dlg);
        if (on_failure) on_failure("pjmedia_sdp_parse: " + pj_err(st));
        return false;
    }
    st = pjmedia_sdp_validate(sdp);
    if (st != PJ_SUCCESS) {
        pjsip_dlg_terminate(dlg);
        if (on_failure) on_failure("pjmedia_sdp_validate: " + pj_err(st));
        return false;
    }

    // Log the outgoing offer SDP exactly as PJSIP will serialise it on the
    // wire (re-printing the parsed session rather than echoing local_offer
    // makes any normalisation PJSIP applies visible). Useful for verifying
    // codecs, ICE candidates, etc. against what Infinity actually sees.
    {
        char  buf[4096];
        int   n = pjmedia_sdp_print(sdp, buf, sizeof(buf));
        if (n > 0) {
            std::fprintf(stderr,
                "===== SIP: outgoing offer SDP (%d bytes) =====\n%.*s"
                "===== end SDP =====\n",
                n, n, buf);
        }
    }

    // Invitation session.
    pjsip_inv_session * inv = nullptr;
    st = pjsip_inv_create_uac(dlg, sdp, 0, &inv);
    if (st != PJ_SUCCESS) {
        pjsip_dlg_terminate(dlg);
        if (on_failure) on_failure("inv_create_uac: " + pj_err(st));
        return false;
    }

    // Stash our Impl pointer so the callbacks can find their way back to us.
    inv->mod_data[m.mod_app.id] = &m;

    // Build INVITE request and send.
    pjsip_tx_data * tdata = nullptr;
    st = pjsip_inv_invite(inv, &tdata);
    if (st != PJ_SUCCESS) {
        pjsip_inv_terminate(inv, 500, PJ_FALSE);
        if (on_failure) on_failure("inv_invite: " + pj_err(st));
        return false;
    }
    // Tag with our User-Agent header. The pjsip_generic_string_hdr_create
    // call copies both name and value into tdata->pool, so the mutable
    // buffers we hand it only need to outlive the call itself.
    char ua_name_buf[] = "User-Agent";
    char ua_val_buf[128];
    std::snprintf(ua_val_buf, sizeof(ua_val_buf), "%s", m.user_agent.c_str());
    pj_str_t ua_name = pj_str(ua_name_buf);
    pj_str_t ua_val  = pj_str(ua_val_buf);
    pjsip_hdr * ua_hdr = (pjsip_hdr *)pjsip_generic_string_hdr_create(
        tdata->pool, &ua_name, &ua_val);
    if (ua_hdr) pjsip_msg_add_hdr(tdata->msg, ua_hdr);

    st = pjsip_inv_send_msg(inv, tdata);
    if (st != PJ_SUCCESS) {
        if (on_failure) on_failure("inv_send_msg: " + pj_err(st));
        return false;
    }

    // Commit the state so callbacks can find us.
    m.inv             = inv;
    m.active_call_id  = to_std(dlg->call_id->id);
    m.got_answer      = false;
    m.on_answer       = std::move(on_answer);
    m.on_failure      = std::move(on_failure);
    m.on_ended        = std::move(on_ended);
    return true;
}

void SipUA::hangup()
{
    Impl & m = *impl_;
    if (!m.endpt) return;
    register_thread();
    pjsip_inv_session * inv = nullptr;
    {
        std::lock_guard<std::mutex> lk(m.call_mutex);
        inv = m.inv;
    }
    if (!inv) return;

    pjsip_tx_data * tdata = nullptr;
    // 200 here is the "graceful end" code that pjsip_inv_end_session maps to
    // CANCEL for early dialogs and BYE for confirmed ones — exactly what we
    // want.
    if (pjsip_inv_end_session(inv, 200, nullptr, &tdata) == PJ_SUCCESS && tdata)
        pjsip_inv_send_msg(inv, tdata);
}

} // namespace doppler
