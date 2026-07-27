# ---------------------------------------------------------------------------
# <your-demo> — TEMPLATE. Copy this folder to demos/<your-demo>/ and rename the
# target below. The parent showcase project provides everything you link
# against (pexip::pulse, the shared `imgui` lib) and the helpers used here
# (pulse_demo_rpath / pulse_demo_launcher from cmake/PulseDemo.cmake).
#
# Don't forget to register the demo in the root CMakeLists.txt with a
# BUILD_<YOUR_DEMO> option + add_subdirectory(). See docs/adding-a-demo.md.
# ---------------------------------------------------------------------------
add_executable(<your-demo>
    src/main.cpp)

target_link_libraries(<your-demo> PRIVATE
    imgui
    pexip::pulse
    Threads::Threads)

# Find libpexpulse.so at run time, and generate build/run-<your-demo>.sh so the
# demo runs without the user setting LD_LIBRARY_PATH by hand.
pulse_demo_rpath(<your-demo>)
pulse_demo_launcher(<your-demo> run-<your-demo>.sh)
