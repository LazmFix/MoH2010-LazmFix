#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <cstdint>

#include "core/core_api.h"

namespace mohpcf {

class Logger;

// Process-wide state of the core: settings, log, the real dinput8.dll and the mouse.
namespace core {

// Loads the real dinput8.dll, settings and log once. Safe to call from any export (not DllMain).
bool ensure_started();
HMODULE real_dinput8();
Logger& log();

// ---- Game mouse (the system mouse device the game created) --------------------------------------

// Data format the game set on its mouse: where X and Y sit in its state / which dwOfs they use.
void mouse_set_data_format(LPCDIDATAFORMAT format);
void mouse_set_cooperative_level(HWND window, DWORD flags);
void mouse_set_acquired(bool acquired);
void mouse_note_created();

// Applies the settings to what the game is about to receive (in place).
void mouse_process_state(DWORD size, void* data);
void mouse_process_data(DWORD object_size, DIDEVICEOBJECTDATA* data, DWORD count, HRESULT result);

// ---- API for the add-on ------------------------------------------------------------------------
bool get_status(api::StatusV2* status);
void set_enabled(bool enabled);
void set_sensitivity(float x, float y);
void set_overlay_open(bool open);
bool save_settings();

bool is_game_process();

}  // namespace core
}  // namespace mohpcf
