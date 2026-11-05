// End-to-end test of the dinput8.dll proxy without the game.
//
// Usage: <copy named moh.exe> <path to built dinput8.dll>          (game process: mouse is wrapped)
//        <copy named other.exe> <path to built dinput8.dll> --not-game
//
// Stands in for SecuROM: puts a recording hook into the real dinput8.dll's CreateFileW import, then
// checks that DirectInput8Create / EnumDevices / CreateDevice through the proxy open HID devices
// past that hook (and everything else through it), and that the hook is back in place afterwards.

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include "core/core_api.h"
#include "core/hid_guard.h"

#include <atomic>

#ifndef DIDFT_OPTIONAL
#define DIDFT_OPTIONAL 0x80000000
#endif
#include <cstdio>
#include <cstring>
#include <string>

static int g_failures = 0;
#define CHECK(expr)                                                         \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #expr);         \
      ++g_failures;                                                         \
    }                                                                       \
  } while (0)

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileWFn g_genuine = nullptr;
static std::atomic<int> g_fake_hid{0};
static std::atomic<int> g_fake_other{0};

static HANDLE WINAPI fake_securom_create_file_w(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                                                DWORD disposition, DWORD flags, HANDLE template_file) {
  if (mohpcf::is_hid_device_path(name)) {
    ++g_fake_hid;
  } else {
    ++g_fake_other;
  }
  return g_genuine(name, access, share, security, disposition, flags, template_file);
}

static BOOL CALLBACK count_device(LPCDIDEVICEINSTANCEW, LPVOID ref) {
  ++*static_cast<int*>(ref);
  return DIENUM_CONTINUE;
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::printf("usage: proxy_test <dinput8.dll> [--not-game]\n");
    return 2;
  }
  const bool not_game = argc > 2 && std::wcscmp(argv[2], L"--not-game") == 0;
  HMODULE proxy = LoadLibraryW(argv[1]);
  CHECK(proxy != nullptr);
  if (proxy == nullptr) {
    return 1;
  }
  auto get_status = reinterpret_cast<mohpcf::api::GetStatusFn>(GetProcAddress(proxy, "MohPcfGetStatus"));
  auto set_sens = reinterpret_cast<mohpcf::api::SetSensitivityFn>(GetProcAddress(proxy, "MohPcfSetSensitivity"));
  using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
  auto create = reinterpret_cast<CreateFn>(GetProcAddress(proxy, "DirectInput8Create"));
  CHECK(get_status != nullptr && set_sens != nullptr && create != nullptr);
  CHECK(GetProcAddress(proxy, "DllCanUnloadNow") != nullptr && GetProcAddress(proxy, "GetdfDIJoystick") != nullptr);
  if (get_status == nullptr || create == nullptr) {
    return 1;
  }

  mohpcf::api::StatusV2 status{};
  CHECK(get_status(&status));  // Starts the core: the real dinput8.dll is loaded now.

  wchar_t system_dir[MAX_PATH]{};
  GetSystemDirectoryW(system_dir, MAX_PATH);
  const std::wstring real_path = std::wstring(system_dir) + L"\\dinput8.dll";
  HMODULE real = GetModuleHandleW(real_path.c_str());
  CHECK(real != nullptr && real != proxy);
  void** slot = mohpcf::find_import_slot(real, "CreateFileW");
  CHECK(slot != nullptr);
  if (slot == nullptr) {
    return 1;
  }
  g_genuine = reinterpret_cast<CreateFileWFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CreateFileW"));
  DWORD old_protect = 0;
  VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect);
  *slot = reinterpret_cast<void*>(&fake_securom_create_file_w);
  VirtualProtect(slot, sizeof(void*), old_protect, &old_protect);

  IDirectInput8W* di = nullptr;
  HRESULT hr = create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&di), nullptr);
  CHECK(SUCCEEDED(hr) && di != nullptr);
  if (di == nullptr) {
    return 1;
  }
  int devices = 0;
  CHECK(SUCCEEDED(di->EnumDevices(DI8DEVCLASS_ALL, count_device, &devices, DIEDFL_ALLDEVICES)));
  CHECK(*slot == reinterpret_cast<void*>(&fake_securom_create_file_w));  // Hook put back.

  CHECK(get_status(&status));
  std::printf("devices %d, HID opens bypassed %d, HID opens through the fake hook %d, other opens through it %d\n", devices,
              status.hid_opens_bypassed, g_fake_hid.load(), g_fake_other.load());
  CHECK(g_fake_hid.load() == 0);
  CHECK(status.securom_hook_seen == 1);
  CHECK(status.hid_opens_bypassed > 0);  // Any PC has HID collections DirectInput opens.

  // Mouse.
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"mohpcf_test";
  RegisterClassW(&wc);
  HWND window = CreateWindowExW(0, wc.lpszClassName, L"test", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
  CHECK(window != nullptr);

  IDirectInputDevice8W* mouse = nullptr;
  CHECK(SUCCEEDED(di->CreateDevice(GUID_SysMouse, &mouse, nullptr)) && mouse != nullptr);
  if (mouse != nullptr) {
    DIOBJECTDATAFORMAT objects[] = {
        {&GUID_XAxis, 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&GUID_YAxis, 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&GUID_ZAxis, 8, DIDFT_AXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0},
        {nullptr, 12, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 13, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 14, DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0},
        {nullptr, 15, DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0},
    };
    DIDATAFORMAT format = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 16, 7, objects};
    CHECK(SUCCEEDED(mouse->SetDataFormat(&format)));
    CHECK(SUCCEEDED(mouse->SetCooperativeLevel(window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)));
    CHECK(SUCCEEDED(mouse->Acquire()));
    DIMOUSESTATE state{};
    CHECK(SUCCEEDED(mouse->GetDeviceState(sizeof(state), &state)));
    set_sens(2.0F, 0.5F);
    CHECK(get_status(&status));
    if (not_game) {
      CHECK(status.mouse_wrapped == 0);
    } else {
      CHECK(status.mouse_wrapped == 1);
      CHECK(status.sensitivity_x == 2.0F && status.sensitivity_y == 0.5F);
    }
    mouse->Unacquire();
    mouse->Release();
  }
  CHECK(*slot == reinterpret_cast<void*>(&fake_securom_create_file_w));
  di->Release();
  DestroyWindow(window);

  if (g_failures == 0) {
    std::printf("proxy test passed (%s)\n", not_game ? "not the game" : "game process");
    return 0;
  }
  std::printf("%d failure(s)\n", g_failures);
  return 1;
}
