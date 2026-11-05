#include "core/startup.h"

#include "common/file_util.h"
#include "common/game_ini.h"
#include "common/intros.h"
#include "core/window.h"

#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <filesystem>

namespace mohpcf::startup {
namespace {

std::vector<std::string> g_report;

void note(const char* fmt, ...) {
  char line[300]{};
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  g_report.emplace_back(line);
}

// Documents\EA Games\Medal of Honor\Config through the registry ("Personal" shell folder): the shell
// API is not safe under the loader lock.
std::wstring config_dir() {
  wchar_t raw[MAX_PATH]{};
  DWORD size = sizeof(raw);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders", L"Personal",
                   RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, raw, &size) != ERROR_SUCCESS) {
    return {};
  }
  wchar_t expanded[MAX_PATH]{};
  if (ExpandEnvironmentStringsW(raw, expanded, MAX_PATH) == 0) {
    return {};
  }
  return std::wstring(expanded) + L"\\EA Games\\Medal of Honor\\Config";
}

std::wstring read_setting(const std::wstring& ini, const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
  wchar_t buffer[64]{};
  GetPrivateProfileStringW(section, key, fallback, buffer, 64, ini.c_str());
  std::wstring value = buffer;
  for (auto& c : value) {
    c = static_cast<wchar_t>(std::towlower(c));
  }
  return value;
}

bool file_exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Physical pixels: the game may not be DPI aware, and GetSystemMetrics would be scaled down then.
DEVMODEW desktop_mode() {
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode);
  return mode;
}

// Without DPI awareness Windows shows a 3840x2160 game at 150 % scaling as a 2560x1440 window
// stretched back up (blurry). System DPI aware: window sizes are real pixels.
void dpi_aware() {
  using SetContextFn = BOOL(WINAPI*)(HANDLE);
  const auto set_context =
      reinterpret_cast<SetContextFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
  if (set_context == nullptr) {
    return;
  }
  if (set_context(reinterpret_cast<HANDLE>(-2) /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */)) {
    note("DPI awareness: system aware (real pixels at any Windows scaling)");
  } else {
    note("DPI awareness: left as it is (error %lu: already set by the game or a compatibility setting)", GetLastError());
  }
}

}  // namespace

bool apply_high_end(const std::wstring& engine_ini) {
  if (!file_exists(engine_ini)) {
    return false;
  }
  // The game's "High end" bucket (BaseEngine.ini + DefaultCompat.ini [AppCompatBucket3]), plus 16x
  // anisotropic filtering, 2048 shadow maps and distortion (off in the base config). CompatLevel 3
  // keeps the menu's "Restore defaults" on the high-end bucket instead of "Minspec".
  const wchar_t* on[] = {L"DynamicShadows", L"LightEnvironmentShadows", L"DepthOfField", L"UseHighQualityBloom",
                         L"Distortion", L"AmbientOcclusion", L"Bloom", L"MotionBlur", L"LensFlares", L"FogVolumes",
                         L"DynamicLights", L"StaticDecals", L"DynamicDecals", L"SpeedTreeLeaves", L"SpeedTreeFronds",
                         L"DirectionalLightmaps", L"bAllowFracturedDamage"};
  const wchar_t* off[] = {L"DropParticleDistortion", L"DOWNVFX", L"MinSpecCull"};
  struct Value {
    const wchar_t* key;
    const wchar_t* value;
  };
  const Value values[] = {{L"DecalCullDistanceScale", L"1.000000"}, {L"DetailMode", L"2"},  {L"SkeletalMeshLODBias", L"0"},
                          {L"ParticleLODBias", L"0"},                 {L"MaxAnisotropy", L"16"}, {L"MaxShadowResolution", L"2048"},
                          {L"CSMDistanceScale", L"2.000000"},         {L"CSMSizeScale", L"2.000000"}, {L"MaxMultisamples", L"1"},
                          {L"ScreenPercentage", L"100.000000"}};
  bool ok = true;
  for (const wchar_t* key : on) {
    ok = game_ini::write_bool(engine_ini, L"SystemSettings", key, true) && ok;
  }
  for (const wchar_t* key : off) {
    ok = game_ini::write_bool(engine_ini, L"SystemSettings", key, false) && ok;
  }
  for (const Value& v : values) {
    ok = game_ini::write(engine_ini, L"SystemSettings", v.key, v.value) && ok;
  }
  ok = game_ini::write(engine_ini, L"AppCompat", L"CompatLevelComposite", L"3") && ok;
  ok = game_ini::write(engine_ini, L"AppCompat", L"CompatLevelGPU", L"3") && ok;
  return ok;
}

void run(HMODULE self) {
  const std::wstring binaries = module_directory(self);
  const std::wstring game_dir = std::filesystem::path(binaries).parent_path().wstring();
  // Only in a real game folder (not in the tests, which load the proxy from the build folder).
  if (!file_exists(game_dir + L"\\MOHAGame")) {
    return;
  }
  const std::wstring ini = join_path(binaries, L"MoH2010.LazmFix.ini");
  const std::wstring config = config_dir();
  const std::wstring engine = config + L"\\MOHAEngine.ini";
  const std::wstring input = config + L"\\MOHAInput.ini";
  const bool have_engine = !config.empty() && file_exists(engine);

  if (GetPrivateProfileIntW(L"Display", L"DpiAware", 1, ini.c_str()) != 0) {
    dpi_aware();
  }
  const DEVMODEW desktop = desktop_mode();

  // Window mode and resolution.
  const std::wstring mode = read_setting(ini, L"Display", L"WindowMode", L"borderless");
  const std::wstring resolution = read_setting(ini, L"Display", L"Resolution", L"desktop");
  const bool borderless = mode == L"borderless";
  if (have_engine) {
    // Borderless: the game asks for fullscreen at the desktop resolution and the ReShade add-on turns
    // that into a window (no display mode change); the back buffer is then exactly ResX x ResY. In the
    // game's own windowed mode it sizes a framed window to ResX x ResY, which does not fit on the screen,
    // and renders smaller (3671x2065 at 3840x2160), stretched.
    if (mode == L"borderless" || mode == L"fullscreen") {
      game_ini::write_bool(engine, L"SystemSettings", L"Fullscreen", true);
    } else if (mode == L"windowed") {
      game_ini::write_bool(engine, L"SystemSettings", L"Fullscreen", false);
    }
    if (resolution == L"desktop" && desktop.dmPelsWidth >= 640 && desktop.dmPelsHeight >= 480) {
      game_ini::write_int(engine, L"SystemSettings", L"ResX", static_cast<int>(desktop.dmPelsWidth));
      game_ini::write_int(engine, L"SystemSettings", L"ResY", static_cast<int>(desktop.dmPelsHeight));
    }
    const int res_x = game_ini::read_int(engine, L"SystemSettings", L"ResX", 0);
    const int res_y = game_ini::read_int(engine, L"SystemSettings", L"ResY", 0);
    window::set_render_size(res_x, res_y);
    note("Display: %ls, resolution %ls -> game %dx%d, desktop %lux%lu at %lu Hz", mode.c_str(), resolution.c_str(), res_x, res_y,
         desktop.dmPelsWidth, desktop.dmPelsHeight, desktop.dmDisplayFrequency);
  } else {
    note("MOHAEngine.ini not found (%ls): the game creates it on its first start; display settings apply from the next start",
         engine.c_str());
  }
  window::install_creation_hook(borderless);

  // Intro videos.
  const bool skip = GetPrivateProfileIntW(L"Startup", L"SkipIntros", 1, ini.c_str()) != 0;
  if (skip != intros::skipped(game_dir)) {
    const bool ok = intros::set_skipped(game_dir, skip);
    note("Intro videos %s%s", skip ? "skipped" : "restored", ok ? "" : " (could not rename every file)");
  }

  // First start with the fix.
  if (GetPrivateProfileIntW(L"Startup", L"FirstStartDone", 0, ini.c_str()) == 0 && have_engine && file_exists(input)) {
    // UE3 mouse smoothing averages the counts over frames: off for a direct feel.
    game_ini::write_bool(input, L"Engine.PlayerInput", L"bEnableMouseSmoothing", false);
    // The game caps at 62 FPS: cap at the desktop refresh rate instead.
    if (desktop.dmDisplayFrequency > 62) {
      game_ini::write_bool(engine, L"Engine.GameEngine", L"bSmoothFrameRate", true);
      game_ini::write_int(engine, L"Engine.GameEngine", L"MaxSmoothedFrameRate", static_cast<int>(desktop.dmDisplayFrequency));
    }
    const bool high_end = GetPrivateProfileIntW(L"Graphics", L"HighEndOnFirstStart", 1, ini.c_str()) != 0 && apply_high_end(engine);
    WritePrivateProfileStringW(L"Startup", L"FirstStartDone", L"1", ini.c_str());
    note("First start: mouse smoothing off, frame rate cap %lu%s", desktop.dmDisplayFrequency,
         high_end ? ", graphics preset \"High end\"" : "");
  }
  if (GetPrivateProfileIntW(L"Graphics", L"ApplyHighEnd", 0, ini.c_str()) != 0 && have_engine) {
    apply_high_end(engine);
    WritePrivateProfileStringW(L"Graphics", L"ApplyHighEnd", L"0", ini.c_str());
    note("Graphics preset \"High end\" applied");
  }
}

std::vector<std::string> report() {
  std::vector<std::string> out = g_report;
  for (std::string& line : window::creation_report()) {
    out.push_back(std::move(line));
  }
  return out;
}

}  // namespace mohpcf::startup
