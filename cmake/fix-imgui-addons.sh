#!/bin/sh
# ImGui-Addons predates Dear ImGui 1.90, where BeginChild()'s third parameter
# changed from `bool border` to `ImGuiChildFlags`, and asserts were added that
# reject ImGuiWindowFlags values passed in that slot:
#
#   "Cannot specify ImGuiWindowFlags_AlwaysAutoResize for BeginChild().
#    Use ImGuiChildFlags_AlwaysAutoResize!"
#
# This repo pins imgui v1.91.5-docking, so opening the file picker aborted the
# process the moment its nav/search bar rendered.
#
# Run as FetchContent's PATCH_COMMAND with the checkout as $1. Idempotent:
# re-running (or running against an already-fixed tree) is a no-op.
set -e
f="${1:-.}/FileBrowser/ImGuiFileBrowser.cpp"
[ -f "$f" ] || { echo "fix-imgui-addons: $f not found" >&2; exit 1; }

# `true` (border) -> ImGuiChildFlags_Border, and AlwaysAutoResize moves to the
# child-flags slot, where it must be paired with an axis flag.
perl -pi -e '
  s/BeginChild\("##NavigationWindow", nw_size, true, ImGuiWindowFlags_AlwaysAutoResize \| ImGuiWindowFlags_NoScrollbar\)/BeginChild("##NavigationWindow", nw_size, ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar)/;
  s/BeginChild\("##SearchWindow", sw_size, true, ImGuiWindowFlags_AlwaysAutoResize \| ImGuiWindowFlags_NoScrollbar\)/BeginChild("##SearchWindow", sw_size, ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar)/;
  s/BeginChild\("##ScrollingRegion", ImVec2\(0, window_height\), true,/BeginChild("##ScrollingRegion", ImVec2(0, window_height), ImGuiChildFlags_Border,/;
  s/BeginChild\("##InputBarComboBox", input_combobox_sz, true,/BeginChild("##InputBarComboBox", input_combobox_sz, ImGuiChildFlags_Border,/;
  s/BeginChild\("##SupportedExts", ImVec2\(0, cw_height\), true\)/BeginChild("##SupportedExts", ImVec2(0, cw_height), ImGuiChildFlags_Border)/;
' "$f"
