/* PexipTheme.h — "Dark Frosted" styling for the pexninja Dear ImGui client.
 *
 * This implements variant 01 "Dark Frosted" from the Pexip redesign canvas
 * ("Pexip App — Design Alternatives"), the alternative described there as
 * *same layout, new visual treatment* — which is what makes it expressible as
 * an ImGui theme rather than a rewrite of pexninja's window layout.
 *
 * The design is a frosted-glass treatment: translucent white panels floating
 * over a dark navy mesh-gradient backdrop, with an azure accent.
 *
 *     shell     linear-gradient(135deg, #0d1220, #111827 40%, #0d1a2a)
 *               + indigo / sky mesh blobs
 *     panels    rgba(255,255,255,0.05), 1px rgba(255,255,255,0.09), radius 14
 *     accent    oklch(0.62 0.18 255)  ==  #2885ef
 *     text      #e8eaf0, plus white at 0.85 / 0.5 / 0.35 / 0.28
 *     font      DM Sans, 13
 *
 * Two notes on translating it to ImGui:
 *
 *  1. `backdrop-filter: blur(20px)` has no ImGui equivalent — ImGui cannot
 *     sample what is behind a window. Over the backdrop this is nearly
 *     invisible, because a blur of a smooth gradient is that same gradient;
 *     the translucent fill alone carries the effect. Over live video the
 *     panels read as tinted rather than frosted.
 *
 *  2. The shell gradient and its mesh blobs are *not* drawn here. Pulse
 *     composites the backdrop image behind the video panes, so that half of
 *     the design lives in assets/make-background.py, which renders the same
 *     gradient and blobs to the PNG the launcher points Pulse at. The two
 *     files are meant to be kept in step.
 *
 * Usage, right after ImGui::CreateContext():
 *
 *     PexipTheme::LoadFonts (io, PEXNINJA_FONT_DIR, content_scale);
 *     PexipTheme::Apply ();
 *     PexipTheme::ApplyImPlot ();
 */

#ifndef _PEXIP_THEME_H_
#define _PEXIP_THEME_H_

#include <cstdio>

#include "imgui.h"
#include "implot.h"

namespace PexipTheme
{

/* 0xRRGGBB -> ImVec4 with an explicit alpha. */
static inline ImVec4
Hex (unsigned int rgb, float a = 1.0f)
{
  return ImVec4 (((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

/* White at an alpha — the design expresses nearly every surface and text
 * colour this way, so it gets its own helper. */
static inline ImVec4
White (float a)
{
  return ImVec4 (1.0f, 1.0f, 1.0f, a);
}

/* --- Accent ------------------------------------------------------------- */
/* AccentPrimary from the design handoff (design_handoff_pexninja_refresh/
 * README.md): the doc pins #5B6CE8 as the final sRGB value for
 * oklch(0.62 0.18 255) and asks for a precise match. */
constexpr unsigned int kAccent = 0x5B6CE8;
constexpr unsigned int kAccentHover = 0x6D7CEB; /* AccentHover               */
constexpr unsigned int kAccentDim = 0x4A5BD6;   /* AccentPressed             */
constexpr unsigned int kIndigo = 0x6366F1;     /* rgba(99,102,241,·) glow      */
constexpr unsigned int kSky = 0x38BDF8;        /* rgba(56,189,248,·) mesh blob */

/* --- Text --------------------------------------------------------------- */
constexpr unsigned int kText = 0xE8EAF0; /* the design's base text colour */

/* --- Signals ------------------------------------------------------------ */
constexpr unsigned int kGreen = 0x22C55E;
constexpr unsigned int kAmber = 0xF59E0B;
constexpr unsigned int kRed = 0xFF5F57;

/* --- Shell (mirrors make-background.py; used where a window must be opaque) */
constexpr unsigned int kShellDeep = 0x0D1220;
constexpr unsigned int kShellMid = 0x111827;

/* --- Glass -------------------------------------------------------------- */
constexpr float kGlassFill = 0.05f;        /* rgba(255,255,255,0.05) */
constexpr float kGlassFillRaised = 0.07f;  /* search bar / hover     */
constexpr float kGlassBorder = 0.09f;      /* rgba(255,255,255,0.09) */

/* --- Radii -------------------------------------------------------------- */
constexpr float kRadiusPanel = 14.0f;
constexpr float kRadiusControl = 10.0f;
constexpr float kRadiusSmall = 8.0f;

/**
 * Load DM Sans, the typeface the redesign specifies.
 *
 * Sized at 14 px rather than the design's 13: several pexninja windows are
 * pinned to a fixed pixel width and were laid out against ImGui's built-in
 * 13 px Proggy font, and DM Sans is a little narrower than Inter at the same
 * size, so 14 fits while staying legible on a large display.
 *
 * Scaled by the display's content scale and drawn back down via
 * FontGlobalScale, so glyphs stay crisp on Retina. On a 1x display this is an
 * identity transform.
 *
 * Falls back to ImGui's built-in font if the TTF is missing, rather than
 * failing the launch.
 */
static inline void
LoadFonts (ImGuiIO & io, const char * font_dir, float content_scale)
{
  if (content_scale <= 0.0f)
    content_scale = 1.0f;

  const float base_size = 14.0f * content_scale;
  char path[1024];

  snprintf (path, sizeof (path), "%s/DMSans.ttf", font_dir);
  ImFont * body = io.Fonts->AddFontFromFileTTF (path, base_size);

  if (body == nullptr) {
    io.Fonts->AddFontDefault ();
    return;
  }

  io.FontGlobalScale = 1.0f / content_scale;
  io.FontDefault = body;
}

/**
 * Apply the Dark Frosted palette and geometry to the ImGui style.
 */
static inline void
Apply ()
{
  ImGuiStyle & s = ImGui::GetStyle ();
  ImVec4 * c = s.Colors;

  /* --- Geometry --------------------------------------------------------- */
  s.WindowRounding = kRadiusPanel;
  s.ChildRounding = kRadiusControl;
  s.PopupRounding = kRadiusControl;
  s.FrameRounding = kRadiusControl;
  s.ScrollbarRounding = kRadiusSmall;
  s.GrabRounding = kRadiusSmall;
  s.TabRounding = kRadiusSmall;

  /* Hairline borders — the design's panels are defined by a 1px white edge
   * at 9% rather than by a fill contrast. */
  s.WindowBorderSize = 1.0f;
  s.ChildBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  s.FrameBorderSize = 1.0f;

  /* Restrained enough that the fixed-width windows pexninja pins with
   * ImGuiCond_Always still fit their contents. */
  s.WindowPadding = ImVec2 (10, 10);
  s.FramePadding = ImVec2 (8, 5);
  s.ItemSpacing = ImVec2 (7, 6);
  s.ItemInnerSpacing = ImVec2 (6, 4);
  s.ScrollbarSize = 11.0f;
  s.GrabMinSize = 10.0f;
  s.WindowTitleAlign = ImVec2 (0.0f, 0.5f);

  /* --- Text ------------------------------------------------------------- */
  c[ImGuiCol_Text] = Hex (kText);
  c[ImGuiCol_TextDisabled] = White (0.28f);
  c[ImGuiCol_TextSelectedBg] = Hex (kAccent, 0.40f);

  /* --- Surfaces --------------------------------------------------------- */
  /* Translucent so the Pulse backdrop reads through — this is the whole
   * point of the frosted treatment. */
  c[ImGuiCol_WindowBg] = White (kGlassFill);
  c[ImGuiCol_ChildBg] = White (0.0f);
  /* Popups must stay legible over arbitrary content, so they get the shell
   * colour at high opacity instead of a 5% wash. */
  c[ImGuiCol_PopupBg] = Hex (kShellMid, 0.96f);
  c[ImGuiCol_MenuBarBg] = White (0.06f);

  /* --- Borders ---------------------------------------------------------- */
  c[ImGuiCol_Border] = White (kGlassBorder);
  c[ImGuiCol_BorderShadow] = White (0.0f);

  /* --- Inputs ----------------------------------------------------------- */
  c[ImGuiCol_FrameBg] = White (kGlassFill);
  c[ImGuiCol_FrameBgHovered] = White (kGlassFillRaised);
  c[ImGuiCol_FrameBgActive] = White (0.10f);

  /* --- Title bars ------------------------------------------------------- */
  c[ImGuiCol_TitleBg] = White (0.06f);
  c[ImGuiCol_TitleBgActive] = White (0.10f);
  c[ImGuiCol_TitleBgCollapsed] = White (0.04f);

  /* --- Scrollbars ------------------------------------------------------- */
  c[ImGuiCol_ScrollbarBg] = White (0.0f);
  c[ImGuiCol_ScrollbarGrab] = White (0.14f);
  c[ImGuiCol_ScrollbarGrabHovered] = White (0.22f);
  c[ImGuiCol_ScrollbarGrabActive] = Hex (kAccent, 0.85f);

  /* --- Controls --------------------------------------------------------- */
  c[ImGuiCol_CheckMark] = Hex (kAccent);
  c[ImGuiCol_SliderGrab] = Hex (kAccent, 0.90f);
  c[ImGuiCol_SliderGrabActive] = Hex (kAccent);

  /* Glass by default, accent on press. ImGuiCol_Button paints every button in
   * the app, so it can't be the design's solid-accent Call button; that
   * treatment is reserved for the pressed state. */
  c[ImGuiCol_Button] = White (0.08f);
  c[ImGuiCol_ButtonHovered] = White (0.14f);
  c[ImGuiCol_ButtonActive] = Hex (kAccent);

  /* --- Headers (selectables, tree nodes, menu items) -------------------- */
  /* The design's active nav pill: rgba(99,102,241,0.22). */
  c[ImGuiCol_Header] = Hex (kIndigo, 0.22f);
  c[ImGuiCol_HeaderHovered] = Hex (kIndigo, 0.32f);
  c[ImGuiCol_HeaderActive] = Hex (kAccent, 0.80f);

  /* --- Separators / resize grips ---------------------------------------- */
  c[ImGuiCol_Separator] = White (0.09f);
  c[ImGuiCol_SeparatorHovered] = Hex (kAccent, 0.60f);
  c[ImGuiCol_SeparatorActive] = Hex (kAccent);
  c[ImGuiCol_ResizeGrip] = White (0.12f);
  c[ImGuiCol_ResizeGripHovered] = Hex (kAccent, 0.55f);
  c[ImGuiCol_ResizeGripActive] = Hex (kAccent);

  /* --- Tabs ------------------------------------------------------------- */
  c[ImGuiCol_Tab] = White (0.05f);
  c[ImGuiCol_TabHovered] = White (0.12f);
  c[ImGuiCol_TabActive] = Hex (kIndigo, 0.28f);
  c[ImGuiCol_TabUnfocused] = White (0.04f);
  c[ImGuiCol_TabUnfocusedActive] = White (0.09f);

  /* --- Docking ---------------------------------------------------------- */
  c[ImGuiCol_DockingPreview] = Hex (kAccent, 0.40f);
  c[ImGuiCol_DockingEmptyBg] = Hex (kShellDeep, 0.0f);

  /* --- Plots (ImGui's own) ---------------------------------------------- */
  c[ImGuiCol_PlotLines] = Hex (kAccent);
  c[ImGuiCol_PlotLinesHovered] = Hex (kSky);
  c[ImGuiCol_PlotHistogram] = Hex (kIndigo);
  c[ImGuiCol_PlotHistogramHovered] = Hex (kAccent);

  /* --- Tables ----------------------------------------------------------- */
  c[ImGuiCol_TableHeaderBg] = White (0.06f);
  c[ImGuiCol_TableBorderStrong] = White (0.12f);
  c[ImGuiCol_TableBorderLight] = White (0.06f);
  c[ImGuiCol_TableRowBg] = White (0.0f);
  c[ImGuiCol_TableRowBgAlt] = White (0.03f);

  /* --- Misc ------------------------------------------------------------- */
  c[ImGuiCol_NavHighlight] = Hex (kAccent);
  c[ImGuiCol_DragDropTarget] = Hex (kSky);
  c[ImGuiCol_ModalWindowDimBg] = Hex (kShellDeep, 0.65f);
}

/**
 * Match the media-stats plots to the same palette.
 */
static inline void
ApplyImPlot ()
{
  ImPlotStyle & p = ImPlot::GetStyle ();

  p.Colors[ImPlotCol_FrameBg] = White (0.0f);
  p.Colors[ImPlotCol_PlotBg] = White (0.04f);
  p.Colors[ImPlotCol_PlotBorder] = White (0.09f);
  p.Colors[ImPlotCol_LegendBg] = Hex (kShellMid, 0.92f);
  p.Colors[ImPlotCol_LegendBorder] = White (0.09f);
  p.Colors[ImPlotCol_LegendText] = Hex (kText);
  p.Colors[ImPlotCol_TitleText] = Hex (kText);
  p.Colors[ImPlotCol_InlayText] = White (0.50f);
  p.Colors[ImPlotCol_AxisText] = White (0.50f);
  p.Colors[ImPlotCol_AxisGrid] = White (0.08f);

  p.PlotPadding = ImVec2 (10, 8);
  p.LegendPadding = ImVec2 (8, 8);

  /* Qualitative ramp drawn from the design's own accents. */
  static ImVec4 kFrostedColormap[5] = {
    Hex (kAccent), Hex (kSky), Hex (kIndigo), Hex (kGreen), Hex (kAmber),
  };
  ImPlot::AddColormap ("PexipFrosted", kFrostedColormap, 5);
}

} /* namespace PexipTheme */

#endif /* _PEXIP_THEME_H_ */
