#pragma once

#include <Windows.h>

#include <cstdint>

namespace mohpcf {

class Logger;

// True for device interface paths of HID collections ("\\?\hid#...", "\\.\HID#...").
bool is_hid_device_path(const wchar_t* path);
bool is_hid_device_path(const char* path);

// Finds the import address table slot of `function_name` in `module` (any imported DLL, matched by
// name). Returns nullptr when the module does not import it by name.
void** find_import_slot(HMODULE module, const char* function_name);

// Reads `module`'s export table directly (GetProcAddress can be hooked: SecuROM does). nullptr for
// forwarded or missing exports.
void* find_export(HMODULE module, const char* function_name);

// True when `address` lies inside `module`'s image.
bool module_contains(HMODULE module, const void* address);

// Startup hang fix.
//
// moh.exe is wrapped by SecuROM, which redirects the CreateFileW/ReadFile imports of DirectInput
// (and other modules) to its own code. For every file DirectInput opens, the hook reads 8 bytes from
// the new handle with a blocking ReadFile. DirectInput opens every HID collection while it builds its
// device list (DirectInput8Create, EnumDevices, CreateDevice); a collection that never sends an input
// report (e.g. the vendor interface MI_03 of Razer mice) makes that read wait forever: the game
// hangs on its splash screen.
//
// While a Scope is alive (the core wraps DirectInput's calls that build the device list), the
// CreateFileW/CreateFileA imports of the real dinput8.dll point to a small shim: HID device paths
// are opened with the genuine KernelBase functions, everything else still goes to whatever the slot
// pointed to (the SecuROM hook). The previous value is put back when the last Scope ends.
class HidGuard {
 public:
  static void initialize(HMODULE real_dinput8, Logger* logger, bool enabled);

  class Scope {
   public:
    Scope();
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
  };

  static bool enabled();
  static bool foreign_hook_seen();       // The slot did not point to KernelBase when a scope began.
  static std::int32_t opens_bypassed();  // HID opens sent past the foreign hook.
};

}  // namespace mohpcf
