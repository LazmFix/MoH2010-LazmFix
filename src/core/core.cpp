#include "core/core.h"

#include "common/file_util.h"
#include "common/logger.h"
#include "common/mouse_math.h"
#include "core/ads.h"
#include "core/fov.h"
#include "core/hid_guard.h"
#include "core/hud.h"
#include "core/startup.h"
#include "core/subtitles.h"
#include "core/window.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mohpcf::core {
namespace {

std::once_flag g_start_once;
bool g_started = false;
HMODULE g_real = nullptr;
Logger g_log;
std::wstring g_ini_path;
bool g_is_game = false;
char g_game_version[32] = "unknown";

// Settings.
std::atomic<bool> g_enabled{true};
std::atomic<float> g_sens_x{1.0F};
std::atomic<float> g_sens_y{1.0F};
std::atomic<bool> g_overlay_open{false};

// Game mouse.
std::atomic<bool> g_mouse_created{false};
DWORD g_x_offset = DIMOFS_X;
DWORD g_y_offset = DIMOFS_Y;
double g_acc_x = 0.0;
double g_acc_y = 0.0;

std::mutex g_problem_mutex;
std::string g_problem;

void set_problem(const std::string& text) {
  std::lock_guard lock(g_problem_mutex);
  if (g_problem.empty()) {
    g_problem = text;
  }
  g_log.warn(text);
}

std::string narrow(const std::wstring& text) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(size > 0 ? size - 1 : 0, '\0');
  if (size > 1) {
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
  }
  return out;
}

std::string format(const char* fmt, ...) {
  char buffer[512]{};
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  return buffer;
}

float clamp_sens(float value) {
  if (!std::isfinite(value)) {
    return 1.0F;
  }
  return std::clamp(value, api::kMinSensitivity, api::kMaxSensitivity);
}

float read_float(const wchar_t* section, const wchar_t* key, float fallback) {
  wchar_t buffer[64]{};
  GetPrivateProfileStringW(section, key, L"", buffer, 64, g_ini_path.c_str());
  if (buffer[0] == L'\0') {
    return fallback;
  }
  wchar_t* end = nullptr;
  const double value = std::wcstod(buffer, &end);
  return end == buffer ? fallback : static_cast<float>(value);
}

std::wstring read_string(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
  wchar_t buffer[64]{};
  GetPrivateProfileStringW(section, key, fallback, buffer, 64, g_ini_path.c_str());
  return buffer;
}

void read_game_version() {
  const std::wstring exe = process_exe_path();
  DWORD ignored = 0;
  const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
  if (size == 0) {
    return;
  }
  std::string data(size, '\0');
  if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data())) {
    return;
  }
  VS_FIXEDFILEINFO* info = nullptr;
  UINT length = 0;
  if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) && info != nullptr) {
    std::snprintf(g_game_version, sizeof(g_game_version), "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS),
                  LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
  }
}

void start_once() {
  const std::wstring dir = module_directory(&__ImageBase);
  g_ini_path = join_path(dir, L"MoH2010.LazmFix.ini");
  g_log.open(join_path(dir, L"MoH2010.LazmFix.log"));

  std::wstring exe_name = std::filesystem::path(process_exe_path()).filename().wstring();
  for (auto& c : exe_name) {
    c = static_cast<wchar_t>(std::towlower(c));
  }
  g_is_game = exe_name == api::kGameExeName;
  read_game_version();
  g_enabled = GetPrivateProfileIntW(L"Mouse", L"Enabled", 1, g_ini_path.c_str()) != 0;
  g_sens_x = clamp_sens(read_float(L"Mouse", L"SensitivityX", 1.0F));
  g_sens_y = clamp_sens(read_float(L"Mouse", L"SensitivityY", 1.0F));
  g_log.info(format("MoH 2010 LazmFix %s in %s (version %s)", MOHPCF_VERSION, narrow(exe_name).c_str(), g_game_version));
  for (const std::string& line : startup::report()) {
    g_log.info(line);
  }

  wchar_t system_dir[MAX_PATH]{};
  GetSystemDirectoryW(system_dir, MAX_PATH);
  const std::wstring real_path = join_path(system_dir, L"dinput8.dll");
  g_real = LoadLibraryW(real_path.c_str());
  if (g_real == nullptr) {
    set_problem("Could not load the system dinput8.dll: " + narrow(real_path));
    return;
  }

  const bool hid_fix = GetPrivateProfileIntW(L"Startup", L"HidFix", 1, g_ini_path.c_str()) != 0;
  HidGuard::initialize(g_real, &g_log, hid_fix);
  g_log.info(std::string("Startup hang fix (HID opens past SecuROM): ") + (hid_fix ? "on" : "off ([Startup] HidFix=0)"));
  g_log.info(format("Mouse: multiplier %s, X %.3f Y %.3f", g_enabled ? "on" : "off", g_sens_x.load(), g_sens_y.load()));
  if (!g_is_game) {
    g_log.info("Not moh.exe: DirectInput is passed through unchanged");
  } else {
    subtitles::start(read_float(L"Display", L"SubtitleScale", 0.0F));
    fov::start(read_float(L"Display", L"FOV", 0.0F));
    hud::start(read_float(L"Display", L"MarkerScale", 0.0F),
               GetPrivateProfileIntW(L"Display", L"CrosshairScale", 1, g_ini_path.c_str()) != 0);
    const std::wstring mode = read_string(L"Mouse", L"AdsMode", L"game");
    const std::wstring aim_fov = read_string(L"Mouse", L"AdsFov", L"game");
    ads::start(mode == L"mwr" ? ads::kMwr : mode == L"custom" ? ads::kCustom : ads::kGame,
               read_float(L"Mouse", L"AdsMultiplier", 0.75F), aim_fov == L"mwr" ? ads::kMwr : ads::kGame);
    g_log.info(format("Aiming: sensitivity %s, aim FOV %s", narrow(mode).c_str(), narrow(aim_fov).c_str()));
  }
  g_started = true;
}

// The game writes key names in its hints with the active keyboard layout ("Используйте цфыв для
// перемещения"). Switching the game to the US layout once, on the game thread, before its menus build
// those hints, gives "WASD". Controls are not affected: DirectInput reads scan codes.
void english_keyboard(HWND window) {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  if (GetPrivateProfileIntW(L"Startup", L"EnglishKeyboard", 1, g_ini_path.c_str()) == 0) {
    return;
  }
  HKL us = LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE | KLF_SETFORPROCESS);
  if (us == nullptr) {
    g_log.warn("Could not switch the keyboard layout to English (US)");
    return;
  }
  ActivateKeyboardLayout(us, KLF_SETFORPROCESS);
  if (window != nullptr) {
    PostMessageW(window, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(us));
  }
  g_log.info("Keyboard layout switched to English (US) for the game ([Startup] EnglishKeyboard=0 turns this off)");
}

}  // namespace

bool ensure_started() {
  std::call_once(g_start_once, start_once);
  return g_started;
}

HMODULE real_dinput8() {
  return g_real;
}

Logger& log() {
  return g_log;
}

void mouse_note_created() {
  if (!g_mouse_created.exchange(true)) {
    g_log.info("The game created its system mouse device: it goes through the fix");
  }
}

void mouse_set_data_format(LPCDIDATAFORMAT data_format) {
  if (data_format == nullptr) {
    return;
  }
  DWORD x = DIMOFS_X;
  DWORD y = DIMOFS_Y;
  for (DWORD i = 0; i < data_format->dwNumObjs && data_format->rgodf != nullptr; ++i) {
    const DIOBJECTDATAFORMAT& object = data_format->rgodf[i];
    if (object.pguid != nullptr && *object.pguid == GUID_XAxis) {
      x = object.dwOfs;
    } else if (object.pguid != nullptr && *object.pguid == GUID_YAxis) {
      y = object.dwOfs;
    }
  }
  g_x_offset = x;
  g_y_offset = y;
}

void mouse_set_cooperative_level(HWND window, DWORD flags) {
  // The game sets it again every frame while it cannot acquire the mouse (window in the background):
  // log changes only.
  static HWND logged_window = nullptr;
  if (window != logged_window) {
    logged_window = window;
    g_log.info(format("Game mouse: cooperative level 0x%lX, window 0x%p", flags, static_cast<void*>(window)));
  }
  if (g_is_game) {
    window::note_game_window(window);
    english_keyboard(window);
  }
}

void mouse_set_acquired(bool) {}

void mouse_process_state(DWORD size, void* data) {
  if (data == nullptr || size < std::max(g_x_offset, g_y_offset) + sizeof(LONG)) {
    return;
  }
  if (g_overlay_open.load()) {
    // ReShade menu open: the camera stays put and clicks do not fire.
    std::memset(data, 0, size);
    return;
  }
  const float aim = ads::update();
  const bool enabled = g_enabled.load();
  const float sens_x = (enabled ? g_sens_x.load() : 1.0F) * aim;
  const float sens_y = (enabled ? g_sens_y.load() : 1.0F) * aim;
  if (sens_x == 1.0F && sens_y == 1.0F) {
    return;
  }
  auto* bytes = static_cast<std::uint8_t*>(data);
  LONG x = 0;
  LONG y = 0;
  std::memcpy(&x, bytes + g_x_offset, sizeof(LONG));
  std::memcpy(&y, bytes + g_y_offset, sizeof(LONG));
  x = scale_axis(x, sens_x, g_acc_x);
  y = scale_axis(y, sens_y, g_acc_y);
  std::memcpy(bytes + g_x_offset, &x, sizeof(LONG));
  std::memcpy(bytes + g_y_offset, &y, sizeof(LONG));
}

void mouse_process_data(DWORD object_size, DIDEVICEOBJECTDATA* data, DWORD count, HRESULT result) {
  if (data == nullptr || FAILED(result) || object_size < sizeof(DWORD) * 4) {
    return;
  }
  const bool overlay = g_overlay_open.load();
  // While aiming: the MWR rule or a fixed multiplier instead of the game's x0.75 (1 otherwise; ads.h).
  const float aim = overlay ? 1.0F : ads::update();  // Also applies the aim FOV setting.
  const bool enabled = g_enabled.load();
  const float sens_x = (enabled ? g_sens_x.load() : 1.0F) * aim;
  const float sens_y = (enabled ? g_sens_y.load() : 1.0F) * aim;
  if (!overlay && sens_x == 1.0F && sens_y == 1.0F) {
    return;
  }
  auto* bytes = reinterpret_cast<std::uint8_t*>(data);
  for (DWORD i = 0; i < count; ++i) {
    auto* item = reinterpret_cast<DIDEVICEOBJECTDATA*>(bytes + static_cast<std::size_t>(i) * object_size);
    if (overlay) {
      // Axes: no movement. Buttons: released, so nothing fires while the menu has the mouse.
      item->dwData = 0;
      continue;
    }
    const bool is_x = item->dwOfs == g_x_offset;
    if (!is_x && item->dwOfs != g_y_offset) {
      continue;
    }
    const auto value = static_cast<LONG>(item->dwData);
    item->dwData = static_cast<DWORD>(is_x ? scale_axis(value, sens_x, g_acc_x) : scale_axis(value, sens_y, g_acc_y));
  }
}

bool get_status(api::StatusV2* status) {
  if (status == nullptr || status->size != sizeof(api::StatusV2)) {
    return false;
  }
  api::StatusV2 out{};
  std::snprintf(out.core_version, sizeof(out.core_version), "%s", MOHPCF_VERSION);
  std::snprintf(out.game_version, sizeof(out.game_version), "%s", g_game_version);
  out.hid_fix_enabled = HidGuard::enabled() ? 1 : 0;
  out.securom_hook_seen = HidGuard::foreign_hook_seen() ? 1 : 0;
  out.hid_opens_bypassed = HidGuard::opens_bypassed();
  out.mouse_wrapped = g_mouse_created.load() ? 1 : 0;
  out.enabled = g_enabled.load() ? 1 : 0;
  out.sensitivity_x = g_sens_x.load();
  out.sensitivity_y = g_sens_y.load();
  out.borderless_active = window::borderless_active() ? 1 : 0;
  out.aspect = window::aspect();
  out.subtitle_fields = subtitles::fields_found();
  out.subtitle_scale_setting = subtitles::configured_scale();
  out.subtitle_scale = subtitles::applied_scale();
  out.fov_setting = fov::configured();
  out.fov_target = fov::target();
  out.fov_applied = fov::applied();
  out.fov_objects = fov::objects();
  out.crosshair_scaled = hud::crosshair_setting() ? 1 : 0;
  out.crosshair_tables = hud::cursor_tables();
  out.marker_scale_setting = hud::marker_scale_setting();
  out.marker_scale = hud::marker_scale();
  out.marker_fields = hud::marker_fields();
  out.ads_mode = ads::mode();
  out.ads_custom = ads::custom();
  out.ads_fov_mode = ads::fov_mode();
  const ads::State aim = ads::state();
  out.ads_found = aim.found ? 1 : 0;
  out.ads_hip_fov = aim.hip_fov;
  out.ads_fov = aim.fov;
  out.ads_game_mult = aim.game_mult;
  out.ads_result = aim.result;
  out.ads_aim_fov_game = aim.aim_fov_game;
  out.ads_aim_fov = aim.aim_fov;
  {
    std::lock_guard lock(g_problem_mutex);
    std::snprintf(out.problem, sizeof(out.problem), "%s", g_problem.c_str());
  }
  *status = out;
  return true;
}

void set_enabled(bool enabled) {
  if (g_enabled.exchange(enabled) != enabled) {
    g_acc_x = 0.0;
    g_acc_y = 0.0;
  }
}

void set_sensitivity(float x, float y) {
  g_sens_x = clamp_sens(x);
  g_sens_y = clamp_sens(y);
}

void set_overlay_open(bool open) {
  g_overlay_open = open;
}

bool save_settings() {
  if (g_ini_path.empty()) {
    return false;
  }
  const wchar_t* ini = g_ini_path.c_str();
  wchar_t buffer[64]{};
  bool ok = WritePrivateProfileStringW(L"Mouse", L"Enabled", g_enabled.load() ? L"1" : L"0", ini) != FALSE;
  std::swprintf(buffer, 64, L"%.4f", g_sens_x.load());
  ok = WritePrivateProfileStringW(L"Mouse", L"SensitivityX", buffer, ini) != FALSE && ok;
  std::swprintf(buffer, 64, L"%.4f", g_sens_y.load());
  ok = WritePrivateProfileStringW(L"Mouse", L"SensitivityY", buffer, ini) != FALSE && ok;
  const std::int32_t aim_mode = ads::mode();
  ok = WritePrivateProfileStringW(L"Mouse", L"AdsMode",
                                  aim_mode == ads::kMwr ? L"mwr" : aim_mode == ads::kCustom ? L"custom" : L"game", ini) != FALSE &&
       ok;
  ok = WritePrivateProfileStringW(L"Mouse", L"AdsFov", ads::fov_mode() == ads::kMwr ? L"mwr" : L"game", ini) != FALSE && ok;
  std::swprintf(buffer, 64, L"%.3f", ads::custom());
  ok = WritePrivateProfileStringW(L"Mouse", L"AdsMultiplier", buffer, ini) != FALSE && ok;
  std::swprintf(buffer, 64, L"%.1f", fov::configured());
  ok = WritePrivateProfileStringW(L"Display", L"FOV", buffer, ini) != FALSE && ok;
  std::swprintf(buffer, 64, L"%.2f", subtitles::configured_scale());
  ok = WritePrivateProfileStringW(L"Display", L"SubtitleScale", buffer, ini) != FALSE && ok;
  std::swprintf(buffer, 64, L"%.2f", hud::marker_scale_setting());
  ok = WritePrivateProfileStringW(L"Display", L"MarkerScale", buffer, ini) != FALSE && ok;
  ok = WritePrivateProfileStringW(L"Display", L"CrosshairScale", hud::crosshair_setting() ? L"1" : L"0", ini) != FALSE && ok;
  return ok;
}

bool is_game_process() {
  return g_is_game;
}

}  // namespace mohpcf::core
