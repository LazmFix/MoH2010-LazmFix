#pragma once

#include <cstdint>

namespace mohpcf::ads {

// Aiming down sights (right mouse button): sensitivity and field of view.
//
// MoH: while aiming the game multiplies the mouse by a fixed 0.75 (measured: 1000 counts turn 12022
// units from the hip, 9017 aimed, with hip FOV 70 and 80.7 alike) and narrows the view to the weapon's
// aim FOV (red dot: 50 deg, whatever the hip FOV). Both sit in a zoom block of the player's actor
// (object + 0xDCC), filled the first time the player aims in a level:
//   +0 the game's aim multiplier (0.75), +4 the current FOV (eases between hip and aim FOV in ~150 ms;
//   weapon sway moves it by up to ~0.6 deg), +8 the FOV it eases from, +12 the FOV it eases to (the aim
//   FOV while aiming, the hip FOV otherwise). The actor's class differs between levels.
//
// MWR (and CoD4): the mouse is multiplied by tan(current FOV / 2) / tan(cg_fov / 2), following the
// zoom as it eases in (measured in MWR: M21 scope 0.2067 = tan(7.5) / tan(32.5); "0% monitor
// distance"). Aim FOVs are the weapon's adsZoomFov in CoD degrees (horizontal at 4:3): rifles 50,
// M21 scope 15.
//
// Sensitivity modes: kGame leaves the game's 0.75; kMwr gives the MWR rule with MoH's own FOVs;
// kCustom uses a fixed multiplier while aiming instead of 0.75. The correction only slows the mouse
// down relative to the hip, and only while aiming or easing back out (scripted FOVs are left alone).
// Aim FOV modes: kGame keeps the weapon's FOV; kMwr reads the weapon's number as CoD degrees, like MWR
// does (red dot 50 -> 63.7 deg at 16:9, the same picture as an MWR rifle sight).

constexpr std::int32_t kGame = 0;
constexpr std::int32_t kMwr = 1;
constexpr std::int32_t kCustom = 2;

void start(std::int32_t mode, float custom, std::int32_t fov_mode);
void set(std::int32_t mode, float custom);
void set_fov_mode(std::int32_t fov_mode);
std::int32_t mode();
float custom();
std::int32_t fov_mode();

// Called on the game thread for every mouse read: applies the aim FOV setting and returns the factor
// for the fix's own count multiplier (1 when not aiming or when the zoom block is not found).
float update();

struct State {
  bool found = false;          // Zoom block found.
  float hip_fov = 0.0F;        // Camera FOV (the fix's FOV setting or the game's 70).
  float fov = 0.0F;            // Current FOV.
  float aim_fov_game = 0.0F;   // The weapon's own aim FOV (last seen while aiming).
  float aim_fov = 0.0F;        // Aim FOV in force (last seen while aiming).
  float game_mult = 1.0F;      // The game's multiplier in force now (0.75 while aiming).
  float result = 1.0F;         // Turn per count now / turn per count from the hip.
};
State state();

}  // namespace mohpcf::ads
