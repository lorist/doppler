/* Theme.h — pexclient's design tokens and ImGui style.
 *
 * Every value here comes from design_handoff_pexninja_refresh/README.md
 * (variant 01, "Dark Frosted"), which pins them as final. The token names
 * mirror the handoff so the two documents can be diffed side by side.
 *
 * The handoff targets WPF; the translation rules used here are the ones it
 * itself recommends for the no-blur baseline: flat translucent fills over the
 * gradient-mesh background ("the design holds up: the mesh blobs and layered
 * opacity carry most of the depth").
 */

#ifndef PEXCLIENT_THEME_H
#define PEXCLIENT_THEME_H

#include <cstdio>

#include <imgui.h>

namespace theme
{

static inline ImVec4
Hex (unsigned int rgb, float a = 1.0f)
{
  return ImVec4 (((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

static inline ImU32
HexU32 (unsigned int rgb, float a = 1.0f)
{
  return ImGui::ColorConvertFloat4ToU32 (Hex (rgb, a));
}

static inline ImU32
WhiteU32 (float a)
{
  return ImGui::ColorConvertFloat4ToU32 (ImVec4 (1, 1, 1, a));
}

/* --- Accent (shared) ---------------------------------------------------- */
constexpr unsigned int AccentPrimary = 0x5B6CE8;
constexpr unsigned int AccentHover = 0x6D7CEB;
constexpr unsigned int AccentPressed = 0x4A5BD6;
constexpr unsigned int AccentGradientEnd = 0x8B5CF6; /* logo mark + avatar fallback only */
constexpr unsigned int StatusOnline = 0x22C55E;
constexpr unsigned int StatusError = 0xEF4444; /* missed-call / not-registered red */
constexpr unsigned int StarActive = 0xF59E0B;

/* --- Dark theme window background (135° gradient stops) ------------------ */
constexpr unsigned int WindowBgStart = 0x0D1220;
constexpr unsigned int WindowBgMid = 0x111827;
constexpr unsigned int WindowBgEnd = 0x0D1A2A;

/* Gradient mesh blobs. */
constexpr unsigned int BlobIndigo = 0x6366F1; /* @18%, top-left     */
constexpr unsigned int BlobSky = 0x38BDF8;    /* @12%, bottom-right */

/* --- Dark theme surfaces (all "white @ alpha") ---------------------------- */
constexpr float PanelFill = 0.05f;
constexpr float PanelStroke = 0.09f;
constexpr float PanelFillRaised = 0.07f;
constexpr float RowHover = 0.05f;
constexpr float TextPrimary = 0.90f;
constexpr float TextSecondary = 0.50f;
constexpr float TextTertiary = 0.32f;
constexpr float TextLabel = 0.28f;
constexpr float IconMuted = 0.30f;
constexpr float Divider = 0.05f;

/* --- Radii ---------------------------------------------------------------- */
constexpr float RadiusPanel = 14.0f;
constexpr float RadiusControl = 10.0f;
constexpr float RadiusRow = 9.0f;
constexpr float RadiusSmall = 8.0f;

/* --- Spacing -------------------------------------------------------------- */
constexpr float WindowPad = 20.0f;  /* window edge padding        */
constexpr float PanelGap = 14.0f;   /* gap between the two panels */

/* --- Fonts ----------------------------------------------------------------
 * The handoff's type scale needs real weights, so the static DM Sans TTFs are
 * loaded at the roles below (sizes in CSS px == ImGui units at 1x).
 */
struct Fonts
{
  ImFont * body = nullptr;      /* 400 @ 13.5 — inputs, body copy            */
  ImFont * bodyBold = nullptr;  /* 600 @ 13.5 — buttons, headers, names      */
  ImFont * small_ = nullptr;    /* 400 @ 11.5 — times, footer, addresses     */
  ImFont * smallMed = nullptr;  /* 500 @ 11   — favourite name labels        */
  ImFont * label = nullptr;     /* 700 @ 10.5 + tracking — section labels    */
  ImFont * title = nullptr;     /* 500 @ 12   — title-bar app name           */
};

/* Load the DM Sans set, rasterised at content_scale for crisp Retina glyphs
 * (FontGlobalScale draws them back at logical size). Any missing file falls
 * back to the ImGui default font rather than failing the launch. */
static inline Fonts
LoadFonts (ImGuiIO & io, const char * font_dir, float content_scale)
{
  if (content_scale <= 0.0f)
    content_scale = 1.0f;

  Fonts f;
  char reg[1024], semi[1024], med[1024], bold[1024];
  snprintf (reg, sizeof (reg), "%s/DMSans-Regular.ttf", font_dir);
  snprintf (semi, sizeof (semi), "%s/DMSans-SemiBold.ttf", font_dir);
  snprintf (med, sizeof (med), "%s/DMSans-Medium.ttf", font_dir);
  snprintf (bold, sizeof (bold), "%s/DMSans-Bold.ttf", font_dir);

  const float s = content_scale;

  f.body = io.Fonts->AddFontFromFileTTF (reg, 13.5f * s);
  if (f.body == nullptr) {
    /* Fonts unavailable — degrade to the built-in font for every role. */
    f.body = io.Fonts->AddFontDefault ();
    f.bodyBold = f.small_ = f.smallMed = f.label = f.title = f.body;
    return f;
  }

  f.bodyBold = io.Fonts->AddFontFromFileTTF (semi, 13.5f * s);

  f.small_ = io.Fonts->AddFontFromFileTTF (reg, 11.5f * s);
  f.smallMed = io.Fonts->AddFontFromFileTTF (med, 11.0f * s);

  /* Section labels: 10.5/700 with +0.8 tracking, per the handoff. */
  ImFontConfig label_cfg;
  label_cfg.GlyphExtraSpacing.x = 0.8f * s;
  f.label = io.Fonts->AddFontFromFileTTF (bold, 10.5f * s, &label_cfg);

  ImFontConfig title_cfg;
  title_cfg.GlyphExtraSpacing.x = 0.3f * s;
  f.title = io.Fonts->AddFontFromFileTTF (med, 12.0f * s, &title_cfg);

  io.FontGlobalScale = 1.0f / s;
  io.FontDefault = f.body;
  return f;
}

/* Apply the token set to the ImGui style. pexclient draws most of its chrome
 * with the draw-list directly, so this covers the widgets that remain stock:
 * text inputs, scrollbars, popups, child panels. */
static inline void
Apply ()
{
  ImGuiStyle & s = ImGui::GetStyle ();
  ImVec4 * c = s.Colors;

  s.WindowRounding = RadiusPanel;
  s.ChildRounding = RadiusPanel;
  s.PopupRounding = RadiusControl;
  s.FrameRounding = RadiusControl;
  s.ScrollbarRounding = RadiusSmall;
  s.GrabRounding = RadiusSmall;

  s.WindowBorderSize = 0.0f;
  s.ChildBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  s.FrameBorderSize = 1.0f;

  s.WindowPadding = ImVec2 (0, 0); /* the shell window is a raw canvas */
  s.FramePadding = ImVec2 (16, 10); /* search field: padding 10 16      */
  s.ItemSpacing = ImVec2 (10, 8);
  s.ScrollbarSize = 10.0f;

  c[ImGuiCol_Text] = Hex (0xFFFFFF, TextPrimary);
  c[ImGuiCol_TextDisabled] = Hex (0xFFFFFF, TextTertiary);
  c[ImGuiCol_TextSelectedBg] = Hex (AccentPrimary, 0.40f);

  c[ImGuiCol_WindowBg] = Hex (WindowBgStart, 0.0f); /* shell paints itself */
  c[ImGuiCol_ChildBg] = Hex (0xFFFFFF, PanelFill);
  c[ImGuiCol_PopupBg] = Hex (WindowBgMid, 0.97f);
  c[ImGuiCol_Border] = Hex (0xFFFFFF, PanelStroke);
  c[ImGuiCol_BorderShadow] = Hex (0x000000, 0.0f);

  c[ImGuiCol_FrameBg] = Hex (0xFFFFFF, PanelFillRaised);
  c[ImGuiCol_FrameBgHovered] = Hex (0xFFFFFF, PanelFillRaised + 0.02f);
  c[ImGuiCol_FrameBgActive] = Hex (0xFFFFFF, PanelFillRaised + 0.03f);

  c[ImGuiCol_ScrollbarBg] = Hex (0xFFFFFF, 0.0f);
  c[ImGuiCol_ScrollbarGrab] = Hex (0xFFFFFF, 0.12f);
  c[ImGuiCol_ScrollbarGrabHovered] = Hex (0xFFFFFF, 0.20f);
  c[ImGuiCol_ScrollbarGrabActive] = Hex (AccentPrimary, 0.80f);

  c[ImGuiCol_Button] = Hex (0xFFFFFF, 0.08f);
  c[ImGuiCol_ButtonHovered] = Hex (0xFFFFFF, 0.14f);
  c[ImGuiCol_ButtonActive] = Hex (AccentPressed);

  c[ImGuiCol_Header] = Hex (AccentPrimary, 0.22f);
  c[ImGuiCol_HeaderHovered] = Hex (0xFFFFFF, RowHover);
  c[ImGuiCol_HeaderActive] = Hex (AccentPrimary, 0.35f);

  c[ImGuiCol_Separator] = Hex (0xFFFFFF, Divider);
  c[ImGuiCol_CheckMark] = Hex (AccentPrimary);
  c[ImGuiCol_SliderGrab] = Hex (AccentPrimary, 0.9f);
  c[ImGuiCol_SliderGrabActive] = Hex (AccentPrimary);
  c[ImGuiCol_NavHighlight] = Hex (AccentPrimary);
  c[ImGuiCol_ModalWindowDimBg] = Hex (WindowBgStart, 0.65f);
}

} /* namespace theme */

#endif /* PEXCLIENT_THEME_H */
