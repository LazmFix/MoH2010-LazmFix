#pragma once

// Shared bits of the settings panel: ImGui through ReShade, the panel language and small helpers.
// Source files are UTF-8 (/utf-8): ImGui takes UTF-8 strings.

#include <Windows.h>

#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>

namespace mohpcf::ui {

// Panel language: English by default, Russian from [UI] Language=ru (switch at the top of the panel).
inline bool g_ru = false;

inline const char* T(const char* en, const char* ru) {
  return g_ru ? ru : en;
}

inline const ImVec4 kGood(0.45F, 0.85F, 0.45F, 1.0F);
inline const ImVec4 kWarn(0.95F, 0.65F, 0.30F, 1.0F);
inline const ImVec4 kBad(0.95F, 0.40F, 0.40F, 1.0F);

// Explanation on hover of the last item, wrapped.
inline void tip(const char* text) {
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0F);
    ImGui::TextUnformatted(text, nullptr);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

// A "(?)" marker with an explanation, on the same line as the last item.
inline void help(const char* text) {
  ImGui::SameLine(0.0F, -1.0F);
  ImGui::TextDisabled("(?)");
  tip(text);
}

// Combo / slider width in font heights (the panel follows ReShade's font size).
inline float item_width() {
  return ImGui::GetFontSize() * 15.0F;
}

}  // namespace mohpcf::ui
