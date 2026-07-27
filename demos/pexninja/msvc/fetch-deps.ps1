<#
.SYNOPSIS
    Fetch the third-party dependencies pexninja needs into third_party/.

.DESCRIPTION
    The CMake build of pexninja pulls these in with FetchContent. The MSVC
    project compiles the same sources, so this script stages them locally at
    the *same pinned versions* the CMakeLists.txt uses:

        imgui        v1.91.5-docking   (Dear ImGui, docking branch)
        implot       v0.16             (media-stats plots)
        gl3w         d5ba934…          (explicit GL loader; generated below)
        ImGui-Addons 0b25588…          (file browser)
        glfw         3.4 (prebuilt)    (window + GL context, win-x64 .lib)

    Pulse itself does NOT come from here — it is restored from the in-repo
    Pexip.Pulse NuGet package (see packages.config / nuget.config).

    Re-running is cheap: each dependency is skipped if already present.

.NOTES
    Requirements: git, python (for the gl3w generator) and internet access.

    You normally do NOT need to run this by hand: the pexninja.vcxproj invokes
    it automatically on the first build (the FetchThirdPartyDeps target). It is
    kept runnable on its own for troubleshooting or to pre-stage the deps:

        powershell -ExecutionPolicy Bypass -File .\fetch-deps.ps1
#>

[CmdletBinding()]
param(
    [switch]$Force  # re-fetch even if a dependency directory already exists
)

$ErrorActionPreference = 'Stop'
$root = Join-Path $PSScriptRoot 'third_party'
New-Item -ItemType Directory -Force -Path $root | Out-Null

function Get-GitDep {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [string]$Url,
        [Parameter(Mandatory)] [string]$Ref
    )
    $dest = Join-Path $root $Name
    if ((Test-Path $dest) -and -not $Force) {
        Write-Host "[skip] $Name already present ($dest)"
        return $dest
    }
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Write-Host "[git ] $Name <- $Url @ $Ref"
    # Init + fetch a single ref so this works for both tags and bare commit
    # SHAs (gl3w / ImGui-Addons have no release tags).
    git init -q $dest
    git -C $dest remote add origin $Url
    git -C $dest fetch -q --depth 1 origin $Ref
    git -C $dest checkout -q FETCH_HEAD
    return $dest
}

# ---- ImGui (docking) -------------------------------------------------------
Get-GitDep -Name 'imgui'        -Url 'https://github.com/ocornut/imgui.git'              -Ref 'v1.91.5-docking' | Out-Null

# ---- ImPlot ----------------------------------------------------------------
Get-GitDep -Name 'implot'       -Url 'https://github.com/epezent/implot.git'             -Ref 'v0.16'           | Out-Null

# ---- ImGui-Addons (file browser) ------------------------------------------
Get-GitDep -Name 'imgui-addons' -Url 'https://github.com/gallickgunner/ImGui-Addons.git' -Ref '0b25588ba842b93537f82ae84f27a75f604b04ce' | Out-Null

# ---- gl3w (needs a one-off Python generator step) -------------------------
$gl3w = Get-GitDep -Name 'gl3w' -Url 'https://github.com/skaslev/gl3w.git'               -Ref 'd5ba9340cdeb9154323817f5c87e5a5c377fdef7'
if ($Force -or -not (Test-Path (Join-Path $gl3w 'src/gl3w.c'))) {
    Write-Host '[gen ] gl3w (downloads glcorearb.h / khrplatform.h from khronos.org)'
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) { $python = Get-Command python3 -ErrorAction SilentlyContinue }
    if (-not $python) { throw 'python is required to generate gl3w (gl3w_gen.py)' }
    # Reject the Microsoft Store python.exe "app execution alias" stub: it lives
    # under …\Microsoft\WindowsApps and, when Python is not actually installed,
    # just launches the Store instead of running the script — which makes gl3w
    # generation fail with a confusing message. Point the user at a real Python.
    if ($python.Source -like '*\Microsoft\WindowsApps\*') {
        throw "Found the Microsoft Store python.exe stub at '$($python.Source)', which cannot run gl3w_gen.py. Install a real Python from https://www.python.org/downloads/ (or disable the 'python'/'python3' App execution aliases in Settings > Apps > Advanced app settings > App execution aliases) and re-run."
    }
    Push-Location $gl3w
    try { & $python.Source 'gl3w_gen.py' }
    finally { Pop-Location }
}

# ---- GLFW (prebuilt win-x64 binaries — no CMake/source build needed) ------
$glfwDir = Join-Path $root 'glfw'
if ($Force -or -not (Test-Path (Join-Path $glfwDir 'include/GLFW/glfw3.h'))) {
    $ver = '3.4'
    $zipUrl = "https://github.com/glfw/glfw/releases/download/$ver/glfw-$ver.bin.WIN64.zip"
    $zip = Join-Path $env:TEMP "glfw-$ver.bin.WIN64.zip"
    Write-Host "[http] glfw <- $zipUrl"
    Invoke-WebRequest -Uri $zipUrl -OutFile $zip
    $tmp = Join-Path $env:TEMP "glfw-$ver-extract"
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    Expand-Archive -Path $zip -DestinationPath $tmp -Force
    $extracted = Join-Path $tmp "glfw-$ver.bin.WIN64"
    if (Test-Path $glfwDir) { Remove-Item -Recurse -Force $glfwDir }
    Move-Item $extracted $glfwDir
    Write-Host "[glfw] headers + lib-vc2022 staged under $glfwDir"
}

Write-Host ''
Write-Host 'All third-party dependencies are ready.' -ForegroundColor Green
Write-Host 'The pexninja.vcxproj restores the Pexip.Pulse NuGet and builds on its'
Write-Host 'own — just open pexninja.sln in Visual Studio 2022 and press Build/Run,'
Write-Host 'or from the command line:'
Write-Host '    msbuild -restore pexninja.sln /p:Configuration=Release /p:Platform=x64'
