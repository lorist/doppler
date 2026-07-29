# Handoff: uavwall — "Instrument" UI restyle

## Overview

A restyle of the **uavwall** demo (`demos/uavwall/`) — the operator wall that pulls
RTSP UAV downlinks, arranges them on a canvas, and pushes that canvas into a Pexip
VMR. The layout and feature set of the current build are kept: top control area,
feed rail on the left, send canvas filling the rest, status footer. What changes is
**grouping, typography, surfaces, and state signalling**, plus five features the
current build lacks.

The direction is called **Instrument**: an operator tool that reads like
instrumentation rather than a generic dark app. Concretely:

* Controls are collected into four **labelled groups** (DESTINATION · LAYOUT ·
  SOURCES · VIEW) inside one panel, separated by hairline dividers — instead of one
  undifferentiated row of pills.
* All numbers are **monospace**; all labels are small, bold, tracked uppercase.
  Prose and names stay DM Sans.
* **Squared 7px controls** (segmented groups) replace fully-rounded pills. Only the
  primary action keeps colour fill.
* Sending state is signalled by the **window itself**: a 3px red strip along the top
  edge, an ON AIR readout in the header, and red corner ticks + hairline on the
  canvas. Idle simply omits all three.
* The status footer becomes **discrete metric cells** (label + mono value) instead of
  a run of concatenated text.

### New features designed in

1. **Per-feed connect/disconnect** — a toggle on every rail card and every row of
   Settings → Feeds (README currently lists this as *Planned*).
2. **Clearer sending state** — the tally treatment described above.
3. **Saved layout presets** — three slots (A/B/C) beside the layout segmented control.
4. **Feed inspector** — a panel at the bottom of the rail showing the selected feed's
   URL, transport, uptime, last-frame age, with Reconnect / Remove.
5. **Feed-loss alert** — an amber bar between the control deck and the body when a
   connected feed stops delivering frames, with Mute alerts / Dismiss.

## About the design files

`UAV Wall.dc.html` in this bundle is a **design reference written in HTML**. It is a
static mock — it is not code to port. The implementation target is the existing
**C++ / Dear ImGui** application in `demos/uavwall/src/main.cpp`, using its existing
patterns: `ImGui::BeginChild` panels, `ImDrawList` drawing for custom chrome, real
`ImGui::Begin` windows for overlays, and the tokens in `demos/uavwall/src/Theme.h`.

Open the HTML file in a browser to read values off the mock. It contains, top to
bottom:

| Section | id | What it shows |
| --- | --- | --- |
| Turn 2 | `2a` | Main wall, **idle** — feeds live, nothing sending (1440×900) |
| | `2b` | Main wall, **nine feeds, 3×3**, sending as content (1920×1080) |
| | `2c` | Settings → **Canvas & sending** and **Feeds** panels |
| Turn 1 | `1a` | Main wall, **live**, 2×2, with a feed-loss alert (1440×900) |
| | `1b`, `1c` | Two rejected directions — ignore, kept for reference |
| | `1d` | **Settings → Conference**, incoming-call banners, PIN, feed error, idle chip |
| Current build | `current` | Faithful recreation of today's UI, for before/after |

**Build from `2a`, `2b`, `1a`, `1d` and `2c`.** `1b` and `1c` are not the chosen
direction.

## Fidelity

**High-fidelity.** Colours, type sizes, spacing and radii are all specified below and
are all present in the mock. Recreate them exactly. Where a value is not listed,
measure it off the HTML.

The mock is rendered at the app's default **`ui_scale = 1.2`**, so the pixel values in
its markup are *screen* pixels at that scale. This document gives **design units**
(`mock px ÷ 1.2`, rounded), which is what the code should use:

* every text size goes through `theme::fs(design_size)`;
* every **new** layout metric is multiplied by `theme::scale`.

(The current code scales some metrics — `rail_w` — and leaves others raw — pill
widths. Scaling all new metrics is deliberate: the design must hold at both a
1440×900 laptop and a control-room display, and `2b` is the proof.)

## Design tokens

### Already in `Theme.h` — unchanged, reuse as-is

```
AccentPrimary   0x5B6CE8      StatusOnline    0x22C55E
AccentHover     0x6D7CEB      StatusError     0xEF4444
AccentPressed   0x4A5BD6      StarActive      0xF59E0B   (used here as "warn/amber")
WindowBgStart   0x0D1220      WindowBgMid     0x111827

PanelFill        white @ 0.05      TextPrimary    white @ 0.90
PanelFillRaised  white @ 0.07      TextSecondary  white @ 0.50
PanelStroke      white @ 0.09      TextTertiary   white @ 0.32
Divider          white @ 0.05      TextLabel      white @ 0.28
```

### To add to `Theme.h`

```cpp
/* --- Instrument additions ------------------------------------------------ */
constexpr unsigned int StatusWarn   = 0xF59E0B; /* alias of StarActive, read as amber */

constexpr float ControlStroke = 0.12f;  /* white @ — segmented + field borders   */
constexpr float TileStroke    = 0.14f;  /* white @ — canvas tile dividers        */
constexpr float ReticleStroke = 0.26f;  /* white @ — placeholder reticle only    */

/* Radii — the Instrument set is tighter than the frosted panel radii. */
constexpr float RadiusDeck    = 12.0f;  /* control deck, rail, canvas pane       */
constexpr float RadiusControl2 = 7.0f;  /* segmented controls, fields, buttons   */
constexpr float RadiusChip     = 5.0f;  /* preset slots, slot-number badges      */
constexpr float RadiusThumb    = 6.0f;  /* rail thumbnails                       */
constexpr float RadiusFooter   = 10.0f; /* metric-cell strip                     */

/* Metrics, design units — multiply by theme::scale at use. */
constexpr float TallyStrip   = 3.0f;   /* NOT scaled — a hairline is a hairline  */
constexpr float ControlH     = 25.0f;  /* every control in the deck              */
constexpr float RailW        = 218.0f;
constexpr float FooterH      = 32.0f;
constexpr float GroupPadX    = 13.0f;  /* horizontal padding inside a deck group */
constexpr float DeckPadY     = 8.0f;
constexpr float Gap          = 10.0f;  /* between panels                         */
constexpr float GapTight     = 6.0f;   /* inside a group / between cards         */
```

### Fonts

`Theme.h`'s existing DM Sans roles are unchanged and still cover names, prose and
buttons. Two additions:

```cpp
struct Fonts {
  /* … existing: body, bodyBold, small_, smallMed, label, title … */
  ImFont * mono     = nullptr;  /* 400 @ 10.5 — every numeric readout       */
  ImFont * microCap = nullptr;  /* 700 @  8.5 + 1.0 tracking — cell labels  */
};
```

* `microCap` is DM Sans Bold, same file as `label`, just smaller with more tracking.
* **`mono` needs a font file that is not yet in the repo.** Add
  **IBM Plex Mono Regular** (SIL OFL 1.1, same licence class as the bundled DM Sans)
  to `demos/uavwall/assets/fonts/`, next to the DM Sans TTFs and its LICENSE file,
  and load it in `theme::LoadFonts()` with the same
  `content_scale * scale` rasterisation as the others. Fall back to
  `AddFontDefault()` if missing, exactly as the existing code does.
  The mock uses the browser's `ui-monospace` stack as a stand-in.

**Type roles (design units → `theme::fs()`):**

| Role | Font | Size | Tracking | Used for |
| --- | --- | --- | --- | --- |
| Body | DM Sans 400 | 13.5 | — | prose, settings values |
| Body bold | DM Sans 600 | 13.5 | — | feed names in settings, dialog titles |
| Group label | DM Sans 700 | 10.5 | +0.9 | DESTINATION, LAYOUT, SOURCES, VIEW, INSPECTOR |
| Control label | DM Sans 700 | 9.5 | +0.75 | button and segment text (1-UP, STOP SENDING) |
| Cell label | DM Sans 700 | 8.5 | +1.0 | footer metric labels (CPU, RSS, TX) |
| Readout | IBM Plex Mono 400 | 10.5 | — | every number, URL, alias, coordinate, clock |
| Tile name | DM Sans 700 | 10 | +0.8 | canvas tile and thumbnail name badges |

Nothing is below 8.5 design units (≈10px at 1.2, ≈12px at the 1.5 an operator would
use on a big display). Do not go smaller.

## Screens

### 1. Main wall (`2a` idle, `1a` live, `2b` nine-feed)

Purpose: the operator's only screen. Pick sources, arrange the canvas, send it to a
VMR, watch that it is healthy.

Vertical stack, top to bottom, inside the existing full-viewport `##shell` window
(`WindowPadding` 14, unchanged):

```
 3px  tally strip        full width, no padding — StatusError when sending,
                         white @ 0.07 when not
 22   header row         identity · state readout · registration · clock
 10   gap
 ~58  control deck       one panel, four labelled groups
 10   gap
 27   feed-loss alert    only when a connected feed has stalled
 10   gap
 fill body               rail (218 wide) + gap 10 + canvas pane
 10   gap
 32   footer             metric cells
```

Everything above is a design unit except the 3px strip.

#### 1.1 Tally strip

`dl->AddRectFilled` across the full viewport width at the very top, before the
shell's padding. `HexU32(StatusError)` while
`conf_status == CONNECTED && input_open`; otherwise `WhiteU32(0.07f)`. This is the
single strongest "we are live" signal — it must be drawn outside the panel padding
so it touches the window edge.

#### 1.2 Header row (height 22)

Left to right, all vertically centred, gap 12:

* 16×16 rounded 4 square filled `AccentPrimary` (the app mark — no glyph, no icon).
* `UAV WALL` — DM Sans 700 @ 10.5, tracking +1.2, `TextPrimary`.
* 1×14 divider, `white @ 0.12`.
* State cluster:
  * 6px dot — `StatusError` sending · `white @ 0.22` idle.
  * `ON AIR` / `NOT SENDING` — DM Sans 700 @ 9.5, tracking +1.2, `StatusError` /
    `white @ 0.45`.
  * Mono @ 10.5, `white @ 0.62`: `MAIN VIDEO → ops@example.com`, or
    `CONTENT → ops@example.com · floor taken` when `send_as_content`, or
    `canvas composited on demand` when idle.
* Spring.
* Registration: 6px `StatusOnline` dot + mono @ 10.5 `white @ 0.50`
  `REGISTERED uavwall@example.com`. Not registered but `reg_host` set:
  `white @ 0.32`, `NOT REGISTERED`. `reg_host` empty: omit entirely.
* 1×14 divider, then a mono @ 10.5 UTC clock, `white @ 0.70`.

#### 1.3 Control deck (auto height, ≈58)

One `BeginChild`: fill `PanelFill`, 1px `PanelStroke`, radius `RadiusDeck`, vertical
padding `DeckPadY`. Four groups laid left to right, each `GroupPadX` horizontal
padding, separated by a 1px `PanelStroke` vertical divider inset 2 top and bottom.
Group internals: label on top, controls below, gap 6.

Every control is height `ControlH` (25) and radius `RadiusControl2` (7).

**DESTINATION**
* VMR field, width 208 — `PanelFillRaised` fill, 1px `ControlStroke`, padding-x 9,
  mono @ 10.5 `TextPrimary`. Behaviour, popup and directory-search list are exactly
  as today (`refresh_vmr_hits`, `##vmrhits` window) — only the field's own metrics
  change.
* PIN field, width 62, same treatment, `ImGuiInputTextFlags_Password`.
* Primary action, padding-x 12, control-label type, white text:
  * idle → fill `AccentPrimary`, a 6×10 right-pointing filled triangle then
    `SEND TO VMR`;
  * connecting → fill `AccentPrimary`, `CONNECTING…`, disabled;
  * sending → fill `StatusError`, a 7×7 filled square then `STOP SENDING`.

**LAYOUT**
* Segmented control, one 48-wide cell per preset (`1-UP` `2×2` `3×3` `PIP`):
  track `white @ 0.06` + 1px `ControlStroke`, 1px `white @ 0.10` dividers between
  cells, no per-cell rounding — the group is rounded and clipped. Active cell is
  filled `AccentPrimary` with white text; inactive text `white @ 0.55`. Active =
  the preset whose tile geometry currently matches `app.tiles`; none active after a
  manual drag.
* Preset slots: same track, containing `SAVED` (DM Sans 700 @ 8.3, `white @ 0.30`)
  then three 18×17 radius-5 chips `A` `B` `C`. Filled `AccentPrimary` + white text
  when the slot holds the current layout; `white @ 0.09` + `white @ 0.60` when
  occupied; `white @ 0.09` + `white @ 0.28` when empty. **Click** recalls, **click
  and hold ~0.5s** (or right-click) stores the current tile list. Persist as
  `preset_a=`/`preset_b=`/`preset_c=` lines in `uavwall.conf`, each a
  semicolon-separated `feed_index,x,y,w,h` list.
* `CLEAR` — outline only, 1px `StatusError @ 0.35`, text `StatusError @ 0.90`.

**SOURCES**
* `CONNECT ALL` when any feed is disconnected: 1px `StatusOnline @ 0.35`, fill
  `StatusOnline @ 0.12`, text `StatusOnline @ 0.95`.
  `DISCONNECT ALL` when all are connected: outline `StatusError @ 0.30`, text
  `StatusError @ 0.85`, no fill (destructive, so quieter than the constructive one).
* Mono @ 10.5 `white @ 0.45`: `4/5 live`.

**VIEW** (right-aligned, after a spring)
* `STATS` — active: fill `AccentPrimary`, white text. Inactive: 1px `ControlStroke`,
  text `white @ 0.60`.
* `SETTINGS` — same two states, driven by `app.show_settings`.

#### 1.4 Feed-loss alert (height 27)

Shown when any feed has `connected == true` and
`ImGui::GetTime() - last_frame_at > 2.0`. Fill `StatusWarn @ 0.13`, 1px
`StatusWarn @ 0.40`, radius 7, padding-x 10, gap 8:

7px `StatusWarn` dot · `FEED LOSS` (control-label, `StatusWarn`) · mono @ 10.5
`white @ 0.70` `FEED 03 — no frames for 4.2s, still connected` (name and age from
the worst offender; `and 2 others` appended when more than one) · spring ·
`MUTE ALERTS` · `DISMISS`, both DM Sans 700 @ 9.2 `white @ 0.45`, as text buttons.

Pair it with the audible cue the user asked for: reuse `play_ring()`'s platform
paths at lower volume, once per stall transition, suppressed by Mute alerts (a
session-only flag, not persisted).

#### 1.5 Feed rail (width `RailW` = 218)

`BeginChild` with fill `PanelFill`, 1px `PanelStroke`, radius `RadiusDeck`, padding
10 (the current code inherits `WindowPadding` (0,0) here and draws flush to the
edge — set an explicit padding for this panel). Contents, gap `GapTight`:

* Header row: `SOURCES` (group label) · spring · mono @ 10.5 `white @ 0.30`
  `4 LIVE / 5`.
* One card per feed.
* Spring.
* Inspector card, pinned to the bottom.

**Card** — a horizontal pair: a 2.5-wide radius-2 **placement bar**, gap 7, then the
card body.

* Placement bar: `AccentPrimary` when the feed has a tile on the canvas,
  `StatusWarn` when it is stalled, `white @ 0.08` when not placed. This replaces
  today's 2px accent rectangle around the whole thumbnail — the frame is needed for
  hover.
* Thumbnail: 16:9, radius `RadiusThumb`, `AddImageRounded` of the feed texture, 1px
  border `TileStroke` (`StatusWarn @ 0.55` when stalled, `AccentPrimary` on hover).
  Name badge top-left in tile-name type, `white @ 0.85`, drawn straight on the
  picture with no plate. **Heights are the one thing that flexes with feed count** —
  see "Density" below.
  * Not connected: `white @ 0.04` fill, 1px dashed `white @ 0.16`, centred `OFFLINE`
    (DM Sans 700 @ 8.3, tracking +0.8, `white @ 0.35`), whole card at 62% opacity.
  * Connected, no frames yet: same plate, `waiting…`.
* Meta row beneath, gap 5: a 5×5 **square** state marker (`StatusOnline` live /
  `StatusWarn` stalled / `white @ 0.25` off — square, not the current circle, so it
  reads as an indicator rather than a bullet) · mono @ 9.2 `white @ 0.55`
  `1280×720 25fps 2.1Mb` · spring · **per-feed toggle**: 22×12 pill,
  `StatusOnline @ 0.85` with the 8px white knob right when connected,
  `white @ 0.12` with a `white @ 0.40` knob left when not.

Card gestures are unchanged from today (click adds/removes tiles, double-click punches
full screen, tooltip carries URL and error) with one addition: the toggle swallows the
click and calls `start_feed`/`stop_feed` for that feed alone. Selecting a card (single
click on the thumbnail) also sets `app.inspect_feed`.

**Inspector card** — fill `PanelFillRaised`, radius 8, padding 8, gap 5. With a
selection: `INSPECTOR` group label · spring · state word (`LIVE` `StatusOnline` /
`STALLED` `StatusWarn` / `OFFLINE` `white @ 0.35`, DM Sans 700 @ 8.3); the feed name
in DM Sans 700 @ 10.8; then mono @ 9.2 label/value rows, label `white @ 0.35` left,
value `white @ 0.72` right-aligned — `URL` (tail-elided), `TRANSPORT` (`TCP · 200ms`),
`UPTIME`, `LAST FRAME` (value in `StatusWarn` when > 2s). Then two half-width
22-high buttons: `RECONNECT` (fill `AccentPrimary`) and `REMOVE` (1px
`white @ 0.14`). Border is `StatusWarn @ 0.35` when the inspected feed is stalled,
none otherwise.

With no selection: 1px dashed `white @ 0.14`, `INSPECTOR` label and one line of
DM Sans @ 10 `white @ 0.30` — "Select a source to see its URL, transport and uptime."

**Density.** The rail must fit its cards without scrolling at both sizes, so thumbnail
height comes off the feed count rather than the chrome. Card height = thumb + 3 + meta
(~12).

| Feeds | Thumb height (design units) | Board |
| --- | --- | --- |
| ≤ 5 | 48 | `1a`, `2a` |
| 6–9 | 45 | `2b` |
| > 9 | keep 45 and scroll — clamp, never shrink further | — |

Compute it once per frame from the available rail height; do not hard-code per board.

#### 1.6 Canvas pane

`BeginChild` filling the remaining width: `PanelFill`, 1px `PanelStroke`, radius
`RadiusDeck`, padding 10.

* Header row: group label `PROGRAM · WHAT THE VMR RECEIVES` (`CANVAS · READY TO SEND`
  when idle) · spring · mono @ 10.5 `white @ 0.45`
  `1920×1080 · 30 fps · H.264 · MAIN`.
* The canvas itself, 16:9 fitted and centred as today, but **square-cornered** (the
  VMR receives a rectangle, so the mock stops pretending otherwise) and drawn on
  `WindowBgStart`.
  * Outline: 1px `StatusError @ 0.55` while sending, 1px `white @ 0.14` idle.
  * While sending, four 12×12 **corner ticks**, 2px, `StatusError @ 0.90`, drawn just
    outside the canvas rect — the tally repeated where the operator is actually
    looking.
* Tiles: `AddImage` of the feed texture, 1px `TileStroke` between neighbours (grid
  presets are edge-to-edge, so draw dividers, not per-tile borders). Overlay per tile:
  a 15×15 radius-4 `AccentPrimary` slot-number badge with white DM Sans 700 @ 9.2,
  then the feed name in tile-name type `white @ 0.92`; bottom corners carry mono @ 9.5
  `white @ 0.50` telemetry when the feed supplies it. Selected tile: 2px inset
  `AccentPrimary` and a `SELECTED` label. Stalled tile: badge turns `StatusWarn` with
  `WindowBgStart` text, a `LAST FRAME 4.2s` outlined chip sits beside the name, and the
  tile gets a `StatusWarn @ 0.05` wash.
  * Hover, drag-to-move, corner grip and double-click-to-punch behaviour are unchanged;
    keep the accent hover frame and grip triangle, at the new stroke weights.
* Empty canvas: centred DM Sans @ 11.5 `white @ 0.35` —
  "Click a source to place it · double-click for full screen".

#### 1.7 Footer (height `FooterH` = 32)

A single strip: `PanelFill`, 1px `PanelStroke`, radius `RadiusFooter`. Cells laid left
to right, each padding-x 13, separated by a 1px `white @ 0.07` divider inset 8 top and
bottom. Cell = cell-label + gap 7 + mono @ 10.5 value.

`CPU 78%` · `RSS 279 MB` · `COMPOSITE 16.6 ms` (with ` / 33 budget` appended in mono @
9.5 `white @ 0.30` when a send session is open) · `TX 3.4 Mbps` (value in
`AccentPrimary @ 0.95`) · `LOSS 0.1%` · `RTT 19 ms` · spring · a right-aligned mono @
10.5 `white @ 0.35` status line — today's `get_status()` string, plus call duration.

Idle: `COMPOSITE` reads `idle` and `TX`/`LOSS`/`RTT` read `—`, all `white @ 0.35`.
`STATS` off hides every cell and leaves only the right-hand status line.

### 2. Settings (`1d` Conference, `2c` Canvas & sending and Feeds)

Purpose: everything in `uavwall.conf`, without the one-long-scroll form.

A real `ImGui::Begin` window (as today — the repo's note about widgets needing a real
window still applies), fixed **683×500 design units**, `WindowBgMid`, 1px
`white @ 0.10`, radius `RadiusPanel` (14), no padding of its own; three bands:

* **Title bar, height 38** — `SETTINGS` (DM Sans 700 @ 10.5, tracking +1.2,
  `white @ 0.85`), spring, mono @ 10 `white @ 0.30` `uavwall.conf`. 1px
  `white @ 0.07` rule beneath.
* **Left nav, width 163**, padding 12/8, 1px `white @ 0.07` right rule. Five rows,
  height 27, radius 8, padding-x 10, DM Sans 500 @ 11.7 `white @ 0.60`; selected row
  is filled `AccentPrimary`, DM Sans 600, white. Rows: Conference · Registration ·
  Canvas & sending · Feeds (with the feed count right-aligned, mono @ 10
  `white @ 0.30`) · Interface. When sending, a small card is pinned to the bottom of
  the nav: `SENDING` cell label + mono @ 9.5 `StatusError`
  "live — some fields locked".
* **Panel, padding 15/17**, gap 13: a heading pair (group label + one line of DM Sans
  @ 11.5 `white @ 0.40` explaining the panel), then the controls, then a spring and a
  footer row of `SAVE` (fill `AccentPrimary`) and `CLOSE` (1px `white @ 0.14`), both
  height 27, padding-x 15, control-label type.

Field rows in the panels are **label left (width 125, DM Sans 500 @ 12
`white @ 0.70`), control right** — not ImGui's default trailing labels, which is why
today's form reads as a debug panel. Values that are machine data (VMR, URLs, PINs)
use mono; names and prose use DM Sans.

Locked-while-sending fields keep `BeginDisabled` and are joined by an explicit
callout: fill `StatusError @ 0.10`, 1px `StatusError @ 0.30`, radius 8, 6px dot, DM Sans
@ 11.5 `white @ 0.75` — "Locked while sending. Stop sending to change the destination."

**Canvas & sending** (`2c`, left): send resolution becomes three side-by-side cards
rather than a combo — each card is radius 8, DM Sans 600 @ 12 title and a mono @ 9.5
`white @ 0.50` cost line taken from the README's measurements (`78% CPU · 16.6 ms`,
`48% CPU · half the pixels`, `lightest`); the selected card is fill
`AccentPrimary @ 0.16` + 1.5px `AccentPrimary`, others 1px `ControlStroke`. Below,
the frame-rate slider (6px track `white @ 0.09`, `AccentPrimary` fill, 13px white
knob, mono min/mid/max ticks) and the same two-card treatment for **Send as**, each
card carrying the one-line consequence in DM Sans @ 10.4 `white @ 0.45`. Tile rescaling
on resolution change is unchanged.

**Feeds** (`2c`, right): transport as a two-cell segmented control, jitter buffer as a
slider with a mono value, `Connect every feed at startup` as a 28×15
`StatusOnline`/`white @ 0.12` toggle, then a table with cell-label column headers
(`NAME` 98 · `RTSP URL` fill · `STATE` 80). Each row: name field (DM Sans 600 @ 10.4),
URL field (mono @ 10.4; 1px `StatusError @ 0.40` when that feed errored), and a state
cell holding the per-feed toggle plus `LIVE` / `STALLED` / `ERROR` / `OFF` in DM Sans
700 @ 8.3. `+ ADD FEED` is a 93-wide dashed-outline button. Removing a feed keeps
today's tile-index fix-up.

**Conference** (`1d`): VMR, PIN, Display name as label/control rows, the locked
callout, and a read-only mono summary of the other config keys
(`canvas`, `send_fps`, `send_as`, `rtsp_transport`) so the operator can see the whole
file without leaving the panel.

**Registration** and **Interface** are not drawn. Build them from the same parts:
label/control rows, one `Register`/`Deregister` primary button, toggles for
`reg_auto`/`auto_accept`, `ui_scale` slider with the existing "applies on next start"
note. Do not invent new control types for them.

### 3. Incoming call (`1d`)

Real window, top-centre, as today. `WindowBgMid`, radius 12, 1px accent, and a **3px
coloured strip along its top edge** (`AccentPrimary` normally, `StatusError` when the
wall is already in a call) — the same tally language as the main window.

Normal: `INCOMING CALL` (group label, `AccentPrimary`), caller display name in DM Sans
600 @ 13.3, alias in mono @ 10.4 `white @ 0.42`; right-aligned `ACCEPT` (fill
`StatusOnline`, `WindowBgStart` text) and `DECLINE` (1px `StatusError @ 0.45`), both
height 28.

Already in a call: strip and label go `StatusError`, the buttons move below a
`white @ 0.05` radius-8 explanation block — "Accepting leaves **ops@example.com** — the
wall stops sending there first." — and become `DISCONNECT & ACCEPT` (fill
`StatusError`) and `REJECT` (1px `white @ 0.16`). Keep the existing async ordering:
start the disconnect on the UI thread, wait bounded for `DISCONNECTED`, then return
`true` from the incoming callback.

### 4. PIN (`1d`)

Keep `BeginPopupModal` (the dimmed backdrop and keyboard capture matter). 333 wide,
radius 12, 1px `white @ 0.12`. `PIN REQUIRED` group label `white @ 0.32`, the target
alias in DM Sans 600 @ 13.3, then a 32-high field: `PanelFillRaised`, 1px
`AccentPrimary @ 0.60`, mono @ 14 with +4 letter-spacing so the dots read as digits,
and a 1.5×15 `AccentPrimary` caret. Helper line DM Sans @ 10.8 `white @ 0.40`. `JOIN`
(fill `AccentPrimary`) and `CANCEL` (1px `white @ 0.14`), height 27. Enter still
submits.

### 5. Feed error (`1d`)

Replaces today's plain tooltip, and doubles as the inspector's error state. Radius 12,
1px `StatusError @ 0.40`: 6px dot + `FEED ERROR` (group label, `StatusError`) + spring
+ the feed name; then mono @ 10 rows — the URL (`white @ 0.70`, wrapped), the reason
(`StatusError @ 0.90`, e.g. "Could not connect — TCP refused"), and the hint
(`white @ 0.35`, "Some cameras only offer UDP. Try Settings → Feeds → Transport.").
Then `RETRY` (fill `AccentPrimary`) and `SWITCH TO UDP` (1px `white @ 0.14`) at height
23 — the second sets `rtsp_transport=udp` and reconnects that feed only.

## Interactions & behaviour

Everything already implemented keeps its behaviour: rail click to place / remove,
double-click to punch full screen and back, tile drag with clamping, corner grip
resize at fixed 16:9, grid and PiP presets, directory search popup, PIN parking,
incoming-call handling, bench mode. New behaviour:

| Trigger | Effect |
| --- | --- |
| Per-feed toggle (rail or Settings) | `start_feed` / `stop_feed` for that feed only; `stop_feed` also removes its tiles |
| Click a rail thumbnail | sets `inspect_feed` in addition to today's place/remove |
| Click a preset slot | replace `app.tiles` with the stored layout; missing feeds are skipped |
| Long-press / right-click a preset slot | store the current `app.tiles` into that slot and save the config |
| Connected feed with no frame for > 2s | alert bar appears, rail marker and placement bar go amber, tile gets its chip and wash, one audible cue |
| Frames resume | all amber state clears automatically; no dismissal needed |
| `MUTE ALERTS` | suppress the sound for the session; the visual state stays |
| `DISMISS` | hide the bar until a *new* feed stalls |
| Conference reaches CONNECTED with the canvas attached | tally strip, header ON AIR, canvas outline and corner ticks all turn `StatusError` together — one state, three places |

No animation is required beyond what ImGui does for free. If you want one thing: fade
the tally strip in over ~150ms on state change. Do not blink it.

## State

Additions to the existing structs in `main.cpp`:

```cpp
struct Feed {
  /* … existing … */
  double connected_at = 0.0;   /* for the inspector's UPTIME       */
};

struct Tile { /* unchanged */ };

struct Config {
  /* … existing … */
  std::string preset_a, preset_b, preset_c;  /* "feed,x,y,w,h;…"   */
  bool alerts_muted = false;                 /* not persisted      */
};

struct App {
  /* … existing … */
  int  inspect_feed   = -1;    /* rail selection for the inspector  */
  int  settings_tab   = 0;     /* 0..4, left-nav selection          */
  int  alert_feed     = -1;    /* worst current stall, -1 = none    */
  bool alert_dismissed = false;
  double alert_rang_at = 0.0;
};
```

Stall detection is derived, not stored: a feed is stalled when
`connected && last_frame_at > 0 && ImGui::GetTime() - last_frame_at > 2.0`. The
existing `f.fps` / `f.mbps` / `sample_resources()` / `tx_*` fields already supply every
number the footer and inspector show — nothing new needs measuring.

## Assets

* **DM Sans** (Regular / Medium / SemiBold / Bold) — already in
  `demos/uavwall/assets/fonts/`, SIL OFL, no change. The copies in this bundle came
  from there.
* **IBM Plex Mono Regular** — **to be added** to the same folder for the `mono` role,
  with its OFL licence file, matching how DM Sans is shipped. Any OFL monospace with a
  clear tabular zero works; the design needs consistent digit widths, not that specific
  face.
* No icons. The design deliberately has no icon set: the app mark is a filled rounded
  square, "play" and "stop" are a filled triangle and a filled square, the reticle in
  the mock's placeholders is one circle and two lines. Do not add an icon font or draw
  SVG-style glyphs with the draw list.
* Feed imagery in the mock is a striped placeholder with a reticle, standing in for the
  live RTSP textures and for what `scripts/uav-streams.sh` generates. Nothing to
  implement.

## Files

* `UAV Wall.dc.html` — the design reference. Sections `2a`, `2b`, `1a`, `1d`, `2c` are
  the spec; `current` is the before; `1b`/`1c` are rejected alternatives.
* `assets/fonts/DMSans-*.ttf` — the fonts the mock uses, copied from the repo.

Implementation targets, all in the existing repo:

* `demos/uavwall/src/main.cpp` — `ui_feed_rail`, `ui_canvas`, `ui_settings`,
  `ui_incoming`, `ui_pin`, `pill_button`, and the top-bar/footer code inline in
  `main()`. `pill_button` should be replaced by (or joined by) a `deck_button` helper
  taking a fill/outline mode, since almost nothing in the new design is a pill.
* `demos/uavwall/src/Theme.h` — token and font additions above. Note the file's own
  comment: it is shared with `pexclient`, so add the Instrument values rather than
  changing the existing ones.
* `demos/uavwall/README.md` — the Planned list loses "per-feed connect/disconnect"; the
  feature list gains presets, the inspector and feed-loss alerts.
* `uavwall.conf` — three new `preset_*` keys, read and written by `load_config` /
  `save_config` with the existing forgiving parser.
