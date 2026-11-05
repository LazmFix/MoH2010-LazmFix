// dinput8.dll proxy: moh.exe imports DirectInput8Create from DINPUT8.dll, and a dinput8.dll next to
// moh.exe is loaded instead of the system one. The real DLL is loaded from the system directory on
// first use (never from DllMain).

#include "core/core.h"
#include "core/hid_guard.h"
#include "core/ads.h"
#include "core/fov.h"
#include "core/hud.h"
#include "core/startup.h"
#include "core/subtitles.h"
#include "core/window.h"
#include "common/logger.h"

#include <atomic>
#include <cstdio>

namespace mohpcf {
namespace {

#define DI_W 1
#include "core/dinput_wrap.inl"
#undef DI_W
#define DI_W 0
#include "core/dinput_wrap.inl"
#undef DI_W

template <typename Fn>
Fn real_export(const char* name) {
  if (!core::ensure_started() && core::real_dinput8() == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<Fn>(GetProcAddress(core::real_dinput8(), name));
}

}  // namespace
}  // namespace mohpcf

using namespace mohpcf;

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out,
                                                   LPUNKNOWN outer) {
  using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
  const auto real = real_export<Fn>("DirectInput8Create");
  if (real == nullptr) {
    return DIERR_NOTINITIALIZED;
  }
  HRESULT hr = E_FAIL;
  {
    HidGuard::Scope guard;
    hr = real(instance, version, riid, out, outer);
  }
  static std::atomic<int> logged{0};
  if (logged++ < 8) {
    char line[160]{};
    std::snprintf(line, sizeof(line), "DirectInput8Create(version 0x%04lX, %s) -> 0x%08lX, HID opens bypassed so far: %ld",
                  version, riid == IID_IDirectInput8W ? "Unicode" : riid == IID_IDirectInput8A ? "ANSI" : "other IID",
                  static_cast<unsigned long>(hr), static_cast<long>(HidGuard::opens_bypassed()));
    core::log().info(line);
  }
  if (FAILED(hr) || out == nullptr || *out == nullptr || outer != nullptr) {
    return hr;
  }
  if (riid == IID_IDirectInput8W) {
    *out = static_cast<IDirectInput8W*>(new WrapDI8W(static_cast<IDirectInput8W*>(*out)));
  } else if (riid == IID_IDirectInput8A) {
    *out = static_cast<IDirectInput8A*>(new WrapDI8A(static_cast<IDirectInput8A*>(*out)));
  }
  return hr;
}

extern "C" HRESULT WINAPI Proxy_DllCanUnloadNow() {
  return S_FALSE;  // The game's mouse goes through this module: never unload it.
}

extern "C" HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
  using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
  const auto real = real_export<Fn>("DllGetClassObject");
  return real != nullptr ? real(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI Proxy_DllRegisterServer() {
  using Fn = HRESULT(WINAPI*)();
  const auto real = real_export<Fn>("DllRegisterServer");
  return real != nullptr ? real() : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_DllUnregisterServer() {
  using Fn = HRESULT(WINAPI*)();
  const auto real = real_export<Fn>("DllUnregisterServer");
  return real != nullptr ? real() : E_FAIL;
}

extern "C" LPCDIDATAFORMAT WINAPI Proxy_GetdfDIJoystick() {
  using Fn = LPCDIDATAFORMAT(WINAPI*)();
  const auto real = real_export<Fn>("GetdfDIJoystick");
  return real != nullptr ? real() : nullptr;
}

// ---- API for the ReShade add-on (see core_api.h) ------------------------------------------------

extern "C" __declspec(dllexport) bool MohPcfGetStatus(api::StatusV2* status) {
  core::ensure_started();
  return core::get_status(status);
}

extern "C" __declspec(dllexport) void MohPcfSetEnabled(bool enabled) {
  core::set_enabled(enabled);
}

extern "C" __declspec(dllexport) void MohPcfSetSensitivity(float x, float y) {
  core::set_sensitivity(x, y);
}

extern "C" __declspec(dllexport) void MohPcfSetOverlayOpen(bool open) {
  core::set_overlay_open(open);
}

extern "C" __declspec(dllexport) bool MohPcfSaveSettings() {
  return core::save_settings();
}

extern "C" __declspec(dllexport) void MohPcfRequestBorderless(HWND window, bool on) {
  core::ensure_started();
  window::request_borderless(window, on);
}

extern "C" __declspec(dllexport) void MohPcfSetSubtitleScale(float scale) {
  subtitles::set_scale(scale);
}

extern "C" __declspec(dllexport) void MohPcfSetFov(float fov) {
  fov::set_fov(fov);
}

extern "C" __declspec(dllexport) void MohPcfSetMarkerScale(float scale) {
  hud::set_marker_scale(scale);
}

extern "C" __declspec(dllexport) void MohPcfSetCrosshairScale(bool on) {
  hud::set_crosshair(on);
}

extern "C" __declspec(dllexport) void MohPcfSetAds(std::int32_t mode, float custom) {
  ads::set(mode, custom);
}

extern "C" __declspec(dllexport) void MohPcfSetAdsFov(std::int32_t fov_mode) {
  ads::set_fov_mode(fov_mode);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = length > 0 ? wcsrchr(path, L'\\') : nullptr;
    if (name != nullptr && _wcsicmp(name + 1, api::kGameExeName) == 0) {
      startup::run(module);
    }
  }
  return TRUE;
}
