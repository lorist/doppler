# ---------------------------------------------------------------------------
# pexclient — a Pexip Infinity home-screen client (register / search / dial)
# styled per design_handoff_pexninja_refresh (variant 01, "Dark Frosted").
#
# Builds from the parent showcase project (BUILD_PEXCLIENT, ON by default).
# The parent provides the pexip::pulse target, the shared `imgui` static lib
# and the helpers from cmake/PulseDemo.cmake.
# ---------------------------------------------------------------------------
add_executable(pexclient
    src/main.cpp)

# Where the DM Sans TTFs and the Pulse background image live. Absolute so the
# binary works regardless of the directory it is launched from.
target_compile_definitions(pexclient PRIVATE
    "PEXCLIENT_ASSET_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\""
    "PEXCLIENT_DEFAULT_CWD=\"${CMAKE_SOURCE_DIR}\"")

target_link_libraries(pexclient PRIVATE
    imgui
    pexip::pulse
    Threads::Threads)

# The share picker enumerates displays/windows via CoreGraphics on macOS.
if(APPLE)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics REQUIRED)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
    # AudioToolbox: incoming-call ring tone (system sound).
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox REQUIRED)
    target_link_libraries(pexclient PRIVATE
        ${COREGRAPHICS_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY})
endif()

pulse_demo_rpath(pexclient)
pulse_demo_launcher(pexclient run-pexclient.sh)
