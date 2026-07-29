cmake_minimum_required(VERSION 3.16)
project(pulse-showcase
    VERSION 0.2.0
    DESCRIPTION "A showcase of Pexip Pulse demo applications"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Always export compile_commands.json (handy for editor/IDE integration).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# This is an orchestrator, not a build in its own right.
#
# Every demo lives in its own self-contained directory under demos/ with its
# own CMakeLists.txt and README.md. This top-level file just:
#
#   1. finds the shared dependencies the demos need (Pulse, GLFW/OpenGL,
#      Dear ImGui), and
#   2. add_subdirectory()s each demo, gated behind a BUILD_<DEMO> option.
#
# The shared boilerplate (locating Pulse, RPATHs, run-*.sh launchers) lives in
# cmake/PulseDemo.cmake — see docs/adding-a-demo.md for how to add your own.
# ---------------------------------------------------------------------------

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PulseDemo)

# --- Shared dependencies ---------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Threads REQUIRED)

pulse_find_runtime()    # defines the pexip::pulse imported target
pulse_declare_imgui()   # fetches Dear ImGui + builds the shared `imgui` lib

# ---------------------------------------------------------------------------
# Demos. Lightweight demos build by default; the heavier ones that need extra
# system packages are opt-in.
# ---------------------------------------------------------------------------
option(BUILD_DOPPLER     "Build the doppler video-call demo"                ON)
option(BUILD_GATEWAY     "Build the pulse_gateway two-conference bridge"    ON)
option(BUILD_PEXNINJA    "Build the larger pexninja reference client"       OFF)
option(BUILD_DOPPLER_SIP "Build the SIP-signalling demo (needs PJSIP)"      OFF)
option(BUILD_VIDEOWALL   "Build the videowall multi-Pulse compositor"       ON)
option(BUILD_PEXCLIENT   "Build the pexclient home-screen client"           ON)
option(BUILD_UAVWALL     "Build the uavwall UAV-feeds-to-VMR demo"          ON)

if(BUILD_DOPPLER)
    add_subdirectory(demos/doppler)
endif()

if(BUILD_GATEWAY)
    add_subdirectory(demos/gateway)
endif()

if(BUILD_PEXNINJA)
    add_subdirectory(demos/pexninja)
endif()

if(BUILD_DOPPLER_SIP)
    add_subdirectory(demos/sip)
endif()

if(BUILD_VIDEOWALL)
    add_subdirectory(demos/videowall)
endif()

if(BUILD_PEXCLIENT)
    add_subdirectory(demos/pexclient)
endif()

if(BUILD_UAVWALL)
    add_subdirectory(demos/uavwall)
endif()
