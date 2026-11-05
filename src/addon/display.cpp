// Display settings: window mode (borderless by default), resolution, and the game's own video settings
// (MOHAEngine.ini, the same keys as the game's Video menu) plus the frame rate limit, anisotropic
// filtering, the graphics preset and skipping the intro video. Game mouse options from MOHAInput.ini.
//
// The core applies the window mode and resolution at startup (core/startup.cpp); the panel writes the
// settings and turns an exclusive fullscreen request from the game menu into borderless here.

#include "addon/display.h"

#include "addon/ui.h"
#include "common/game_ini.h"
#include "common/intros.h"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mohpcf::display {
namespace {

using ui::T;

std::filesystem::path g_binaries;
std::wstring g_fix_ini;
RequestBorderlessFn g_request_borderless = nullptr;

// [Display] WindowMode in MoH2010.PCFix.ini.
enum WindowMode { kBorderless = 0, kFullscreen = 1, kWindowed = 2 };
int g_window_mode = kBorderless;
bool g_desktop_resolution = true;  // [Display] Resolution=desktop

// What the game asked for at the last swap chain creation, and what it got.
struct Live {
  bool known = false;
  bool game_fullscreen = false;   // The game asked for exclusive fullscreen.
  bool converted = false;         // ... and got a window instead.
  unsigned width = 0;
  unsigned height = 0;
  float refresh = 0.0F;           // Exclusive fullscreen refresh rate used (0 = default).
  HWND window = nullptr;
};
Live g_live;

// Game settings as last read from MOHAEngine.ini.
struct GameSettings {
  bool loaded = false;
  int res_x = 0;
  int res_y = 0;
  bool vsync = false;
  bool smooth_frame_rate = true;
  int max_smoothed = 62;
  int anisotropy = 1;
  bool operator==(const GameSettings&) const = default;
};
GameSettings g_game;
game_ini::FileWatch g_engine_watch;
GameSettings g_at_start;  // Values when the game started (what is running now).
bool g_at_start_known = false;
int g_mode_at_start = kBorderless;
bool g_desktop_at_start = true;

constexpr const wchar_t* kSystemSettings = L"SystemSettings";
constexpr const wchar_t* kGameEngine = L"Engine.GameEngine";

void read_game_settings() {
  const std::wstring ini = game_ini::engine_ini();
  GameSettings s;
  s.res_x = game_ini::read_int(ini, kSystemSettings, L"ResX", 0);
  s.res_y = game_ini::read_int(ini, kSystemSettings, L"ResY", 0);
  s.vsync = game_ini::read_bool(ini, kSystemSettings, L"UseVsync", false);
  s.smooth_frame_rate = game_ini::read_bool(ini, kGameEngine, L"bSmoothFrameRate", true);
  s.max_smoothed = game_ini::read_int(ini, kGameEngine, L"MaxSmoothedFrameRate", 62);
  s.anisotropy = game_ini::read_int(ini, kSystemSettings, L"MaxAnisotropy", 1);
  s.loaded = s.res_x > 0;
  g_game = s;
  if (!g_at_start_known && s.loaded) {
    g_at_start = s;
    g_at_start_known = true;
  }
}

void load_fix_settings() {
  wchar_t buffer[32]{};
  GetPrivateProfileStringW(L"Display", L"WindowMode", L"borderless", buffer, 32, g_fix_ini.c_str());
  const std::wstring mode = game_ini::lower(buffer);
  g_window_mode = mode == L"fullscreen" ? kFullscreen : mode == L"windowed" ? kWindowed : kBorderless;
  GetPrivateProfileStringW(L"Display", L"Resolution", L"desktop", buffer, 32, g_fix_ini.c_str());
  g_desktop_resolution = game_ini::lower(buffer) == L"desktop";
}

void save_fix_settings() {
  const wchar_t* modes[] = {L"borderless", L"fullscreen", L"windowed"};
  WritePrivateProfileStringW(L"Display", L"WindowMode", modes[g_window_mode], g_fix_ini.c_str());
  WritePrivateProfileStringW(L"Display", L"Resolution", g_desktop_resolution ? L"desktop" : L"game", g_fix_ini.c_str());
}

// ---- Monitor modes --------------------------------------------------------------------------------

std::wstring monitor_device(HWND window) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &info);
  return info.szDevice;
}

float best_refresh(HWND window, unsigned width, unsigned height) {
  const std::wstring device = monitor_device(window);
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  DWORD best = 0;
  for (DWORD i = 0; EnumDisplaySettingsW(device.c_str(), i, &mode); ++i) {
    if (mode.dmPelsWidth == width && mode.dmPelsHeight == height && mode.dmBitsPerPel == 32) {
      best = std::max(best, mode.dmDisplayFrequency);
    }
  }
  return static_cast<float>(best);
}

// Enumerating the modes asks the driver for each of them (hundreds on a 4K / 240 Hz monitor): doing it
// every frame took the game from 200 to 36 FPS while the panel was open. Cached, refreshed rarely.
struct ModeCache {
  DEVMODEW desktop{};
  double desktop_time = -10.0;
  std::vector<std::pair<int, int>> modes;
  double modes_time = -100.0;
};
ModeCache g_mode_cache;

const DEVMODEW& cached_desktop() {
  if (ImGui::GetTime() - g_mode_cache.desktop_time > 2.0) {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    EnumDisplaySettingsW(monitor_device(g_live.window).c_str(), ENUM_CURRENT_SETTINGS, &mode);
    g_mode_cache.desktop = mode;
    g_mode_cache.desktop_time = ImGui::GetTime();
  }
  return g_mode_cache.desktop;
}

const std::vector<std::pair<int, int>>& cached_resolutions() {
  if (ImGui::GetTime() - g_mode_cache.modes_time > 60.0) {
    std::set<std::pair<int, int>> unique;
    const std::wstring device = monitor_device(g_live.window);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    for (DWORD i = 0; EnumDisplaySettingsW(device.c_str(), i, &mode); ++i) {
      if (mode.dmBitsPerPel == 32 && mode.dmPelsWidth >= 1024 && mode.dmPelsHeight >= 600) {
        unique.emplace(static_cast<int>(mode.dmPelsWidth), static_cast<int>(mode.dmPelsHeight));
      }
    }
    g_mode_cache.modes.assign(unique.rbegin(), unique.rend());
    g_mode_cache.modes_time = ImGui::GetTime();
  }
  return g_mode_cache.modes;
}

// "16:9", "21:9" (2.33..2.40), "32:9", "16:10", "4:3", else "w:h" reduced.
std::string aspect_label(int w, int h) {
  const double a = static_cast<double>(w) / h;
  struct Named {
    double value;
    const char* name;
  };
  for (const Named& n : {Named{16.0 / 9.0, "16:9"}, Named{16.0 / 10.0, "16:10"}, Named{4.0 / 3.0, "4:3"}, Named{5.0 / 4.0, "5:4"},
                         Named{32.0 / 9.0, "32:9"}, Named{32.0 / 10.0, "32:10"}}) {
    if (std::abs(a - n.value) < 0.02) {
      return n.name;
    }
  }
  if (a > 2.3 && a < 2.42) {
    return "21:9";
  }
  const int g = std::gcd(w, h);
  return std::to_string(w / g) + ":" + std::to_string(h / g);
}

void resolution_label(char* out, size_t size, int w, int h) {
  std::snprintf(out, size, "%d x %d  (%s)", w, h, aspect_label(w, h).c_str());
}

const char* kFpsLabels[] = {"Unlimited", "60", "120", "144", "165", "240", "Custom"};
const int kFpsValues[] = {0, 60, 120, 144, 165, 240, -1};

// ---- Game mouse settings (MOHAInput.ini [MOHAGame.MOHAPlayerInput]) ---------------------------------

constexpr const wchar_t* kPlayerInput = L"MOHAGame.MOHAPlayerInput";
const wchar_t* const kAssistFields[] = {L"Friction",  L"FrictionAtMaxRotation", L"Deflection",
                                        L"DeflectionAtMaxRotation", L"Sticky", L"StickyAtMaxRotation"};

// Value of `name=` inside a struct line "(A=1, B=2, ...)", or empty.
std::wstring struct_field(const std::wstring& line, const wchar_t* name) {
  const std::wstring key = std::wstring(name) + L"=";
  for (size_t at = line.find(key); at != std::wstring::npos; at = line.find(key, at + 1)) {
    if (at > 0 && (line[at - 1] == L'(' || line[at - 1] == L' ' || line[at - 1] == L',')) {
      const size_t begin = at + key.size();
      const size_t end = line.find_first_of(L",)", begin);
      return line.substr(begin, end == std::wstring::npos ? std::wstring::npos : end - begin);
    }
  }
  return {};
}

std::wstring set_struct_field(std::wstring line, const wchar_t* name, const std::wstring& value) {
  const std::wstring key = std::wstring(name) + L"=";
  for (size_t at = line.find(key); at != std::wstring::npos; at = line.find(key, at + 1)) {
    if (at > 0 && (line[at - 1] == L'(' || line[at - 1] == L' ' || line[at - 1] == L',')) {
      const size_t begin = at + key.size();
      const size_t end = line.find_first_of(L",)", begin);
      return line.substr(0, begin) + value + (end == std::wstring::npos ? L"" : line.substr(end));
    }
  }
  return line;
}

std::wstring read_long(const std::wstring& file, const wchar_t* section, const wchar_t* key) {
  std::wstring buffer(2048, L'\0');
  const DWORD n = GetPrivateProfileStringW(section, key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());
  buffer.resize(n);
  return buffer;
}

bool aim_assist_forced_off(const std::wstring& ini) {
  for (int i = 0; i < 4; ++i) {
    const std::wstring key = L"AimAssistProfiles[" + std::to_wstring(i) + L"]";
    const std::wstring line = read_long(ini, kPlayerInput, key.c_str());
    if (line.empty()) {
      return false;
    }
    for (const wchar_t* field : kAssistFields) {
      const std::wstring value = struct_field(line, field);
      if (value.empty() || std::wcstod(value.c_str(), nullptr) != 0.0) {
        return false;
      }
    }
  }
  return true;
}

void set_aim_assist_forced_off(const std::wstring& ini, bool off) {
  const std::wstring defaults = (g_binaries.parent_path() / L"MOHAGame" / L"Config" / L"DefaultInput.ini").wstring();
  for (int i = 0; i < 4; ++i) {
    const std::wstring key = L"AimAssistProfiles[" + std::to_wstring(i) + L"]";
    std::wstring line = read_long(ini, kPlayerInput, key.c_str());
    const std::wstring original = read_long(defaults, kPlayerInput, key.c_str());
    if (line.empty()) {
      continue;
    }
    for (const wchar_t* field : kAssistFields) {
      const std::wstring restored = struct_field(original, field);
      line = set_struct_field(line, field, off ? L"0.0" : (restored.empty() ? L"0.0" : restored));
    }
    game_ini::write(ini, kPlayerInput, key.c_str(), line);
  }
}

game_ini::FileWatch g_input_watch;  // MOHAInput.ini
bool g_slow_curve = true;
bool g_assist_off = false;
bool g_smoothing = true;

}  // namespace

void draw_mouse_tab() {
  const std::wstring ini = game_ini::input_ini();
  if (g_input_watch.changed(ini, ImGui::GetTime())) {
    g_slow_curve = game_ini::read_bool(ini, kPlayerInput, L"bEnableSlowMouseReinterpret", true);
    g_smoothing = game_ini::read_bool(ini, L"Engine.PlayerInput", L"bEnableMouseSmoothing", true);
    g_assist_off = aim_assist_forced_off(ini);
  }
  ImGui::TextDisabled("%s", T("The game's own mouse options (MOHAInput.ini). Apply on the next start.",
                              "Настройки мыши самой игры (MOHAInput.ini). Применяются при следующем запуске."));
  bool smoothing_off = !g_smoothing;
  if (ImGui::Checkbox(T("Mouse smoothing off", "Без сглаживания мыши"), &smoothing_off)) {
    game_ini::write_bool(ini, L"Engine.PlayerInput", L"bEnableMouseSmoothing", !smoothing_off);
    g_input_watch.reset();
  }
  ui::help(T("bEnableMouseSmoothing: the game averages mouse movement over frames. Off: direct response. "
             "The fix turns it off on its first start.",
             "bEnableMouseSmoothing: игра усредняет движение мыши по кадрам. Выключено: прямой отклик. "
             "Фикс выключает его при первом запуске."));
  bool raw = !g_slow_curve;
  if (ImGui::Checkbox(T("1:1 mouse (no slow-movement curve)", "Мышь 1:1 (без кривой медленных движений)"), &raw)) {
    game_ini::write_bool(ini, kPlayerInput, L"bEnableSlowMouseReinterpret", !raw);
    g_input_watch.reset();
  }
  ui::help(T("bEnableSlowMouseReinterpret: the game turns 1..9 counts per frame into 0.25 .. 8.45. At high frame "
             "rates almost every frame is under 10 counts, so slow and medium movement is cut unevenly.",
             "bEnableSlowMouseReinterpret: игра превращает 1..9 отсчётов за кадр в 0.25 .. 8.45. При высоком FPS почти "
             "каждый кадр меньше 10 отсчётов, поэтому медленные и средние движения режутся неравномерно."));
  bool assist_off = g_assist_off;
  if (ImGui::Checkbox(T("Aim assist off for the mouse", "Без помощи в прицеливании для мыши"), &assist_off)) {
    set_aim_assist_forced_off(ini, assist_off);
    g_input_watch.reset();
  }
  ui::help(T("Zeroes AimAssistProfiles[0..3]: slowing down near targets and pulling toward them. Also turn off "
             "aim assist in the game's Gameplay menu.",
             "Обнуляет AimAssistProfiles[0..3]: замедление возле целей и притягивание к ним. Также выключите "
             "«Помощь в прицеливании» в меню игры (Игровой процесс)."));
}

void initialize(const std::filesystem::path& binaries, RequestBorderlessFn request_borderless) {
  g_binaries = binaries;
  g_fix_ini = (binaries / L"MoH2010.PCFix.ini").wstring();
  g_request_borderless = request_borderless;
  load_fix_settings();
  g_mode_at_start = g_window_mode;
  g_desktop_at_start = g_desktop_resolution;
  read_game_settings();
}

bool on_create_swapchain(reshade::api::device_api, reshade::api::swapchain_desc& desc, void* hwnd) {
  load_fix_settings();
  const auto window = static_cast<HWND>(hwnd);
  g_live.known = true;
  g_live.window = window;
  g_live.game_fullscreen = desc.fullscreen_state;
  g_live.width = desc.back_buffer.texture.width;
  g_live.height = static_cast<unsigned>(desc.back_buffer.texture.height);
  g_live.converted = false;
  g_live.refresh = desc.fullscreen_refresh_rate;
  char line[200]{};
  bool modified = false;
  if (desc.fullscreen_state && g_window_mode == kBorderless) {
    // Fullscreen chosen in the game menu: borderless instead (no display mode change).
    desc.fullscreen_state = false;
    desc.fullscreen_refresh_rate = 0.0F;
    g_live.converted = true;
    g_live.refresh = 0.0F;
    modified = true;
    std::snprintf(line, sizeof(line), "MoH 2010 PC Fix: exclusive fullscreen %ux%u -> borderless window", g_live.width, g_live.height);
  } else if (desc.fullscreen_state) {
    // Exclusive fullscreen: the monitor's highest refresh rate (D3D9 otherwise takes the default, often 60 Hz).
    const float hz = best_refresh(window, g_live.width, g_live.height);
    if (hz > desc.fullscreen_refresh_rate) {
      std::snprintf(line, sizeof(line), "MoH 2010 PC Fix: exclusive fullscreen %ux%u at %.0f Hz", g_live.width, g_live.height, hz);
      desc.fullscreen_refresh_rate = hz;
      g_live.refresh = hz;
      modified = true;
    }
  }
  if (line[0] != '\0') {
    reshade::log::message(reshade::log::level::info, line);
  }
  return modified;
}

void on_init_swapchain(reshade::api::swapchain* swapchain, bool) {
  if (g_request_borderless == nullptr || swapchain == nullptr) {
    return;
  }
  auto window = static_cast<HWND>(swapchain->get_hwnd());
  if (window == nullptr) {
    window = g_live.window;
  }
  g_request_borderless(window, g_window_mode == kBorderless);
}

void draw_display_tab() {
  if (g_engine_watch.changed(game_ini::engine_ini(), ImGui::GetTime())) {
    // The game's Video menu writes the same file when you press "Save": pick that up.
    read_game_settings();
  }
  const DEVMODEW& desktop = cached_desktop();
  const int desktop_w = static_cast<int>(desktop.dmPelsWidth);
  const int desktop_h = static_cast<int>(desktop.dmPelsHeight);
  const std::wstring ini = game_ini::engine_ini();

  ImGui::SeparatorText(T("Screen", "Экран"));
  const char* modes[] = {T("Borderless window (recommended)", "Окно без рамки (рекомендуется)"),
                         T("Exclusive fullscreen", "Эксклюзивный полный экран"), T("Window", "Окно")};
  ImGui::SetNextItemWidth(ui::item_width() * 1.3F);
  if (ImGui::Combo(T("Window mode", "Режим окна"), &g_window_mode, modes, 3, -1)) {
    save_fix_settings();
  }
  ui::help(T("Borderless: the game fills the monitor without a frame, at the desktop resolution and refresh rate. No "
             "display mode change at startup, instant Alt+Tab. Exclusive fullscreen uses the monitor's highest refresh "
             "rate. Applies on the next start.",
             "Без рамки: игра на весь монитор без рамки, с разрешением и частотой рабочего стола. Экран не "
             "переключается при запуске, Alt+Tab мгновенный. Эксклюзивный полный экран берёт максимальную частоту "
             "монитора. Применяется при следующем запуске."));

  // Resolution: the desktop's (default) or one of the monitor's modes.
  char current[64]{};
  if (g_desktop_resolution) {
    std::snprintf(current, sizeof(current), "%s (%d x %d)", T("Desktop", "Как у рабочего стола"), desktop_w, desktop_h);
  } else {
    resolution_label(current, sizeof(current), g_game.res_x, g_game.res_y);
  }
  ImGui::SetNextItemWidth(ui::item_width() * 1.3F);
  if (ImGui::BeginCombo(T("Resolution", "Разрешение"), current, 0)) {
    char label[64]{};
    std::snprintf(label, sizeof(label), "%s (%d x %d, %s)", T("Desktop", "Как у рабочего стола"), desktop_w, desktop_h,
                  aspect_label(desktop_w, desktop_h).c_str());
    if (ImGui::Selectable(label, g_desktop_resolution, 0, ImVec2(0.0F, 0.0F))) {
      g_desktop_resolution = true;
      save_fix_settings();
      if (g_game.loaded && desktop_w > 0) {
        game_ini::write_int(ini, kSystemSettings, L"ResX", desktop_w);
        game_ini::write_int(ini, kSystemSettings, L"ResY", desktop_h);
        read_game_settings();
      }
    }
    for (const auto& [w, h] : cached_resolutions()) {
      resolution_label(label, sizeof(label), w, h);
      const bool selected = !g_desktop_resolution && w == g_game.res_x && h == g_game.res_y;
      if (ImGui::Selectable(label, selected, 0, ImVec2(0.0F, 0.0F))) {
        g_desktop_resolution = false;
        save_fix_settings();
        game_ini::write_int(ini, kSystemSettings, L"ResX", w);
        game_ini::write_int(ini, kSystemSettings, L"ResY", h);
        read_game_settings();
      }
    }
    ImGui::EndCombo();
  }
  ui::help(T("Rendering resolution. \"Desktop\" follows the monitor (also after you change it in Windows): "
             "16:9, 16:10, 21:9 and 32:9 all work, the view is widened for wide screens (Hor+). The list shows "
             "your monitor's modes.",
             "Разрешение рендера. «Как у рабочего стола» следует за монитором (и после смены в Windows): "
             "16:9, 16:10, 21:9 и 32:9 работают, на широких экранах обзор расширяется по бокам (Hor+). В списке — "
             "режимы вашего монитора."));

  if (g_live.known) {
    if (g_live.game_fullscreen && !g_live.converted) {
      ImGui::TextColored(ui::kGood, "%s: %ux%u, %s, %.0f Hz", T("Now", "Сейчас"), g_live.width, g_live.height,
                         T("exclusive fullscreen", "эксклюзивный полный экран"), g_live.refresh);
    } else {
      ImGui::TextColored(ui::kGood, "%s: %ux%u, %s, %lu Hz", T("Now", "Сейчас"), g_live.width, g_live.height,
                         g_window_mode == kBorderless ? T("borderless", "без рамки") : T("window", "окно"),
                         desktop.dmDisplayFrequency);
    }
  }

  if (!g_game.loaded) {
    ImGui::TextColored(ui::kWarn, "%s",
                       T("MOHAEngine.ini not found (Documents\\EA Games\\Medal of Honor\\Config).",
                         "MOHAEngine.ini не найден (Документы\\EA Games\\Medal of Honor\\Config)."));
    return;
  }

  // Frame rate limit (UE3 frame rate smoothing: caps every frame, menus included).
  int fps_index = 6;
  if (!g_game.smooth_frame_rate) {
    fps_index = 0;
  } else {
    for (int i = 1; i < 6; ++i) {
      if (kFpsValues[i] == g_game.max_smoothed) {
        fps_index = i;
      }
    }
  }
  const char* fps_labels[7];
  std::copy(std::begin(kFpsLabels), std::end(kFpsLabels), fps_labels);
  fps_labels[0] = T("Unlimited", "Без ограничения");
  fps_labels[6] = T("Custom", "Своё");
  ImGui::SetNextItemWidth(ui::item_width());
  if (ImGui::Combo(T("Frame rate limit", "Ограничение FPS"), &fps_index, fps_labels, 7, -1)) {
    if (kFpsValues[fps_index] == 0) {
      game_ini::write_bool(ini, kGameEngine, L"bSmoothFrameRate", false);
    } else {
      game_ini::write_bool(ini, kGameEngine, L"bSmoothFrameRate", true);
      if (kFpsValues[fps_index] > 0) {
        game_ini::write_int(ini, kGameEngine, L"MaxSmoothedFrameRate", kFpsValues[fps_index]);
      }
    }
    read_game_settings();
  }
  ui::help(T("The game caps at 62 by default; the fix sets the desktop refresh rate on its first start.",
             "Игра по умолчанию ограничивает 62 FPS; фикс при первом запуске ставит частоту рабочего стола."));
  if (fps_index == 6) {
    int custom = g_game.max_smoothed;
    ImGui::SetNextItemWidth(ui::item_width());
    if (ImGui::InputInt("FPS##custom", &custom, 1, 10, 0) && custom >= 20 && custom <= 1000) {
      game_ini::write_int(ini, kGameEngine, L"MaxSmoothedFrameRate", custom);
      read_game_settings();
    }
  }
  bool vsync = g_game.vsync;
  if (ImGui::Checkbox(T("Vertical sync", "Вертикальная синхронизация"), &vsync)) {
    game_ini::write_bool(ini, kSystemSettings, L"UseVsync", vsync);
    read_game_settings();
  }
  const char* aniso_labels[] = {"1x", "2x", "4x", "8x", "16x"};
  const int aniso_values[] = {1, 2, 4, 8, 16};
  int aniso_index = 0;
  for (int i = 0; i < 5; ++i) {
    if (aniso_values[i] <= g_game.anisotropy) {
      aniso_index = i;
    }
  }
  ImGui::SetNextItemWidth(ui::item_width());
  if (ImGui::Combo(T("Anisotropic filtering", "Анизотропная фильтрация"), &aniso_index, aniso_labels, 5, -1)) {
    game_ini::write_int(ini, kSystemSettings, L"MaxAnisotropy", aniso_values[aniso_index]);
    read_game_settings();
  }
  ui::help(T("Not in the game's menu; its presets leave it at 1x.", "В меню игры его нет; её пресеты оставляют 1x."));

  static bool high_end_requested = false;
  if (ImGui::Button(T("Highest graphics preset", "Максимальный пресет графики"), ImVec2(0.0F, 0.0F))) {
    WritePrivateProfileStringW(L"Graphics", L"ApplyHighEnd", L"1", g_fix_ini.c_str());
    high_end_requested = true;
  }
  ui::help(T("The game's hardware detection files current graphics cards under \"Minspec\" (no dynamic shadows, no "
             "depth of field, 1x filtering). This sets the game's own \"High end\" preset plus 16x filtering and "
             "sharper shadows, on the next start. The fix also does it once on its first start.",
             "Определение железа игры записывает современные видеокарты в «Minspec» (без динамических теней, без "
             "глубины резкости, фильтрация 1x). Кнопка ставит пресет игры «High end» плюс фильтрацию 16x и чёткие "
             "тени, при следующем запуске. Фикс делает это один раз и при первом запуске."));
  if (high_end_requested) {
    ImGui::SameLine(0.0F, -1.0F);
    ImGui::TextColored(ui::kGood, "%s", T("on the next start", "при следующем запуске"));
  }

  if ((g_at_start_known && !(g_game == g_at_start)) || g_window_mode != g_mode_at_start ||
      g_desktop_resolution != g_desktop_at_start) {
    ImGui::TextColored(ui::kWarn, "%s", T("Changed: restart the game to apply.", "Изменено: перезапустите игру."));
  }

  ImGui::SeparatorText(T("Startup", "Запуск"));
  static bool skip = true;
  static bool english = true;
  static double startup_read = -10.0;
  const std::wstring game_dir = g_binaries.parent_path().wstring();
  if (ImGui::GetTime() - startup_read > 2.0) {
    skip = GetPrivateProfileIntW(L"Startup", L"SkipIntros", 1, g_fix_ini.c_str()) != 0;
    english = GetPrivateProfileIntW(L"Startup", L"EnglishKeyboard", 1, g_fix_ini.c_str()) != 0;
    startup_read = ImGui::GetTime();
  }
  if (ImGui::Checkbox(T("Skip intro videos", "Пропускать заставки"), &skip)) {
    WritePrivateProfileStringW(L"Startup", L"SkipIntros", skip ? L"1" : L"0", g_fix_ini.c_str());
    intros::set_skipped(game_dir, skip);
  }
  ui::help(T("EA / Danger Close logos and the legal screen: straight to the menu. Renames "
             "MOHAGame\\CookedMovies\\MOHA_Legal_ID_Only*.vp6 to *.vp6.skip (off renames them back).",
             "Логотипы EA / Danger Close и юридический экран: сразу в меню. Переименовывает "
             "MOHAGame\\CookedMovies\\MOHA_Legal_ID_Only*.vp6 в *.vp6.skip (выключение возвращает)."));
  if (ImGui::Checkbox(T("English keyboard layout in the game", "Английская раскладка в игре"), &english)) {
    WritePrivateProfileStringW(L"Startup", L"EnglishKeyboard", english ? L"1" : L"0", g_fix_ini.c_str());
  }
  ui::help(T("The game names keys in its hints with the active layout (\"ЦФЫВ\" instead of \"WASD\" with a Russian "
             "layout). Switches the game to English (US) at startup; controls work either way.",
             "Игра пишет клавиши в подсказках в активной раскладке («ЦФЫВ» вместо «WASD»). Переключает игру на "
             "английскую (US) при запуске; управление работает в любой раскладке."));
}

}  // namespace mohpcf::display
