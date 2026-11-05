#pragma once

#include <cstdint>

namespace mohpcf::hud {

// Native HUD pieces made in 720p pixels, scaled for the real resolution in the game's memory.
//
// Crosshair: MOHAHUDCursor.DefaultCursorData holds the 4 ticks (80-byte structs: PixelStart, PixelSize,
// UV0..UV3, Rotation, RotationOrigin, OffsetVec, bApplyScale at +68). The ticks ship with
// bApplyScale = 0 while every other cursor (red dot, shotgun, ...) has 1; with 1 the engine scales them
// by its resolution scale (height / 720), so at 4K they look as they do at 720p (tested).
//
// Marker text: rich text objects {FString "$DefaultFont", FontSize 20, 20} (objective labels such as
// "Mother" and squadmate name tags such as "Voodoo"). Both sizes are multiplied; once the templates are
// patched at the main menu, labels created in a level inherit the size (tested at 4K).
//
// A background thread finds the data by full memory scans: at start, after each level load (new
// camera object, from the FOV module) and every 30 s; between scans it keeps the found values.

// marker_scale <= 0: automatic (render height / 720); 1 = the game's size.
void start(float marker_scale, bool crosshair);
void set_marker_scale(float scale);
void set_crosshair(bool scale_with_resolution);
float marker_scale_setting();
float marker_scale();          // In force.
bool crosshair_setting();
std::int32_t marker_fields();  // Text objects found.
std::int32_t cursor_tables();  // Crosshair tables found (class default + HUD instances).

}  // namespace mohpcf::hud
