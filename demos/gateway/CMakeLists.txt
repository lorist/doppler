# ---------------------------------------------------------------------------
# pulse_gateway — two Pulse instances bridged together to relay raw audio +
# video between two Pexip Infinity conferences using only the Pulse
# data-session input/output APIs. See README.md for the full story.
#
# Builds from the parent showcase project (BUILD_GATEWAY, ON by default).
# ---------------------------------------------------------------------------
add_executable(pulse_gateway
    src/gateway.cpp)

target_link_libraries(pulse_gateway PRIVATE
    imgui
    pexip::pulse
    Threads::Threads)

pulse_demo_rpath(pulse_gateway)
pulse_demo_launcher(pulse_gateway run-pulse-gateway.sh)
