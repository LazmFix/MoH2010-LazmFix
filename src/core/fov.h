#pragma once

#include <cstdint>

namespace mohpcf::fov {

// Field of view. The player camera (UE3 class MOHAPlayerCamera, Camera.DefaultFOV = 70, horizontal
// degrees) is a plain UObject; changing DefaultFOV in the class default object and in the live camera
// widens the view at once (tested at 4K: weapon model and mouse sensitivity per count unchanged).
//
// A background thread finds the camera objects once (float 70 followed 16 bytes later by
// DefaultAspectRatio 4/3, with the UObject header 0x244 bytes before it), then the engine's object
// list (GObjObjects) through the object's index, and from then on updates every object of that class
// once a second: new cameras after level loads, and the class default so they spawn with it.
// Only values equal to the game's 70 or to the value the fix wrote are replaced, so script-driven FOV
// changes (vehicles, set pieces) keep working.

constexpr float kGameDefault = 70.0F;
constexpr float kMin = 50.0F;
constexpr float kMax = 130.0F;

// fov <= 0: game default (70).
void start(float fov);
void set_fov(float fov);
float configured();
float applied();           // DefaultFOV of the live camera (0 before a level is loaded).
std::int32_t objects();    // Camera objects found (class default + live cameras).
bool object_list_found();  // GObjObjects found (cheap updates); otherwise full memory scans.
std::uint32_t live_camera(); // Address of the live camera object; changes when a level is loaded.
std::uintptr_t object_list(); // Address of GObjObjects (TArray<UObject*>: Data, Num, Max), 0 until found.

// The setting is horizontal degrees at 16:9 (the game's 70 is made for 16:9). Other aspect ratios
// keep the vertical view (Hor+): 21:9 and 32:9 see more at the sides instead of losing the top and
// bottom, 16:10 and 4:3 see less at the sides.
float hor_plus(float fov_16_9);
float target();  // DefaultFOV written for the current window (setting or 70, Hor+).

}  // namespace mohpcf::fov
