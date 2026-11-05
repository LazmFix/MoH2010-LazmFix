#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace mohpcf::window {

// Borderless fullscreen.
//
// At startup (before the game makes its window) a CBT hook on the game's main thread changes the
// game window as it is created: no frame, monitor-sized, at the monitor's corner. The game then runs
// windowed (MOHAEngine.ini Fullscreen=False, set by the startup code), so the display mode never
// changes and the desktop refresh rate applies. The ReShade add-on also turns an exclusive fullscreen
// request (the game menu's "Fullscreen") into a windowed swap chain and asks for borderless here; the
// window itself is changed on the thread that owns it (the game thread, when it next reads the
// mouse), never from the render thread.
void install_creation_hook(bool borderless);
std::vector<std::string> creation_report();  // Top-level windows seen being created (for the log).

void request_borderless(HWND window, bool on);

// Called on the game thread (mouse reads): applies a pending request.
void pump();

void note_game_window(HWND window);
bool borderless_active();
HWND game_window();

// Width / height of the game window's client area; before the window shows the game, the resolution
// the game starts with (set_render_size, from MOHAEngine.ini), else the monitor's.
float aspect();
void set_render_size(int width, int height);

}  // namespace mohpcf::window
