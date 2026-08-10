# ---------------------------------------------------------------------------
# make-bundle.ps1 -- wrap the built uavwall.exe into a self-contained portable
# folder: the Windows twin of make-bundle.sh.
#
# The result runs on any Windows 11 x64 machine with nothing installed: the
# Pulse runtime, the fonts, four demo clips and the app itself all live in the
# folder. It opens onto a working wall; an operator swaps in their own footage
# with Settings > Feeds > + ADD FILE, which Pulse decodes directly -- no media
# server involved.
#
#     .\demos\uavwall\make-bundle.ps1            # build-win\UAV Wall
#     .\demos\uavwall\make-bundle.ps1 -Zip       # ...plus a zip beside it
#
# mediamtx is included, so the RTMP/SRT receiver works out of the box. ffmpeg
# is NOT: it is GPL, and shipping it turns handing over the folder into a GPL
# distribution. Nothing in the demo needs it, and a recipient who wants
# import, recording or audio monitoring installs it themselves (winget install
# Gyan.FFmpeg) -- which the app tells them, and then finds automatically.
# docs\uavwall-setup.pdf is the guide to give them.
#
# To include it anyway (accepting the obligations -- see docs/third-party.md):
#
#     .\demos\uavwall\make-bundle.ps1 -WithTools -SourceOffer "You <you@example.com>"
#
# Unlike macOS, ffplay IS copied with -WithTools: LISTEN plays audio through
# ffplay on Windows (there is no audiotoolbox output device to hand PCM to).
#
# -NoMediamtx drops the media server. -ClipSeconds sets how much of each demo
# clip to carry (default 20).
#
# What this does NOT do is sign anything: SmartScreen will interpose an
# "unrecognised app" screen on the recipient's machine, cleared with
# More info > Run anyway. Authenticode signing is the way past that.
#
# Layout note: the folder is flat -- DLLs and resources beside the exe, which
# is how Windows resolves libraries with no launcher script. The clips\
# directory doubles as the app's bundle marker (see in_app_bundle() in
# main.cpp), so it is created even when there is no footage to fill it.
# ---------------------------------------------------------------------------
param(
    [string]$BuildDir = "",
    [string]$OutDir   = "",
    [switch]$WithTools,
    [switch]$NoMediamtx,
    [switch]$Zip,
    [int]$ClipSeconds = 20,

    # The ffmpeg carried by -WithTools is GPL, and GPL requires that a
    # recipient be able to get the corresponding source. A written offer
    # naming a real contact is the usual way to satisfy that; it goes into
    # THIRD-PARTY-NOTICES.txt. Leave it empty and the build warns.
    #   -SourceOffer "Ada Lovelace <ada@example.com>, Example Ltd, 1 Somewhere St."
    [string]$SourceOffer = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $repo "build-win" }
if (-not $OutDir)   { $OutDir   = Join-Path $BuildDir "UAV Wall" }

$exe = Join-Path $BuildDir "demos\uavwall\uavwall.exe"
if (-not (Test-Path $exe)) { throw "uavwall.exe not found at $exe -- build it first (cmake --build $BuildDir --target uavwall)" }

$ffmpegCmd = Get-Command ffmpeg -ErrorAction SilentlyContinue
$ffplayCmd = Get-Command ffplay -ErrorAction SilentlyContinue
$mtxCmd    = Get-Command mediamtx -ErrorAction SilentlyContinue
if (-not $NoMediamtx -and -not $mtxCmd) { throw "mediamtx not on PATH (winget install bluenviron.mediamtx) -- or pass -NoMediamtx" }
if ($WithTools -and -not $ffmpegCmd)    { throw "-WithTools needs ffmpeg on PATH (winget install Gyan.FFmpeg)" }

Write-Host "Staging bundle at $OutDir"
if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
foreach ($d in @("", "assets", "clips", "tools", "share", "licenses")) {
    New-Item -ItemType Directory -Force (Join-Path $OutDir $d) | Out-Null
}

# --- app + assets + Pulse runtime ------------------------------------------
# Flat: DLLs beside the exe is the one layout Windows loads with no launcher
# and no PATH edits. The app exports PEX_BASE_PATH itself when it finds
# pexpulse.dll beside it. The .lib is link-time only and stays behind.
Copy-Item $exe (Join-Path $OutDir "uavwall.exe")
Copy-Item -Recurse (Join-Path $repo "demos\uavwall\assets\*") (Join-Path $OutDir "assets")
Copy-Item (Join-Path $repo "sdk\windows\native\*.dll") $OutDir
Copy-Item -Recurse (Join-Path $repo "sdk\windows\native\share\*") (Join-Path $OutDir "share")

# --- demo clips ------------------------------------------------------------
# Same recipe as make-bundle.sh: downscaled, clipped excerpts so the folder
# stays small, Constrained Baseline because the Windows Pulse decoder breaks
# up High profile (docs/porting.md). Absent footage is not an error: the app
# falls back to the rtsp://127.0.0.1:8554/uavN URLs, which is what a source
# build demonstrates against. The clips\ directory itself always ships -- it
# is the bundle marker.
$clipCount = 0
$footage = Join-Path $repo "UAV_footage\prepared"
if ((Test-Path $footage) -and $ffmpegCmd) {
    foreach ($n in 1..4) {
        $src = Join-Path $footage "feed$n.mp4"
        if (-not (Test-Path $src)) { continue }
        & $ffmpegCmd.Source -nostdin -hide_banner -loglevel error -i $src -t $ClipSeconds -an `
            -vf "scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=25" `
            -c:v libx264 -profile:v baseline -level 3.1 -preset veryfast -crf 28 -movflags +faststart `
            -y (Join-Path $OutDir "clips\feed$n.mp4")
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed preparing feed$n.mp4" }
        $clipCount++
    }
}
if ($clipCount -gt 0) {
    $mb = [math]::Round(((Get-ChildItem (Join-Path $OutDir "clips") | Measure-Object Length -Sum).Sum) / 1MB, 1)
    Write-Host "  bundled $clipCount demo clips ($mb MB)"
} else {
    Write-Host "  no demo clips (needs UAV_footage\prepared and ffmpeg) --"
    Write-Host "    the app will default to the rtsp://127.0.0.1:8554/uavN feeds instead"
}

# --- tools -----------------------------------------------------------------
# mediamtx ships by default: the app runs it itself for the RTMP/SRT receiver
# (Settings > Feeds), and it is MIT, so it adds attribution and no more.
# ffmpeg (and ffplay, which LISTEN needs on Windows) only with -WithTools;
# see the header for why not, and docs/third-party.md for what it costs.
if (-not $NoMediamtx) {
    Copy-Item $mtxCmd.Source (Join-Path $OutDir "tools\mediamtx.exe")
    Write-Host "  bundled mediamtx ($($mtxCmd.Source))"
}
if ($WithTools) {
    Copy-Item $ffmpegCmd.Source (Join-Path $OutDir "tools\ffmpeg.exe")
    Write-Host "  bundled ffmpeg ($($ffmpegCmd.Source))"
    if ($ffplayCmd) {
        Copy-Item $ffplayCmd.Source (Join-Path $OutDir "tools\ffplay.exe")
        Write-Host "  bundled ffplay ($($ffplayCmd.Source))"
    } else {
        Write-Warning "ffplay not found -- LISTEN will be disabled for the recipient"
    }
}

# --- Self-containment check ------------------------------------------------
# The point of the bundle is that it runs on a machine with nothing installed,
# and the one way to be sure is to walk the import tables: every DLL the exe
# and the bundled DLLs name must resolve inside the bundle or to Windows
# itself. The macOS script learned this the hard way (a Homebrew glfw path
# shipped once and the app died in dyld) -- same check, Windows dress.
# Needs dumpbin, which lives with MSVC; skipped with a warning when absent.
$dumpbin = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe",
                         "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" `
                         -ErrorAction SilentlyContinue | Select-Object -First 1
if ($dumpbin) {
    # Windows-supplied surface: KnownDLLs, the api-ms-* umbrella, and the
    # OS-shipped ucrtbase. vcruntime/msvcp are the VC++ redistributable --
    # near-universal but not guaranteed, so they warn rather than fail.
    $system = @("kernel32","user32","gdi32","shell32","advapi32","ole32","oleaut32","combase","ws2_32",
                "winmm","psapi","opengl32","dwmapi","shcore","imm32","version","setupapi","bcrypt",
                "crypt32","secur32","ntdll","msvcrt","ucrtbase","shlwapi","iphlpapi","userenv","wldap32",
                "normaliz","dnsapi","dbghelp","winhttp","wininet","comdlg32","d3d11","d3d9","dxgi",
                "dxva2","mf","mfplat","mfreadwrite","evr","avrt","ksuser","cfgmgr32","powrprof","uxtheme",
                "propsys","comctl32","wtsapi32","hid","dinput8","xinput1_4","winusb","bthprops","netapi32",
                "msimg32","d2d1","d3d12","dwrite","bcryptprimitives","mmdevapi","dxcore","dcomp",
                "windowscodecs","audioses")
    $bundled = @(Get-ChildItem $OutDir -Filter *.dll | ForEach-Object { $_.Name.ToLower() })
    $leaks = @(); $vcNote = $false
    $targets = @(Get-Item (Join-Path $OutDir "uavwall.exe")) + @(Get-ChildItem $OutDir -Filter *.dll)
    foreach ($t in $targets) {
        $deps = & $dumpbin.FullName /nologo /dependents $t.FullName |
                Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim().ToLower() }
        foreach ($d in $deps) {
            $base = $d -replace '\.dll$',''
            if ($bundled -contains $d) { continue }
            if ($d -like "api-ms-*" -or $d -like "ext-ms-*") { continue }
            if ($system -contains $base) { continue }
            if ($base -match '^(vcruntime|msvcp|concrt)\d*') { $vcNote = $true; continue }
            $leaks += "$($t.Name) -> $d"
        }
    }
    if ($leaks.Count -gt 0) {
        Write-Host ""
        Write-Error ("the bundle is NOT self-contained. These will not exist on another`n" +
                     "machine, and the app will fail to start:`n  " + ($leaks -join "`n  "))
    }
    Write-Host "  self-contained: every import resolves inside the bundle or to Windows"
    if ($vcNote) {
        Write-Host "    (relies on the VC++ runtime -- present on virtually every Windows 11"
        Write-Host "     machine; a truly bare one needs Microsoft's vc_redist.x64.exe once)"
    }
} else {
    Write-Warning "dumpbin not found -- skipping the self-containment check"
}

# --- Licence notices --------------------------------------------------------
# Assembled here rather than kept as a static file, because what the bundle
# owes depends on what went into it -- see make-bundle.sh, which this mirrors.
$notices = Join-Path $OutDir "THIRD-PARTY-NOTICES.txt"
$licdir  = Join-Path $OutDir "licenses"

$head = @"
UAV Wall -- third-party notices

This application includes the components below. Licence texts are in
the licenses\ folder beside this file.

-- Pexip Pulse SDK ------------------------------------------------
pexpulse.dll is proprietary, licensed under the Pexip Software
Development Kit License Agreement -- see licenses\Pexip-SDK-LICENSE.txt,
which also carries Pexip's own open-source notices for the components
inside the runtime.

SCOPE: redistribution of the SDK inside this bundle was confirmed with
Pexip on 10 August 2026 for INTERNAL PEXIP USE ONLY, to demonstrate the
functionality. This app is not cleared for customers, partners, or
anyone outside Pexip. Do not forward it externally without going back
to Pexip for a wider clearance.

pexlgpl.dll carries the LGPL dependencies (GStreamer, GLib,
libav/ffmpeg, OpenSSL, Opus) that Pexip deliberately separated out. It
is shipped as its own DLL, dynamically linked and therefore replaceable
by the user, which is how LGPL s4 is satisfied. Do not merge or
statically absorb it. libmmd.dll, svml_dispmd.dll (Intel compiler
runtime) and tbb12.dll (oneTBB, Apache-2.0) are runtime support
libraries the SDK requires.

-- Dear ImGui (MIT), ImGui-Addons (MIT), GLFW (zlib/libpng) --------
Compiled into the application binary. Permissive; attribution only.

-- DM Sans, IBM Plex Mono (SIL Open Font License 1.1) --------------
See licenses\LICENSE-DMSans.txt and licenses\LICENSE-IBMPlexMono.txt.
The OFL requires its text to travel with the fonts.

"@
Set-Content -Path $notices -Value $head -Encoding UTF8

Copy-Item (Join-Path $repo "demos\uavwall\assets\fonts\LICENSE-DMSans.txt") $licdir -ErrorAction SilentlyContinue
Copy-Item (Join-Path $repo "demos\uavwall\assets\fonts\LICENSE-IBMPlexMono.txt") $licdir -ErrorAction SilentlyContinue

# The SDK licence, and Pexip's open-source notices with it, travel inside the
# NuGet the runtime came from. Extract rather than duplicating in the tree.
$nupkg = Get-ChildItem (Join-Path $repo "sdk\windows") -Filter *.nupkg -ErrorAction SilentlyContinue | Select-Object -First 1
$gotLic = $false
if ($nupkg) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zipFile = [IO.Compression.ZipFile]::OpenRead($nupkg.FullName)
    try {
        $entry = $zipFile.Entries | Where-Object { $_.FullName -eq "LICENSE.txt" } | Select-Object -First 1
        if ($entry) {
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $licdir "Pexip-SDK-LICENSE.txt"), $true)
            $gotLic = $true
        }
    } finally { $zipFile.Dispose() }
}
if (-not $gotLic) {
    Write-Host "  WARNING: could not extract the Pexip SDK licence from sdk\windows\*.nupkg"
    Write-Host "           -- the bundle is missing licenses\Pexip-SDK-LICENSE.txt"
}

if ($clipCount -gt 0) {
    Add-Content -Path $notices -Encoding UTF8 -Value @"
-- Demo clips ------------------------------------------------------
clips\feed1-4.mp4 are downscaled excerpts of stock footage from Pexels
(https://www.pexels.com), used under the Pexels License: free for
commercial and non-commercial use, no attribution required, but the
clips may not be sold unaltered and may not be used to imply
endorsement by people shown in them. Replace them if either
restriction is awkward for your use.

"@
}

if (Test-Path (Join-Path $OutDir "tools\ffmpeg.exe")) {
    # Read the licence off the binary that was copied: --enable-gpl and
    # --enable-version3 decide GPL-2 vs GPL-3, and guessing from the vendor
    # gets that wrong.
    $ffBanner = (& (Join-Path $OutDir "tools\ffmpeg.exe") -version 2>$null | Select-Object -First 1)
    $ffCfg = (& (Join-Path $OutDir "tools\ffmpeg.exe") -version 2>$null) -join " "
    $ffLic = "non-GPL -- check the build"
    if ($ffCfg -match "--enable-gpl") { $ffLic = "GPL-2.0-or-later" }
    if ($ffCfg -match "--enable-version3") { $ffLic = "GPL-3.0-or-later" }
    (& (Join-Path $OutDir "tools\ffmpeg.exe") -version 2>$null) | Set-Content (Join-Path $licdir "ffmpeg-BUILD.txt") -Encoding UTF8
    # Gyan's winget package keeps the licence text beside its bin\ directory.
    $ffLicFile = Get-ChildItem (Split-Path $ffmpegCmd.Source -Parent), (Join-Path (Split-Path $ffmpegCmd.Source -Parent) "..") `
                 -Filter "LICENSE*" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ffLicFile) { Copy-Item $ffLicFile.FullName (Join-Path $licdir "ffmpeg-LICENSE.txt") }
    else { Write-Warning "no LICENSE file found beside ffmpeg -- licenses\ffmpeg-LICENSE.txt is missing" }

    $offer = if ($SourceOffer) { @"
    WRITTEN OFFER: for three years from receipt of this software,
    the distributor named below will supply, on request, the
    complete corresponding source for the bundled ffmpeg, for no
    more than the cost of distribution:

      $SourceOffer
"@ } else { @"
    A written offer valid for three years is the usual way to
    satisfy this. NO OFFER HAS BEEN SET FOR THIS BUILD -- rebuild
    with -SourceOffer set before distributing this app.
"@ }
    Add-Content -Path $notices -Encoding UTF8 -Value @"
-- ffmpeg ($ffLic) --------------------------------------
$ffBanner

Bundled at tools\ffmpeg.exe (with ffplay.exe, which the LISTEN
feature uses on Windows) and run as a separate process -- it is not
linked into this application. Invoking a copyleft program as a
subprocess does not make the calling program a derived work, but
SHIPPING the binary is a distribution, and the obligations attach:

  * the licence text must travel with it -- see
    licenses\ffmpeg-LICENSE.txt;
  * corresponding source must be provided or offered. FFmpeg's
    source is at https://ffmpeg.org/download.html and the exact
    build's configuration is recorded in licenses\ffmpeg-BUILD.txt.

$offer

An LGPL-only ffmpeg build would avoid the GPL obligation, at the
cost of the encoders those builds omit (libx264 among them, which
this app uses to prepare imported clips).

"@
}

if (Test-Path (Join-Path $OutDir "tools\mediamtx.exe")) {
    Add-Content -Path $notices -Encoding UTF8 -Value @"
-- mediamtx (MIT) --------------------------------------------------
Bundled at tools\mediamtx.exe and run as a separate process.
Attribution only; see https://github.com/bluenviron/mediamtx.

"@
}

$components = (Select-String -Path $notices -Pattern '^-- ').Count
Write-Host "  wrote THIRD-PARTY-NOTICES.txt ($components components)"

$size = [math]::Round(((Get-ChildItem -Recurse $OutDir | Measure-Object -Property Length -Sum).Sum) / 1MB)
Write-Host ""
Write-Host "Built: $OutDir ($size MB)"
Write-Host ""
Write-Host "  Config:     <bundle>\uavwall.conf (the folder is the install)"
Write-Host "  Recordings: $env:USERPROFILE\Videos\UAV Wall"
Write-Host ""
if ((Test-Path (Join-Path $OutDir "tools\ffmpeg.exe")) -and -not $SourceOffer) {
    Write-Host "  WARNING: this bundle carries GPL ffmpeg with NO source offer."
    Write-Host "  Re-run with -SourceOffer `"Your Name <you@example.com>, Company, Address`""
    Write-Host "  before giving it to anyone. See docs/third-party.md."
    Write-Host ""
}
Write-Host "  Unsigned, so SmartScreen will warn on the recipient's machine:"
Write-Host "  More info > Run anyway. Authenticode signing is the way past that."

if ($Zip) {
    $zipPath = "$OutDir.zip"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path "$OutDir\*" -DestinationPath $zipPath
    Write-Host ""
    Write-Host "Zipped: $zipPath ($([math]::Round((Get-Item $zipPath).Length / 1MB)) MB)"
}
