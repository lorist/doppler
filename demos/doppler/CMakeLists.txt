# ---------------------------------------------------------------------------
# doppler — a tiny Pexip Pulse + Dear ImGui video-call demo.
#
# Builds from the parent showcase project (BUILD_DOPPLER, ON by default). The
# parent provides the pexip::pulse target, the shared `imgui` static lib and
# the helpers from cmake/PulseDemo.cmake. See README.md for the walk-through.
# ---------------------------------------------------------------------------
add_executable(doppler
    src/main.cpp
    src/udp_rtp_bridge.cpp)

target_link_libraries(doppler PRIVATE
    imgui
    pexip::pulse
    Threads::Threads)

pulse_demo_rpath(doppler)
pulse_demo_launcher(doppler run-doppler.sh)
