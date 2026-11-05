#pragma once

#include <cstdint>

// C API exported by the core (the dinput8.dll proxy in Binaries\) for the ReShade add-on, which
// shows status and changes settings without linking against the core. The add-on and the core ship
// together: the struct carries its size and the add-on refuses a core of another size.

namespace mohpcf::api {

constexpr const wchar_t* kCoreFileName = L"dinput8.dll";  // Next to moh.exe and the add-on.
constexpr const wchar_t* kGameExeName = L"moh.exe";

struct StatusV2 {
  std::uint32_t size = sizeof(StatusV2);
  char core_version[32] = {};
  char game_version[32] = {};
  // Startup hang fix (SecuROM reads from HID devices while DirectInput lists them).
  std::int32_t hid_fix_enabled = 0;
  std::int32_t securom_hook_seen = 0;    // DirectInput's CreateFileW went through a foreign hook.
  std::int32_t hid_opens_bypassed = 0;   // HID device opens sent past that hook.
  // Mouse.
  std::int32_t mouse_wrapped = 0;        // The game created a system mouse device; it goes through the core.
  std::int32_t enabled = 0;              // Setting: apply the sensitivity multipliers.
  float sensitivity_x = 1.0F;
  float sensitivity_y = 1.0F;
  char problem[256] = {};                // First problem found, empty when everything is fine.
  // Window.
  std::int32_t borderless_active = 0;
  float aspect = 0.0F;                   // Game window (or monitor) width / height.
  // Subtitle size (engine subtitle font sizes, made for 720p).
  std::int32_t subtitle_fields = 0;      // Engine fields found in memory (2 = class default + live object).
  float subtitle_scale_setting = 0.0F;   // 0 = automatic (render height / 720).
  float subtitle_scale = 1.0F;           // Scale in force.
  // Field of view (player camera DefaultFOV, horizontal degrees; the game uses 70 at 16:9).
  float fov_setting = 0.0F;              // Horizontal degrees at 16:9, 0 = game default.
  float fov_target = 0.0F;               // What is written for this aspect ratio (Hor+).
  float fov_applied = 0.0F;              // DefaultFOV of the live camera, 0 before a level is loaded.
  std::int32_t fov_objects = 0;          // Camera objects kept at the setting (class default + live).
  // Native HUD made in 720p pixels.
  std::int32_t crosshair_scaled = 0;     // Setting: crosshair ticks scale with the resolution.
  std::int32_t crosshair_tables = 0;     // Crosshair tables found in memory.
  float marker_scale_setting = 0.0F;     // Objective marker text: 0 = automatic (height / 720).
  float marker_scale = 1.0F;             // In force.
  std::int32_t marker_fields = 0;        // Marker text objects found.
  // Aiming down sights (see core/ads.h).
  std::int32_t ads_mode = 0;             // 0 = the game's (x0.75), 1 = as MWR / CoD4, 2 = fixed multiplier.
  float ads_custom = 0.75F;              // Multiplier for mode 2.
  std::int32_t ads_found = 0;            // The game's zoom block found (in a level).
  float ads_hip_fov = 0.0F;              // Last values seen while reading the mouse.
  float ads_fov = 0.0F;                  // Current FOV (lower while aiming).
  float ads_game_mult = 1.0F;            // The game's own multiplier in force (0.75 while aiming).
  float ads_result = 1.0F;               // Turn per count now relative to the hip.
  std::int32_t ads_fov_mode = 0;         // Aim FOV: 0 = the weapon's, 1 = as MWR (its number in CoD degrees).
  float ads_aim_fov_game = 0.0F;         // The weapon's aim FOV, last seen while aiming.
  float ads_aim_fov = 0.0F;              // Aim FOV in force, last seen while aiming.
};

using GetStatusFn = bool (*)(StatusV2* status);
using SetEnabledFn = void (*)(bool enabled);
using SetSensitivityFn = void (*)(float x, float y);
using SetOverlayOpenFn = void (*)(bool open);
using SaveSettingsFn = bool (*)();
using SetSubtitleScaleFn = void (*)(float scale);  // 0 = automatic
using SetFovFn = void (*)(float fov);  // 0 = game default (70)
using SetMarkerScaleFn = void (*)(float scale);  // 0 = automatic
using SetCrosshairScaleFn = void (*)(bool on);
using SetAdsFn = void (*)(std::int32_t mode, float custom);
using SetAdsFovFn = void (*)(std::int32_t fov_mode);

constexpr float kMinSensitivity = 0.05F;
constexpr float kMaxSensitivity = 20.0F;

}  // namespace mohpcf::api

// Exported names: MohPcfGetStatus, MohPcfSetEnabled, MohPcfSetSensitivity, MohPcfSetOverlayOpen,
// MohPcfSaveSettings, MohPcfSetSubtitleScale, MohPcfSetFov, MohPcfSetMarkerScale,
// MohPcfSetCrosshairScale, MohPcfSetAds, MohPcfSetAdsFov, MohPcfRequestBorderless.
