# ---------------------------------------------------------------------------
# uavwall — UAV RTSP downlinks composed onto a canvas and sent into a Pexip VMR.
#
# Builds from the parent showcase project (BUILD_UAVWALL, ON by default). Uses
# the shared `imgui` target and pexip::pulse from cmake/PulseDemo.cmake, and
# pexclient's Dark Frosted theme (copied into src/Theme.h).
# ---------------------------------------------------------------------------
add_executable(uavwall
    src/main.cpp)

# Fonts live here; baked in as an absolute path so the binary works from any
# working directory.
target_compile_definitions(uavwall PRIVATE
    "UAVWALL_ASSET_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\"")

target_link_libraries(uavwall PRIVATE
    imgui
    pexip::pulse
    Threads::Threads)

# AudioToolbox: the incoming-call ring tone (a macOS system sound).
if(APPLE)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox REQUIRED)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(uavwall PRIVATE ${AUDIOTOOLBOX_LIBRARY} ${COREFOUNDATION_LIBRARY})
endif()

pulse_demo_rpath(uavwall)
pulse_demo_launcher(uavwall run-uavwall.sh)
