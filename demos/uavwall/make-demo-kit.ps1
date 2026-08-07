# ---------------------------------------------------------------------------
# make-demo-kit.ps1 -- stage uavwall + its whole demo rig into one portable
# folder that runs on any Windows 11 x64 box with a double-click.
#
#   .\demos\uavwall\make-demo-kit.ps1              # build-win\uavwall-demo-kit
#   .\demos\uavwall\make-demo-kit.ps1 -Zip         # ...plus a zip beside it
#
# The kit carries: uavwall.exe + assets, the Pulse runtime (DLLs + models),
# ffmpeg/ffplay, mediamtx, looping demo clips (prepared from -MediaDir at kit
# build time: 720p25 Constrained Baseline with a per-feed tone), a clean
# uavwall.conf, and start-demo.bat. The launcher starts mediamtx and the
# publishers, runs the wall, and tears everything down when the window closes.
#
# Prerequisites on the *build* machine only: a built uavwall.exe, and ffmpeg +
# mediamtx on PATH (winget: Gyan.FFmpeg, bluenviron.mediamtx). The *target*
# machine needs nothing.
#
# Clips are transcoded to Constrained Baseline deliberately: the Windows Pulse
# build breaks up High-profile H.264 (docs/porting.md) -- and stream-copy
# publishing keeps the running kit at ~0% CPU per feed.
# ---------------------------------------------------------------------------
param(
    [string]$BuildDir = "",
    [string]$OutDir   = "",
    [string]$MediaDir = "",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $repo "build-win" }
if (-not $OutDir)   { $OutDir   = Join-Path $BuildDir "uavwall-demo-kit" }
if (-not $MediaDir) { $MediaDir = Join-Path $repo "sample_videos" }

$exe = Join-Path $BuildDir "demos\uavwall\uavwall.exe"
if (-not (Test-Path $exe)) { throw "uavwall.exe not found at $exe -- build it first (cmake --build $BuildDir --target uavwall)" }
$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
$ffplay = Get-Command ffplay -ErrorAction SilentlyContinue
$mtx    = Get-Command mediamtx -ErrorAction SilentlyContinue
if (-not $ffmpeg) { throw "ffmpeg not on PATH (winget install Gyan.FFmpeg)" }
if (-not $mtx)    { throw "mediamtx not on PATH (winget install bluenviron.mediamtx)" }

Write-Host "Staging kit at $OutDir"
if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
foreach ($d in @("", "assets", "pulse", "tools", "mediamtx", "media", "recordings")) {
    New-Item -ItemType Directory -Force (Join-Path $OutDir $d) | Out-Null
}

# --- app + assets ----------------------------------------------------------
Copy-Item $exe (Join-Path $OutDir "uavwall.exe")
Copy-Item -Recurse (Join-Path $repo "demos\uavwall\assets\*") (Join-Path $OutDir "assets")

# --- Pulse runtime: DLLs + the share\ tree (VAD/denoiser models) -----------
Copy-Item -Recurse (Join-Path $repo "sdk\windows\native\*") (Join-Path $OutDir "pulse")

# --- tools -----------------------------------------------------------------
# ffmpeg full builds are self-contained single exes. ffplay is what LISTEN
# uses on Windows; skip it with a warning rather than failing the kit.
Copy-Item $ffmpeg.Source (Join-Path $OutDir "tools\ffmpeg.exe")
if ($ffplay) { Copy-Item $ffplay.Source (Join-Path $OutDir "tools\ffplay.exe") }
else { Write-Warning "ffplay not found -- LISTEN will be disabled in the kit" }
Copy-Item $mtx.Source (Join-Path $OutDir "mediamtx\mediamtx.exe")

# --- mediamtx config -------------------------------------------------------
# Anonymous publish/read (a loopback demo rig), any path accepted, plus the
# payload-type-fixing relay for RTMP devices: mediamtx's RTMP->RTSP conversion
# gives both tracks PT 96 and Pulse demuxes by PT, so phones are pulled via
# the 'wearable' path, which round-trips them through ffmpeg for distinct PTs
# (and Baseline profile, which the Windows Pulse decoder requires).
@'
logLevel: info
rtspAddress: 0.0.0.0:8554
rtmpAddress: 0.0.0.0:1935

authMethod: internal
authInternalUsers:
  - user: any
    permissions:
      - action: publish
      - action: read
      - action: playback

paths:
  wearable:
    runOnDemand: ffmpeg -hide_banner -loglevel warning -rtsp_transport tcp -i rtsp://127.0.0.1:8554/live/wearable -c:v libx264 -preset veryfast -tune zerolatency -profile:v baseline -pix_fmt yuv420p -g 30 -b:v 4M -c:a copy -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/wearable
    runOnDemandRestart: yes
  all_others:
'@ | Out-File -Encoding ascii (Join-Path $OutDir "mediamtx\mediamtx.yml")

# --- demo clips ------------------------------------------------------------
# Prepared once here so the kit publishes with -c copy at ~0% CPU. Tones are
# per-feed so LISTEN / SEND AUDIO audibly switch sources.
$clips = @()
if (Test-Path $MediaDir) {
    $clips = Get-ChildItem $MediaDir -Filter *.mp4 | Sort-Object Name | Select-Object -First 4
}
if ($clips.Count -eq 0) {
    Write-Warning "no clips in $MediaDir -- kit will have no synthetic feeds (phone/RTMP still works)"
}
$freqs = @(220, 330, 440, 550)
for ($i = 0; $i -lt $clips.Count; $i++) {
    $n = $i + 1
    Write-Host "  preparing media\uav$n.mp4 <- $($clips[$i].Name) (tone $($freqs[$i]) Hz)"
    & $ffmpeg.Source -hide_banner -loglevel error -y -i $clips[$i].FullName `
        -f lavfi -i "sine=frequency=$($freqs[$i]):sample_rate=48000" -shortest `
        -vf "scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2,fps=25" `
        -c:v libx264 -preset veryfast -profile:v baseline -pix_fmt yuv420p -g 25 -sc_threshold 0 `
        -b:v 2M -maxrate 2M -bufsize 2M -c:a aac -b:a 64k -af "volume=0.15" `
        (Join-Path $OutDir "media\uav$n.mp4")
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed preparing $($clips[$i].Name)" }
}

# --- uavwall.conf ----------------------------------------------------------
# Deliberately no registration credentials: a kit gets copied around, and
# reg_pass is stored in clear. Fill in Settings on the target machine.
@'
# uavwall demo kit configuration. Edit here or via Settings in the app.
vmr=
pin=
display_name=UAV Wall

canvas=1920x1080
send_fps=30
send_as=main

reg_host=
reg_alias=
reg_user=
reg_pass=
reg_auto=false
auto_accept=false

rtsp_transport=tcp
rtsp_latency_ms=200
autoconnect=true

record_dir=recordings
audio_delay_ms=0

ui_scale=1.2
show_stats=true

preset_a=
preset_b=
preset_c=

feed=HAWKEYE 21|rtsp://127.0.0.1:8554/uav1
feed=KESTREL 33|rtsp://127.0.0.1:8554/uav2
feed=NOMAD 14|rtsp://127.0.0.1:8554/uav3
feed=OSPREY 12|rtsp://127.0.0.1:8554/uav4
feed=WEARABLE 01|rtsp://127.0.0.1:8554/wearable
'@ | Out-File -Encoding ascii (Join-Path $OutDir "uavwall.conf")

# --- launcher --------------------------------------------------------------
@'
# Started by start-demo.bat. Owns the rig lifecycle: mediamtx + one looping
# publisher per media clip, then the wall in the foreground; everything this
# script started is stopped when the wall exits.
$ErrorActionPreference = "SilentlyContinue"
$kit = $PSScriptRoot
Set-Location $kit

$env:PATH = "$kit\pulse;$kit\tools;" + $env:PATH
$env:PEX_BASE_PATH = "$kit\pulse"

$children = @()
# Keep mediamtx's output: its log is the ground truth when a device will not
# connect -- "is publishing to path 'x'" versus nothing arriving at all.
$mtx = Start-Process -FilePath "$kit\mediamtx\mediamtx.exe" -ArgumentList "`"$kit\mediamtx\mediamtx.yml`"" `
    -WorkingDirectory "$kit\mediamtx" -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput "$kit\mediamtx\mediamtx.log" -RedirectStandardError "$kit\mediamtx\mediamtx.err.log"
$children += $mtx
Start-Sleep -Seconds 2

foreach ($clip in (Get-ChildItem "$kit\media" -Filter uav*.mp4 | Sort-Object Name)) {
    $path = $clip.BaseName
    $p = Start-Process -FilePath "$kit\tools\ffmpeg.exe" `
        -ArgumentList "-hide_banner -loglevel error -stream_loop -1 -re -i `"$($clip.FullName)`" -c copy -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/$path" `
        -WindowStyle Hidden -PassThru
    $children += $p
}

& "$kit\uavwall.exe" $args

# The wall has exited: stop the publishers, the on-demand relay, and mediamtx.
foreach ($c in $children) { if ($c -and -not $c.HasExited) { Stop-Process -Id $c.Id -Force } }
Get-CimInstance Win32_Process -Filter "Name='ffmpeg.exe'" |
    Where-Object { $_.CommandLine -match [regex]::Escape($kit) -or $_.CommandLine -match "wearable" } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
'@ | Out-File -Encoding ascii (Join-Path $OutDir "_launcher.ps1")

@'
@echo off
rem uavwall demo kit -- starts mediamtx + the demo feeds, then the wall.
rem Everything is stopped again when the wall window is closed.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_launcher.ps1" %*
'@ | Out-File -Encoding ascii (Join-Path $OutDir "start-demo.bat")

# --- README ----------------------------------------------------------------
@'
uavwall demo kit
================

Double-click start-demo.bat. That starts a local media server (mediamtx) and
four looping demo feeds, then the wall itself with everything auto-connecting.
Closing the wall window stops the whole rig.

Needs: Windows 11 x64. Nothing to install.

To dial a conference or register: Settings inside the app (credentials are
not shipped in the kit). Recordings land in recordings\.

A phone or bodycam can push in live. The relay listens on path live/wearable,
so point the device there -- the two common app layouts both reach it:
  Larix (one URL field):        rtmp://<this-machine>:1935/live/wearable
  PRISM / OBS (URL + key):      URL rtmp://<this-machine>:1935/live
                                key wearable
The wall pulls it back as the WEARABLE 01 feed (rtsp://127.0.0.1:8554/wearable),
which relays through ffmpeg to fix two things at once: mediamtx's RTMP
conversion gives both tracks the same RTP payload type, and the Windows Pulse
decoder cannot handle High-profile H.264 (most phones send High profile).
If the tile stays offline, mediamtx\mediamtx.log names the path the device
actually landed on ("is publishing to path '...'") -- it must be live/wearable.

If feeds show offline: another process may already own port 8554 or 1935 on
this machine (a previous rig, another mediamtx). Stop it and relaunch.
'@ | Out-File -Encoding ascii (Join-Path $OutDir "README.txt")

$size = [math]::Round(((Get-ChildItem -Recurse $OutDir | Measure-Object -Property Length -Sum).Sum) / 1MB)
Write-Host "Kit staged: $OutDir ($size MB)"

if ($Zip) {
    $zipPath = "$OutDir.zip"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path "$OutDir\*" -DestinationPath $zipPath
    Write-Host "Zipped: $zipPath ($([math]::Round((Get-Item $zipPath).Length / 1MB)) MB)"
}
