# ---------------------------------------------------------------------------
# videowall — a multi-instance Pulse compositor. Many Pulse instances (one per
# source) are composited onto a single superwide canvas in our own draw list.
#
# Registered in the root CMakeLists.txt behind the BUILD_VIDEOWALL option.
# Uses the shared helpers from cmake/PulseDemo.cmake.
# ---------------------------------------------------------------------------
add_executable(videowall
    src/main.cpp)

# ---- ImGui-Addons file browser --------------------------------------------
# Provides imgui_addons::ImGuiFileBrowser, used by the "Browse..." buttons that
# point Image / MP4 sources at a file on disk (the same picker pexninja uses).
# Upstream has no release tags, so pin to a known-good commit. It has no
# CMakeLists, so we compile its single .cpp into a small static lib that links
# the shared `imgui` target for its headers.
include(FetchContent)
FetchContent_Declare(
    imgui_addons
    GIT_REPOSITORY https://github.com/gallickgunner/ImGui-Addons.git
    GIT_TAG        0b25588ba842b93537f82ae84f27a75f604b04ce)
FetchContent_MakeAvailable(imgui_addons)

add_library(videowall_imgui_addons STATIC
    ${imgui_addons_SOURCE_DIR}/FileBrowser/ImGuiFileBrowser.cpp)
target_include_directories(videowall_imgui_addons PUBLIC
    ${imgui_addons_SOURCE_DIR}/FileBrowser)
target_link_libraries(videowall_imgui_addons PUBLIC imgui)
# The file browser's .cpp uses some sprintf/strcpy-style helpers that newer
# glibc/gcc flag as deprecated; silence those so the build log stays clean.
target_compile_options(videowall_imgui_addons PRIVATE
    -Wno-deprecated-declarations -Wno-format-truncation)

target_link_libraries(videowall PRIVATE
    imgui
    videowall_imgui_addons
    pexip::pulse
    Threads::Threads)

# macOS still ships OpenGL but marks every GL entry point deprecated; silence
# those so the demo builds cleanly there as well as on Linux.
if(APPLE)
    target_compile_definitions(videowall PRIVATE GL_SILENCE_DEPRECATION)
endif()

# Find libpexpulse.so at run time, and generate build/run-videowall.sh so the
# demo runs without the user setting LD_LIBRARY_PATH by hand.
pulse_demo_rpath(videowall)
pulse_demo_launcher(videowall run-videowall.sh)
