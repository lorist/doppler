// Tiny shim for the small subset of "ImGuiUtils" pexninja.cpp actually uses.
//
// The upstream pexninja repo pulls in a fuller helpers header; here we only
// need ImGuiUtils::HelpToolTip(), which renders a "(?)" marker that shows
// the given text in a tooltip while hovered. This is the classic snippet
// from the official Dear ImGui FAQ, kept local so we don't have to vendor
// a whole utilities library for one helper.
#pragma once

#include <imgui.h>

namespace ImGuiUtils
{

inline void HelpToolTip(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace ImGuiUtils
