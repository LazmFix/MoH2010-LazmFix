#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace mohpcf::startup {

// Runs in DllMain when moh.exe loads the proxy, before the engine reads its settings:
//   - [Display] Resolution=desktop: MOHAEngine.ini ResX/ResY = the desktop's (no mode change, no
//     resolution switch at startup); WindowMode=borderless|fullscreen|windowed sets Fullscreen and the
//     window creation hook (borderless from the first frame: no frame, no taskbar showing through);
//   - [Startup] SkipIntros: renames / restores the intro video;
//   - first start ([Startup] FirstStartDone=0): game mouse smoothing off, frame rate cap at the
//     desktop refresh rate, the game's own "High end" graphics preset (its hardware detection files
//     current GPUs under "Minspec");
//   - [Graphics] ApplyHighEnd=1 (set by the panel): the "High end" preset again, once.
// Only Win32 file, registry and window calls (no shell, no COM): it runs under the loader lock.
void run(HMODULE self);

// What run() did, for the log (the log opens later).
std::vector<std::string> report();

// The game's "High end" preset plus what it leaves out (see startup.cpp). Game closed, or on the next
// start: the game keeps its settings in memory and writes them back.
bool apply_high_end(const std::wstring& engine_ini);

}  // namespace mohpcf::startup
