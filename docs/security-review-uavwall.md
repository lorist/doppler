# Security review — uavwall

A design review of [`demos/uavwall/`](../demos/uavwall/) against the OWASP Top 10
(2021), plus the native-application risks that list under-weights.

Reviewed at commit `e54a9cd`. Findings were verified against the running app and
the code, not inferred; the check that establishes each one is given.

## Threat model first

The Top 10 assumes a server exposed to untrusted remote users. uavwall is a
single-user desktop application on an operator's workstation, so several
categories do not apply as written. The exposures that matter here are:

1. **Other users and processes on the same machine** — the wall stores
   credentials and writes video to disk.
2. **Media and logs leaving the machine** — recordings are made to be copied off.
3. **The network between the wall, the cameras and Infinity.**
4. **Whoever can dial the registered alias**, once registration is enabled.

For a demonstration on a laptop, most of what follows is low risk. For the
deployed use the demo depicts — a wall carrying ISR video, in a military
context — several become significant. The findings are rated for the second
case, and say so where the two diverge.

## Findings

### 1 · Credentials stored in cleartext, world-readable — **high**

`uavwall.conf` holds `reg_pass` and `pin` in plain text, and RTSP URLs commonly
embed `user:pass@` (the README recommends exactly that for real cameras). The
file is written with the default umask:

```
$ ls -l uavwall.conf
-rw-r--r--  uavwall.conf        # 0644 — any local account can read it
```

`save_config()` rewrites it on exit, so clearing a password by hand does not
persist. The README already warns the password is stored in clear, but the file
mode makes it readable by every account on the machine, not just the operator.

**Fix:** create the file 0600, and refuse to load a config that is group- or
world-readable (or at least warn). Keeping the secret out of the file entirely —
Keychain on macOS, libsecret on Linux — is the stronger answer but a larger
change.

### 2 · Camera credentials leak into recorder log files — **high**

Each recording writes an `ffmpeg` log beside it, `<recording>.mp4.log`. ffmpeg
echoes the input URL on failure, credentials included:

```
$ ffmpeg ... -i "rtsp://secretuser:hunter2@host/path" ...
Error opening input file rtsp://secretuser:hunter2@host/path.
```

That file sits in the recordings folder — the one directory whose whole purpose
is to be copied off the machine after an exercise. A failed recording therefore
exports the camera password along with the footage.

**Fix:** the log exists to diagnose a failed recorder. Write it to a temporary
directory rather than alongside the media, redact anything matching
`//user:pass@`, or drop it once the recorder exits successfully. Whichever, it
should be 0600.

### 3 · Recordings are world-readable — **medium**

`ensure_dir()` creates the folder 0755 and the files land 0644. Anyone with an
account on the machine can read captured ISR video.

**Fix:** 0700 on the directory, 0600 on the files.

### 4 · TLS degrade behaviour is unverified — **unknown, potentially critical**

Pulse exposes `pulse_options_set_tls_degrade_approval_callback()`, whose callback
fires "to acknowledge TLS degrade when connecting to eg. a server that uses a
certificate issued by an unknown CA or a certificate that is expired".

**No demo in this repository sets it**, uavwall included, and the header says only
that a NULL pointer means "the callback function will be disabled" — which does
not say whether a degraded connection is then refused or silently accepted. If it
fails open, every Infinity connection accepts an unknown-CA or expired
certificate without complaint, which is a straightforward man-in-the-middle
exposure on a hostile network.

This is the one finding that could be critical and the one I could not settle
from the code. **It needs testing against a server with a bad certificate.** If
it fails open, uavwall should install a callback that refuses by default and asks
the operator, the way a browser does.

### 5 · `auto_accept` removes the only human check — **medium, by design**

With `auto_accept=true` the wall answers incoming calls with no operator
involvement, so anyone who can dial the alias sees whatever is on the canvas.
Access control is entirely Infinity's — which is the correct place for it — but
the setting turns a two-party decision into a one-party one.

Notably the busy-state path deliberately does *not* auto-accept, so a call in
progress is never dropped without a human. That asymmetry is right; it is worth
documenting as a security property rather than a UX one.

**Fix:** none needed in code. Document that `auto_accept` should only be enabled
where the VMR's own membership controls are trusted, and consider logging every
auto-accepted call.

### 6 · No provenance or integrity on recordings — **medium in the depicted use**

Recordings carry no hash, signature, or manifest, and the filename is the only
record of which feed produced them. For a demonstration this is fine. For
anything evidentiary it is not: there is nothing to show a file was not altered
after capture, and nothing binding it to a source, an operator, or a time beyond
the name.

**Fix, if the use case ever warrants it:** write a sidecar manifest per recording
(source URL with credentials stripped, start/stop UTC, canvas geometry, SHA-256
of the finished file) and consider signing it.

### 7 · No audit trail — **low/medium** (maps to A09)

Nothing durable records what the wall did: which VMR it dialled, which calls it
answered, when recording started and stopped, which feeds were connected. Pulse's
own diagnostics go to stderr and are lost when the terminal closes.

**Fix:** an append-only local log of the handful of security-relevant events
would cost very little and answers "what was this wall doing at 14:05?".

### 8 · `ffmpeg` resolved partly through `PATH` — **low**

`find_ffmpeg()` tries `/opt/homebrew/bin`, `/usr/local/bin`, `/usr/bin` first and
only then walks `PATH`, so the common case is a fixed absolute path. But if none
of the three exists, a `PATH` entry an attacker can write to would have its
`ffmpeg` executed as the operator.

On this machine `/usr/local/bin` is `root:wheel` and not user-writable, so the
exposure is small; that is not guaranteed on every host, and Homebrew-managed
prefixes are often admin-writable.

**Fix:** make the binary path a config key, and prefer it over any search.

## Where the Top 10 maps

| Category | Applies? | Assessment |
| --- | --- | --- |
| **A01 Broken access control** | partly | No app-level authorisation to break — the wall has one local operator. Access to the *conference* is Infinity's, correctly. Finding 5 (`auto_accept`) and finding 3 (file modes) sit here. |
| **A02 Cryptographic failures** | **yes** | Findings 1, 2 and 4. Media in flight is SRTP/TLS via Pulse; the weaknesses are secrets at rest and the unverified certificate posture. |
| **A03 Injection** | **no — verified** | The recorder uses `fork`/`execv` with an argv array. There is no `system()`, `popen()` or `/bin/sh` anywhere in the file, so a hostile feed name or URL cannot reach a shell. Argument injection was tested too: a URL of `-loglevel` is consumed by `-i` as a filename, not re-interpreted as an option. Filenames are built from a `slug()` that keeps only alphanumerics. |
| **A04 Insecure design** | partly | The parked-callback pattern (PIN, incoming call) blocks a Pulse worker until the UI answers, with bounded waits — deliberate and sound. The design weakness is that secrets live in a plain config file the app rewrites on every exit (finding 1). |
| **A05 Security misconfiguration** | **yes** | Default file modes (findings 1–3). The defaults ship permissive and nothing warns. |
| **A06 Vulnerable/outdated components** | **yes** | Not assessed here — it deserves its own pass. The dependency surface is Pulse (with a large bundled GStreamer), Dear ImGui, GLFW, and now ffmpeg as a runtime dependency. ffmpeg in particular has a steady stream of CVEs and is being fed untrusted network media. |
| **A07 Auth failures** | partly | No authentication of its own. Registration is device credentials; PIN entry is offered once per dial and then prompts, so it cannot loop on a wrong PIN. Rate limiting is Infinity's. |
| **A08 Software/data integrity** | partly | Findings 6 and 8. Nothing is downloaded or auto-updated at runtime; the one externally-resolved executable is ffmpeg. |
| **A09 Logging/monitoring failures** | **yes** | Finding 7, and finding 2 is a logging failure in the other direction — logging something it should not. |
| **A10 SSRF** | **not meaningfully** | The operator can point the app at any RTSP URL or VMR, which is the entire purpose. There is no untrusted party supplying those URLs, so there is no confused deputy — unless a config file from an untrusted source is ever loaded, which would make finding 8 and this row real. |

## Native risks the Top 10 misses

* **Memory safety.** C++ with fixed-size buffers throughout. No `strcpy`,
  `strcat`, `sprintf`, `gets` or `alloca` appear anywhere in `main.cpp`;
  formatting is `snprintf` with `sizeof` bounds, and parsing uses bounded
  `sscanf` on `std::string` substrings. The largest untrusted-input surface is
  not this code at all — it is the H.264 decoder inside Pulse's GStreamer, and
  ffmpeg's demuxers, both being handed network media.
* **Untrusted media is the real attack surface.** Every feed is attacker-supplied
  data if a camera or the network is compromised. That risk is inherited from the
  decoders, and the mitigation is patching them (A06), not anything in this file.
* **Subprocess lifetime.** Recorders are child processes holding a pipe. They are
  reaped, bounded and killed on exit, so a wedged encoder cannot outlive the app
  silently — but `kill -9` on the app orphans them.

## Suggested order

Cheap and worth doing regardless of deployment:

1. **Verify the TLS degrade behaviour** (finding 4). Everything else is a
   known quantity; this one is not, and it is the only candidate for critical.
2. **Stop leaking camera credentials into the recordings folder** (finding 2) —
   a few lines, removes an exposure from the artefact most likely to be shared.
3. **Tighten file modes** (findings 1 and 3) — 0600 on the config, 0700 on the
   recordings directory.

Then, only if the deployed use case is real rather than demonstrative: a
recording manifest (6), an audit log (7), a configured ffmpeg path (8), and a
dependency/CVE pass (A06).

None of these are exploitable by a remote party against the demo as it stands
today. They matter to the extent that the demo is taken as a template for
something fielded — which, given the audience, is a reasonable assumption.
