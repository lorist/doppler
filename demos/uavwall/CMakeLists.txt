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

# ---- ImGui-Addons file browser --------------------------------------------
# Provides imgui_addons::ImGuiFileBrowser, used by "+ ADD FILE" in Settings so
# an operator can point the wall at their own footage. Same picker and same
# pinned commit as videowall; see that CMakeLists for why the fix-up runs via
# execute_process rather than PATCH_COMMAND.
include(FetchContent)
# SOURCE_SUBDIR points at a directory that does not exist, which stops
# MakeAvailable from trying to add_subdirectory() content that has no
# CMakeLists.txt of its own. CMake 4 treats that as an error rather than
# quietly synthesising a project scope.
FetchContent_Declare(
    imgui_addons
    GIT_REPOSITORY https://github.com/gallickgunner/ImGui-Addons.git
    GIT_TAG        0b25588ba842b93537f82ae84f27a75f604b04ce
    SOURCE_SUBDIR  _no_cmake_here)
FetchContent_MakeAvailable(imgui_addons)

execute_process(
    COMMAND "${CMAKE_SOURCE_DIR}/cmake/fix-imgui-addons.sh" "${imgui_addons_SOURCE_DIR}"
    RESULT_VARIABLE _addons_fix_result)
if(NOT _addons_fix_result EQUAL 0)
    message(FATAL_ERROR "Failed to patch ImGui-Addons for the Dear ImGui 1.90+ API")
endif()

if(NOT TARGET uavwall_imgui_addons)
    add_library(uavwall_imgui_addons STATIC
        ${imgui_addons_SOURCE_DIR}/FileBrowser/ImGuiFileBrowser.cpp)
    target_include_directories(uavwall_imgui_addons PUBLIC
        ${imgui_addons_SOURCE_DIR}/FileBrowser)
    target_link_libraries(uavwall_imgui_addons PUBLIC imgui)
    target_compile_options(uavwall_imgui_addons PRIVATE
        -Wno-deprecated-declarations -Wno-format-truncation)
endif()

target_link_libraries(uavwall PRIVATE
    imgui
    uavwall_imgui_addons
    pexip::pulse
    Threads::Threads)

# On Windows: winmm for the incoming-call ring (PlaySound), psapi for the
# resident-memory figure in the stats row.
if(WIN32)
    target_link_libraries(uavwall PRIVATE winmm psapi)
    # The CRT's *_s "secure" nagging adds nothing over the portable calls used
    # here; keep the build warning-clean without forking the code per platform.
    target_compile_definitions(uavwall PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# AudioToolbox: the incoming-call ring tone (a macOS system sound).
if(APPLE)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox REQUIRED)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(uavwall PRIVATE ${AUDIOTOOLBOX_LIBRARY} ${COREFOUNDATION_LIBRARY})
endif()

pulse_demo_rpath(uavwall)
pulse_demo_launcher(uavwall run-uavwall.sh)
