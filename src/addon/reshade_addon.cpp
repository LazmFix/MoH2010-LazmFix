// ReShade add-on front-end for MoH 2010 LazmFix: the settings panel (Home -> Add-ons).
//
// The core is the dinput8.dll proxy next to moh.exe: it is loaded by the game itself and keeps working
// without ReShade (settings from MoH2010.LazmFix.ini). This add-on finds it (by full path, so never the
// system dinput8.dll), shows its state, changes its settings, and turns an exclusive fullscreen
// request into borderless.

#include "addon/display.h"
#include "addon/ui.h"
#include "common/game_ini.h"
#include "core/core_api.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" __declspec(dllexport) const char* NAME = "MoH 2010 LazmFix";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Medal of Honor (2010) single player: startup hang fix, borderless window, mouse, FOV, aiming, HUD and subtitle scaling.";

namespace {

using mohpcf::ui::T;
using mohpcf::ui::kGood;
using mohpcf::ui::kWarn;
using mohpcf::ui::kBad;
using mohpcf::ui::item_width;
namespace api = mohpcf::api;

HMODULE g_addon_module = nullptr;
bool g_core_attempted = false;
api::GetStatusFn g_get_status = nullptr;
api::SetEnabledFn g_set_enabled = nullptr;
api::SetSensitivityFn g_set_sensitivity = nullptr;
api::SetOverlayOpenFn g_set_overlay_open = nullptr;
api::SaveSettingsFn g_save_settings = nullptr;
mohpcf::display::RequestBorderlessFn g_request_borderless = nullptr;
api::SetSubtitleScaleFn g_set_subtitle_scale = nullptr;
api::SetFovFn g_set_fov = nullptr;
api::SetMarkerScaleFn g_set_marker_scale = nullptr;
api::SetCrosshairScaleFn g_set_crosshair_scale = nullptr;
api::SetAdsFn g_set_ads = nullptr;
api::SetAdsFovFn g_set_ads_fov = nullptr;
std::string g_core_error;
bool g_menu_open = false;

bool is_game_process() {
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return false;
  }
  std::wstring name = std::filesystem::path(path).filename().wstring();
  for (auto& c : name) {
    c = static_cast<wchar_t>(std::towlower(c));
  }
  return name == api::kGameExeName;
}

std::filesystem::path addon_directory() {
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(g_addon_module, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return {};
  }
  return std::filesystem::path(path).parent_path();
}

std::wstring fix_ini() {
  static const std::wstring path = (addon_directory() / L"MoH2010.LazmFix.ini").wstring();
  return path;
}

template <typename Fn>
Fn core_export(HMODULE core, const char* name) {
  return reinterpret_cast<Fn>(GetProcAddress(core, name));
}

void ensure_core() {
  if (g_core_attempted) {
    return;
  }
  g_core_attempted = true;
  // Full path: the system dinput8.dll is loaded in the process too (by the proxy itself).
  const auto path = addon_directory() / api::kCoreFileName;
  HMODULE core = GetModuleHandleW(path.c_str());
  if (core == nullptr) {
    g_core_error = "dinput8.dll (MoH 2010 LazmFix) is not loaded from the game folder. Is it next to moh.exe?";
    return;
  }
  g_get_status = core_export<api::GetStatusFn>(core, "MohPcfGetStatus");
  g_set_enabled = core_export<api::SetEnabledFn>(core, "MohPcfSetEnabled");
  g_set_sensitivity = core_export<api::SetSensitivityFn>(core, "MohPcfSetSensitivity");
  g_set_overlay_open = core_export<api::SetOverlayOpenFn>(core, "MohPcfSetOverlayOpen");
  g_save_settings = core_export<api::SaveSettingsFn>(core, "MohPcfSaveSettings");
  g_request_borderless = core_export<mohpcf::display::RequestBorderlessFn>(core, "MohPcfRequestBorderless");
  g_set_subtitle_scale = core_export<api::SetSubtitleScaleFn>(core, "MohPcfSetSubtitleScale");
  g_set_fov = core_export<api::SetFovFn>(core, "MohPcfSetFov");
  g_set_marker_scale = core_export<api::SetMarkerScaleFn>(core, "MohPcfSetMarkerScale");
  g_set_crosshair_scale = core_export<api::SetCrosshairScaleFn>(core, "MohPcfSetCrosshairScale");
  g_set_ads = core_export<api::SetAdsFn>(core, "MohPcfSetAds");
  g_set_ads_fov = core_export<api::SetAdsFovFn>(core, "MohPcfSetAdsFov");
  api::StatusV2 probe{};
  if (g_request_borderless == nullptr || g_set_subtitle_scale == nullptr || g_set_fov == nullptr ||
      g_set_marker_scale == nullptr || g_set_crosshair_scale == nullptr || g_set_ads == nullptr || g_set_ads_fov == nullptr ||
      g_get_status == nullptr || g_set_enabled == nullptr || g_set_sensitivity == nullptr || g_set_overlay_open == nullptr ||
      g_save_settings == nullptr || !g_get_status(&probe)) {
    g_core_error = "The dinput8.dll in the game folder does not match this add-on version: copy both files from the same release.";
    g_get_status = nullptr;
  }
}

// ---- Language ---------------------------------------------------------------------------------

void load_language() {
  static bool loaded = false;
  if (loaded) {
    return;
  }
  loaded = true;
  wchar_t buffer[16]{};
  GetPrivateProfileStringW(L"UI", L"Language", L"en", buffer, 16, fix_ini().c_str());
  mohpcf::ui::g_ru = mohpcf::game_ini::lower(buffer) == L"ru";
}

// ---- Sensitivity ------------------------------------------------------------------------------
// MoH turns a mouse count into 0.5 * MouseSensitivity rotation units (65536 = 360 deg), measured in game.
// CoD4 / MW Remastered: sensitivity * m_yaw (0.022) degrees per count. "As CoD4 / MWR" sets the fix
// multiplier so that one count turns the camera exactly as far as in MWR at the chosen value.
constexpr double kMohUnitsPerSens = 0.5;
constexpr double kUnitsToDeg = 360.0 / 65536.0;
constexpr double kMwrYaw = 0.022;

enum SensMode { kSensGame = 0, kSensMultiplier = 1, kSensMwr = 2 };
int g_sens_mode = kSensGame;
float g_mwr_sens = 3.0F;
float g_game_sens = 35.0F;
mohpcf::game_ini::FileWatch g_game_sens_watch;  // MOHAInput.ini (the game menu writes it)
bool g_sens_loaded = false;

void load_sensitivity_settings() {
  if (g_sens_loaded) {
    return;
  }
  g_sens_loaded = true;
  const std::wstring ini = fix_ini();
  wchar_t buffer[64]{};
  GetPrivateProfileStringW(L"Mouse", L"Mode", L"multiplier", buffer, 64, ini.c_str());
  const bool enabled = GetPrivateProfileIntW(L"Mouse", L"Enabled", 1, ini.c_str()) != 0;
  g_sens_mode = !enabled ? kSensGame : std::wstring(buffer) == L"mwr" ? kSensMwr : kSensMultiplier;
  GetPrivateProfileStringW(L"Mouse", L"MwrSensitivity", L"3.0", buffer, 64, ini.c_str());
  g_mwr_sens = std::clamp(static_cast<float>(std::wcstod(buffer, nullptr)), 0.1F, 20.0F);
}

void save_sensitivity_settings() {
  wchar_t buffer[64]{};
  WritePrivateProfileStringW(L"Mouse", L"Mode", g_sens_mode == kSensMwr ? L"mwr" : L"multiplier", fix_ini().c_str());
  std::swprintf(buffer, 64, L"%.3f", g_mwr_sens);
  WritePrivateProfileStringW(L"Mouse", L"MwrSensitivity", buffer, fix_ini().c_str());
  g_save_settings();  // Enabled and the multipliers.
}

void read_game_sensitivity() {
  const std::wstring ini = mohpcf::game_ini::input_ini();
  if (!g_game_sens_watch.changed(ini, ImGui::GetTime())) {
    return;
  }
  const float sens = static_cast<float>(std::wcstod(mohpcf::game_ini::read(ini, L"Engine.PlayerInput", L"MouseSensitivity").c_str(), nullptr));
  if (sens > 0.0F) {
    g_game_sens = sens;
  }
}

float mwr_multiplier() {
  return static_cast<float>(g_mwr_sens * kMwrYaw / kUnitsToDeg / (kMohUnitsPerSens * g_game_sens));
}

// Keeps the core's multipliers in line with the mode (also when the game slider changes).
void sync_sensitivity(const api::StatusV2& status) {
  load_sensitivity_settings();
  read_game_sensitivity();
  if (g_sens_mode == kSensMwr) {
    const float want = mwr_multiplier();
    if (status.enabled == 0 || std::fabs(want - status.sensitivity_x) > 0.0005F || std::fabs(want - status.sensitivity_y) > 0.0005F) {
      g_set_enabled(true);
      g_set_sensitivity(want, want);
      g_save_settings();
    }
  }
}

// ---- Field of view ----------------------------------------------------------------------------
// The FOV setting is MoH's horizontal degrees at 16:9 (the core widens it for other aspect ratios).
// CoD4 / MWR's cg_fov is horizontal degrees of a 4:3 picture: at 16:9, tan(moh/2) = tan(cod/2) * 4/3.
constexpr double kPi = 3.14159265358979323846;
constexpr double kWide = 16.0 / 9.0;
double deg_to_rad(double d) { return d * kPi / 180.0; }
double rad_to_deg(double r) { return r * 180.0 / kPi; }
double moh_to_cod(double h) { return rad_to_deg(2.0 * std::atan(std::tan(deg_to_rad(h) / 2.0) / kWide * (4.0 / 3.0))); }
double cod_to_moh(double cod) { return rad_to_deg(2.0 * std::atan(std::tan(deg_to_rad(cod) / 2.0) * 0.75 * kWide)); }

// ---- Presets ----------------------------------------------------------------------------------

void apply_preset_game() {
  g_sens_mode = kSensGame;
  g_set_enabled(false);
  g_set_sensitivity(1.0F, 1.0F);
  g_set_fov(0.0F);
  g_set_ads(0, 0.75F);
  g_set_ads_fov(0);
  save_sensitivity_settings();
}

void apply_preset_mwr() {
  g_sens_mode = kSensMwr;
  const float m = mwr_multiplier();
  g_set_enabled(true);
  g_set_sensitivity(m, m);
  g_set_fov(static_cast<float>(cod_to_moh(65.0)));
  g_set_ads(1, 0.75F);
  g_set_ads_fov(1);
  save_sensitivity_settings();
}

void draw_presets(const api::StatusV2& status) {
  const auto mwr_fov = static_cast<float>(cod_to_moh(65.0));
  const bool is_game = g_sens_mode == kSensGame && status.fov_setting <= 0.0F && status.ads_mode == 0 && status.ads_fov_mode == 0;
  const bool is_mwr = g_sens_mode == kSensMwr && std::fabs(status.fov_setting - mwr_fov) < 0.15F && status.ads_mode == 1 &&
                      status.ads_fov_mode == 1;
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(T("Preset:", "Пресет:"), nullptr);
  ImGui::SameLine(0.0F, -1.0F);
  if (ImGui::RadioButton(T("Game", "Как в игре"), is_game)) {
    apply_preset_game();
  }
  mohpcf::ui::tip(T("The game's own sensitivity, field of view (70) and aiming.",
                    "Чувствительность, поле зрения (70) и прицеливание самой игры."));
  ImGui::SameLine(0.0F, -1.0F);
  if (ImGui::RadioButton("CoD4 / MW Remastered", is_mwr)) {
    apply_preset_mwr();
  }
  mohpcf::ui::tip(T("Feels like Call of Duty 4 / Modern Warfare Remastered: the same turn per mouse count as MWR at "
                    "your MWR sensitivity, the view of cg_fov 65, and aiming slowed and zoomed the way MWR does it.",
                    "Как в Call of Duty 4 / Modern Warfare Remastered: тот же поворот на отсчёт мыши, что в MWR при вашей "
                    "чувствительности MWR, обзор как cg_fov 65, а прицеливание замедляет мышь и приближает как в MWR."));
  if (!is_game && !is_mwr) {
    ImGui::SameLine(0.0F, -1.0F);
    ImGui::TextDisabled("%s", T("(custom)", "(свои настройки)"));
  }
}

void draw_sensitivity(const api::StatusV2& status) {
  ImGui::SeparatorText(T("Mouse sensitivity", "Чувствительность мыши"));
  const char* modes[] = {T("Game", "Как в игре"), T("Multiplier", "Множитель"), T("As CoD4 / MWR", "Как в CoD4 / MWR")};
  int mode = g_sens_mode;
  ImGui::SetNextItemWidth(mohpcf::ui::item_width());
  if (ImGui::Combo(T("Mode##sens", "Режим##sens"), &mode, modes, 3, -1) && mode != g_sens_mode) {
    g_sens_mode = mode;
    if (mode == kSensGame) {
      g_set_enabled(false);
    } else if (mode == kSensMultiplier) {
      g_set_enabled(true);
    } else {
      const float m = mwr_multiplier();
      g_set_enabled(true);
      g_set_sensitivity(m, m);
    }
    save_sensitivity_settings();
  }
  mohpcf::ui::help(T("The game reads the mouse raw (DirectInput, no Windows acceleration). Game: its slider only. "
                     "Multiplier: on top of the game's slider. As CoD4 / MWR: type your MWR sensitivity and get the same "
                     "turn per mouse count (follows the game's slider too).",
                     "Игра читает мышь напрямую (DirectInput, без ускорения Windows). Как в игре: только её ползунок. "
                     "Множитель: поверх ползунка игры. Как в CoD4 / MWR: введите чувствительность из MWR и получите тот "
                     "же поворот на отсчёт мыши (учитывает и ползунок игры)."));
  if (g_sens_mode == kSensMultiplier) {
    float x = status.sensitivity_x;
    ImGui::SetNextItemWidth(mohpcf::ui::item_width());
    if (ImGui::SliderFloat(T("Multiplier", "Множитель"), &x, api::kMinSensitivity, 5.0F, "%.3f",
                           ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
      g_set_sensitivity(x, x);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      g_save_settings();
    }
  } else if (g_sens_mode == kSensMwr) {
    ImGui::SetNextItemWidth(mohpcf::ui::item_width());
    if (ImGui::SliderFloat(T("MWR sensitivity", "Чувствительность MWR"), &g_mwr_sens, 0.1F, 20.0F, "%.2f",
                           ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
      const float m = mwr_multiplier();
      g_set_sensitivity(m, m);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      save_sensitivity_settings();
    }
    mohpcf::ui::tip(T("Ctrl+click to type a value.", "Ctrl+клик — ввести число."));
  }
  const double deg = kMohUnitsPerSens * g_game_sens * (status.enabled != 0 ? status.sensitivity_x : 1.0F) * kUnitsToDeg;
  ImGui::TextDisabled("%s %.1f, %s %.4f° (= MWR %.2f)", T("Game slider", "Ползунок игры"), g_game_sens,
                      T("turn per count", "поворот на отсчёт"), deg, deg / kMwrYaw);
}

void draw_fov(const api::StatusV2& status) {
  ImGui::SeparatorText(T("Field of view", "Поле зрения (FOV)"));
  bool custom = status.fov_setting > 0.0F;
  if (ImGui::Checkbox(T("Change the field of view", "Изменить поле зрения"), &custom)) {
    g_set_fov(custom ? static_cast<float>(cod_to_moh(65.0)) : 0.0F);
    g_save_settings();
  }
  mohpcf::ui::help(T("The game uses 70° horizontal at 16:9 (as on consoles). Wider screens see more at the sides "
                     "(Hor+), whatever this setting. Applies at once. Turn per mouse count does not change with FOV.",
                     "Игра использует 70° по горизонтали при 16:9 (как на консолях). Широкие экраны видят больше по "
                     "бокам (Hor+) при любой настройке. Применяется сразу. Поворот на отсчёт мыши от FOV не зависит."));
  if (custom) {
    float fov = status.fov_setting;
    ImGui::SetNextItemWidth(mohpcf::ui::item_width());
    if (ImGui::SliderFloat(T("Horizontal at 16:9", "По горизонтали при 16:9"), &fov, 60.0F, 120.0F, "%.1f°",
                           ImGuiSliderFlags_AlwaysClamp)) {
      g_set_fov(fov);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      g_save_settings();
    }
    float cod = static_cast<float>(moh_to_cod(fov));
    ImGui::SetNextItemWidth(mohpcf::ui::item_width());
    if (ImGui::SliderFloat(T("Same as CoD4 / MWR cg_fov", "Как cg_fov в CoD4 / MWR"), &cod, 55.0F, 90.0F, "%.1f",
                           ImGuiSliderFlags_AlwaysClamp)) {
      g_set_fov(static_cast<float>(cod_to_moh(cod)));
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      g_save_settings();
    }
  }
  if (status.fov_applied > 0.0F) {
    ImGui::TextDisabled("%s %.1f° (%s %.2f)", T("On this screen:", "На этом экране:"), status.fov_applied,
                        T("aspect", "соотношение"), status.aspect);
  } else {
    ImGui::TextDisabled("%s %.1f° (%s %.2f)", T("On this screen:", "На этом экране:"), status.fov_target,
                        T("aspect", "соотношение"), status.aspect);
  }
}

// Aiming down sights: MWR's rule (tan of half the current FOV over tan of half the hip FOV) instead of
// the game's fixed x0.75, and MWR-sized aim zoom. See core/ads.h for the measurements.
void draw_aim(const api::StatusV2& status) {
  ImGui::SeparatorText(T("Aiming (right mouse button)", "Прицеливание (ПКМ)"));
  const char* sens_modes[] = {T("Game (x0.75)", "Как в игре (x0.75)"), T("As CoD4 / MWR", "Как в CoD4 / MWR"),
                              T("Fixed multiplier", "Свой множитель")};
  int mode = status.ads_mode;
  ImGui::SetNextItemWidth(mohpcf::ui::item_width());
  bool save = ImGui::Combo(T("Sensitivity##aim", "Чувствительность##aim"), &mode, sens_modes, 3, -1);
  mohpcf::ui::help(T("The game slows the mouse by x0.75 for every zoom. MWR slows it by tan(aim FOV / 2) / tan(FOV / 2), "
                     "following the zoom (measured in MWR), so every sight feels the same on screen.",
                     "Игра замедляет мышь в x0.75 при любом зуме. MWR — в tan(FOV прицела / 2) / tan(FOV / 2), следуя "
                     "за зумом (замерено в MWR), поэтому любой прицел ощущается одинаково на экране."));
  float custom = status.ads_custom;
  bool changed = save;
  if (mode == 2) {
    ImGui::SetNextItemWidth(mohpcf::ui::item_width());
    changed |= ImGui::SliderFloat(T("Multiplier while aiming", "Множитель в прицеле"), &custom, 0.1F, 1.5F, "%.3f",
                                  ImGuiSliderFlags_AlwaysClamp);
    save |= ImGui::IsItemDeactivatedAfterEdit();
  }
  if (changed) {
    g_set_ads(mode, custom);
  }
  if (save) {
    g_save_settings();
  }
  const char* zoom_modes[] = {T("Game", "Как в игре"), T("As CoD4 / MWR", "Как в CoD4 / MWR")};
  int fov_mode = status.ads_fov_mode;
  ImGui::SetNextItemWidth(mohpcf::ui::item_width());
  if (ImGui::Combo(T("Zoom", "Зум"), &fov_mode, zoom_modes, 2, -1)) {
    g_set_ads_fov(fov_mode);
    g_save_settings();
  }
  mohpcf::ui::help(T("MWR reads a weapon's aim FOV as CoD degrees (4:3): a rifle sight of 50 is 63.7° at 16:9. MoH shows "
                     "its red dot 50 as 50° at 16:9, a stronger zoom. \"As CoD4 / MWR\" reads it the MWR way.",
                     "MWR считает FOV прицела в градусах CoD (4:3): прицел винтовки 50 — это 63.7° при 16:9. MoH "
                     "показывает свой коллиматор 50 как 50° при 16:9 — зум сильнее. «Как в CoD4 / MWR» считает как MWR."));
  if (status.ads_found != 0 && status.ads_fov > 0.0F && status.ads_fov < status.ads_hip_fov - 0.75F) {
    ImGui::TextColored(kGood, "%s %.1f°, %s x%.3f", T("Aiming: FOV", "В прицеле: FOV"), status.ads_fov, T("turn", "поворот"),
                       status.ads_result);
  }
}

// ---- HUD and subtitles --------------------------------------------------------------------------

void scale_row(const char* label, float setting, float applied, mohpcf::api::SetSubtitleScaleFn set, const char* help) {
  ImGui::PushID(label);
  bool automatic = setting <= 0.0F;
  if (ImGui::Checkbox(T("Auto", "Авто"), &automatic)) {
    set(automatic ? 0.0F : applied);
    g_save_settings();
  }
  ImGui::SameLine(0.0F, -1.0F);
  ImGui::BeginDisabled(automatic);
  float scale = automatic ? applied : setting;
  ImGui::SetNextItemWidth(mohpcf::ui::item_width() * 0.7F);
  if (ImGui::SliderFloat(label, &scale, 1.0F, 4.0F, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
    set(scale);
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    g_save_settings();
  }
  ImGui::EndDisabled();
  mohpcf::ui::help(help);
  ImGui::PopID();
}

void draw_hud(const api::StatusV2& status) {
  ImGui::SeparatorText(T("HUD and subtitles", "Интерфейс и субтитры"));
  scale_row(T("Subtitles", "Субтитры"), status.subtitle_scale_setting, status.subtitle_scale, g_set_subtitle_scale,
            T("The game draws subtitles in 720p pixels (tiny at 4K). Auto: height / 720.",
              "Игра рисует субтитры в пикселях 720p (мелко в 4K). Авто: высота / 720."));
  scale_row(T("Markers and names", "Метки и имена"), status.marker_scale_setting, status.marker_scale, g_set_marker_scale,
            T("Objective markers and squadmate names (720p pixels in the game). Auto: height / 720.",
              "Метки заданий и имена напарников (в игре — пиксели 720p). Авто: высота / 720."));
  bool crosshair = status.crosshair_scaled != 0;
  if (ImGui::Checkbox(T("Scale the crosshair with the resolution", "Масштабировать прицел под разрешение"), &crosshair)) {
    g_set_crosshair_scale(crosshair);
    g_save_settings();
  }
  mohpcf::ui::help(T("The four-tick crosshair is drawn in 720p pixels; the red dot and shotgun crosshairs already scale.",
                     "Прицел из четырёх штрихов рисуется в пикселях 720p; коллиматор и прицел дробовика уже масштабируются."));
}

// ---- NVIDIA anti-aliasing -------------------------------------------------------------------------
// The game's own AA option does nothing in the campaign. MoH2010.LazmFix.NvProfile.exe sets MSAA + sparse grid
// supersampling in the NVIDIA profile of moh.exe (never the global profile); writing driver profiles
// needs administrator rights, so it runs through a UAC prompt.

std::string g_nv_log;
mohpcf::game_ini::FileWatch g_nv_log_watch;

bool nvidia_present() {
  static const bool present = [] {
    wchar_t dir[MAX_PATH]{};
    GetSystemDirectoryW(dir, MAX_PATH);  // SysWOW64 for this 32-bit process.
    return GetFileAttributesW((std::wstring(dir) + L"\\nvapi.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
  }();
  return present;
}

void run_nv_profile(const wchar_t* arguments) {
  const auto exe = addon_directory() / L"MoH2010.LazmFix.NvProfile.exe";
  const std::wstring dir = addon_directory().wstring();
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.lpVerb = L"runas";
  info.lpFile = exe.c_str();
  info.lpParameters = arguments;
  info.lpDirectory = dir.c_str();
  info.nShow = SW_HIDE;
  ShellExecuteExW(&info);
  g_nv_log_watch.reset();
}

void draw_nvidia_aa() {
  std::error_code ec;
  if (!nvidia_present() || !std::filesystem::exists(addon_directory() / L"MoH2010.LazmFix.NvProfile.exe", ec)) {
    return;
  }
  ImGui::SeparatorText(T("Anti-aliasing (NVIDIA)", "Сглаживание (NVIDIA)"));
  if (ImGui::Button("2x SGSSAA", ImVec2(0.0F, 0.0F))) {
    run_nv_profile(L"apply --sgssaa 2");
  }
  ImGui::SameLine(0.0F, -1.0F);
  if (ImGui::Button("4x SGSSAA", ImVec2(0.0F, 0.0F))) {
    run_nv_profile(L"apply --sgssaa 4");
  }
  ImGui::SameLine(0.0F, -1.0F);
  if (ImGui::Button(T("Undo", "Отменить"), ImVec2(0.0F, 0.0F))) {
    run_nv_profile(L"restore");
  }
  mohpcf::ui::help(T("The game's anti-aliasing option does nothing in the campaign. The driver can do it: MSAA plus "
                     "sparse grid supersampling, also on foliage and fences. 2x is light, 4x is sharper but heavy at 4K. "
                     "Only the driver profile \"Medal of Honor\" is changed (not the global one). Windows asks for "
                     "administrator rights; restart the game afterwards. Undo restores the profile as it was.",
                     "Сглаживание в настройках игры в кампании не работает. Его может сделать драйвер: MSAA плюс "
                     "суперсэмплинг (SGSSAA), в том числе на листве и заборах. 2x — лёгкое, 4x — чётче, но тяжело в 4K. "
                     "Меняется только профиль драйвера «Medal of Honor» (не глобальный). Windows запросит права "
                     "администратора; после этого перезапустите игру. «Отменить» возвращает профиль как был."));
  if (g_nv_log_watch.changed((addon_directory() / L"MoH2010.LazmFix.NvProfile.log").wstring(), ImGui::GetTime())) {
    // The summary line ("applied 2x MSAA + ...", "restored ...", or an error), not the setting list.
    std::ifstream in(addon_directory() / L"MoH2010.LazmFix.NvProfile.log");
    g_nv_log.clear();
    for (std::string line; std::getline(in, line);) {
      if (!line.empty() && line[0] != ' ' && line.rfind("profile:", 0) != 0) {
        g_nv_log = line;
      }
    }
    while (!g_nv_log.empty() && (g_nv_log.back() == '\r' || g_nv_log.back() == ':')) {
      g_nv_log.pop_back();
    }
  }
  if (!g_nv_log.empty()) {
    ImGui::TextDisabled("%s", g_nv_log.c_str());
  }
}

// ---- Info -----------------------------------------------------------------------------------------

void info_row(const char* label, const ImVec4* color, const char* text) {
  ImGui::TableNextRow(0, 0.0F);
  ImGui::TableSetColumnIndex(0);
  ImGui::TextUnformatted(label, nullptr);
  ImGui::TableSetColumnIndex(1);
  if (color != nullptr) {
    ImGui::TextColored(*color, "%s", text);
  } else {
    ImGui::TextUnformatted(text, nullptr);
  }
}

void draw_info(const api::StatusV2& status) {
  if (ImGui::BeginTable("##info", 2, ImGuiTableFlags_SizingFixedFit, ImVec2(0.0F, 0.0F), 0.0F)) {
    char text[300]{};
    std::snprintf(text, sizeof(text), "moh.exe %s", status.game_version);
    info_row(T("Game", "Игра"), nullptr, text);
    std::snprintf(text, sizeof(text), "%s beta", status.core_version);
    info_row(T("Fix", "Фикс"), &kGood, text);
    std::snprintf(text, sizeof(text), "%s, %d %s", status.hid_fix_enabled != 0 ? T("on", "вкл") : T("off", "выкл"),
                  status.hid_opens_bypassed, T("HID devices opened past SecuROM", "HID-устройств открыто в обход SecuROM"));
    info_row(T("Startup hang fix", "Фикс зависания"), status.hid_fix_enabled != 0 ? &kGood : &kWarn, text);
    info_row(T("Game mouse", "Мышь игры"), status.mouse_wrapped != 0 ? &kGood : &kWarn,
             status.mouse_wrapped != 0 ? T("DirectInput, through the fix", "DirectInput, через фикс")
                                       : T("not created yet", "ещё не создана"));
    info_row(T("Borderless window", "Окно без рамки"), nullptr, status.borderless_active != 0 ? T("on", "вкл") : T("off", "выкл"));
    std::snprintf(text, sizeof(text), "%d / %d / %d", status.subtitle_fields, status.marker_fields, status.crosshair_tables);
    info_row(T("Subtitle / marker / crosshair objects", "Объекты субтитров / меток / прицела"), nullptr, text);
    info_row(T("Aim zoom data", "Данные зума"), nullptr,
             status.ads_found != 0 ? T("found", "найдены") : T("found the first time you aim in a level", "находятся при первом прицеливании в уровне"));
    if (status.problem[0] != '\0') {
      info_row(T("Problem", "Проблема"), &kBad, status.problem);
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("%s", T("Log: Binaries\\MoH2010.LazmFix.log. Settings: Binaries\\MoH2010.LazmFix.ini.",
                              "Лог: Binaries\\MoH2010.LazmFix.log. Настройки: Binaries\\MoH2010.LazmFix.ini."));
}

// ---- Panel ----------------------------------------------------------------------------------------

void draw_settings(reshade::api::effect_runtime*) {
  load_language();
  ensure_core();
  if (g_get_status == nullptr) {
    ImGui::TextColored(kBad, "%s", g_core_error.c_str());
    return;
  }
  api::StatusV2 status{};
  if (!g_get_status(&status)) {
    return;
  }
  sync_sensitivity(status);

  // Language switch, top right.
  ImGui::TextDisabled("MoH 2010 LazmFix %s beta", status.core_version);
  ImGui::SameLine(0.0F, -1.0F);
  const float width = ImGui::CalcTextSize("English | Русский", nullptr, false, -1.0F).x + 24.0F;
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right));
  if (ImGui::RadioButton("English", !mohpcf::ui::g_ru)) {
    mohpcf::ui::g_ru = false;
    WritePrivateProfileStringW(L"UI", L"Language", L"en", fix_ini().c_str());
  }
  ImGui::SameLine(0.0F, -1.0F);
  if (ImGui::RadioButton("Русский", mohpcf::ui::g_ru)) {
    mohpcf::ui::g_ru = true;
    WritePrivateProfileStringW(L"UI", L"Language", L"ru", fix_ini().c_str());
  }
  mohpcf::ui::tip("If Russian shows as \"?\", set ReShade's own language to Russian (Settings tab).\n"
                  "Если русский текст показан как «?», выберите русский язык в настройках ReShade.");

  if (!ImGui::BeginTabBar("##tabs", 0)) {
    return;
  }
  if (ImGui::BeginTabItem(T("Gameplay", "Игра"), nullptr, 0)) {
    draw_presets(status);
    draw_sensitivity(status);
    draw_fov(status);
    draw_aim(status);
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem(T("Display", "Экран"), nullptr, 0)) {
    mohpcf::display::draw_display_tab();
    draw_hud(status);
    draw_nvidia_aa();
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem(T("Mouse", "Мышь"), nullptr, 0)) {
    mohpcf::display::draw_mouse_tab();
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem(T("Info", "Инфо"), nullptr, 0)) {
    draw_info(status);
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

// MWR sensitivity follows the game's slider also while the panel is closed.
void on_reshade_overlay(reshade::api::effect_runtime*) {
  ensure_core();
  static double next = 0.0;
  if (g_get_status == nullptr || ImGui::GetTime() < next) {
    return;
  }
  next = ImGui::GetTime() + 1.0;
  api::StatusV2 status{};
  if (g_get_status(&status)) {
    sync_sensitivity(status);
  }
}

bool on_open_overlay(reshade::api::effect_runtime*, bool open, reshade::api::input_source) {
  g_menu_open = open;
  // While the menu is open ReShade owns the mouse: the camera must not turn, clicks must not fire.
  if (g_set_overlay_open != nullptr) {
    g_set_overlay_open(open);
  }
  return false;
}

bool g_registered = false;

}  // namespace

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module) {
  g_addon_module = addon_module;
  if (!is_game_process()) {
    return false;
  }
  if (reshade_module == nullptr || !reshade::register_addon(addon_module, reshade_module)) {
    return false;
  }
  g_registered = true;
  ensure_core();
  mohpcf::display::initialize(addon_directory(), g_request_borderless);
  reshade::register_event<reshade::addon_event::create_swapchain>(mohpcf::display::on_create_swapchain);
  reshade::register_event<reshade::addon_event::init_swapchain>(mohpcf::display::on_init_swapchain);
  reshade::register_event<reshade::addon_event::reshade_overlay>(on_reshade_overlay);
  reshade::register_event<reshade::addon_event::reshade_open_overlay>(on_open_overlay);
  reshade::register_overlay(nullptr, draw_settings);
  return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module) {
  if (!g_registered) {
    return;
  }
  g_registered = false;
  if (g_set_overlay_open != nullptr) {
    g_set_overlay_open(false);
  }
  reshade::unregister_overlay(nullptr, draw_settings);
  reshade::unregister_event<reshade::addon_event::reshade_open_overlay>(on_open_overlay);
  reshade::unregister_event<reshade::addon_event::reshade_overlay>(on_reshade_overlay);
  reshade::unregister_event<reshade::addon_event::init_swapchain>(mohpcf::display::on_init_swapchain);
  reshade::unregister_event<reshade::addon_event::create_swapchain>(mohpcf::display::on_create_swapchain);
  reshade::unregister_addon(addon_module, reshade_module);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_addon_module = module;
    DisableThreadLibraryCalls(module);
  }
  return TRUE;
}
