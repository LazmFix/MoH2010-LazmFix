#include "core/window.h"

#include "common/logger.h"
#include "core/core.h"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace mohpcf::window {
namespace {

constexpr LONG_PTR kFrameStyles = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

std::mutex g_mutex;
HWND g_requested_window = nullptr;
bool g_requested_on = false;
std::atomic<bool> g_pending{false};

HWND g_window = nullptr;         // Subclassed window (owner thread only).
WNDPROC g_previous_proc = nullptr;
std::atomic<bool> g_active{false};
std::atomic<HWND> g_known_window{nullptr};

std::atomic<float> g_render_aspect{0.0F};  // ResX / ResY the game starts with.

HHOOK g_hook = nullptr;
bool g_hook_borderless = false;
std::mutex g_report_mutex;
std::vector<std::string> g_report;

RECT monitor_rect(HWND window) {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(window != nullptr ? MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY)
                                    : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY),
                  &info);
  return info.rcMonitor;
}

LRESULT CALLBACK subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (g_active.load()) {
    switch (message) {
      case WM_STYLECHANGING:
        if (wparam == static_cast<WPARAM>(GWL_STYLE)) {
          auto* styles = reinterpret_cast<STYLESTRUCT*>(lparam);
          styles->styleNew = (styles->styleNew & ~static_cast<DWORD>(kFrameStyles)) | WS_POPUP;
        }
        break;
      case WM_WINDOWPOSCHANGING: {
        auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
        // Keep the window on its monitor, monitor-sized. Minimizing (Alt+Tab handling) is left alone.
        if (!IsIconic(window) && pos->x != -32000) {
          const RECT rect = monitor_rect(window);
          pos->x = rect.left;
          pos->y = rect.top;
          pos->cx = rect.right - rect.left;
          pos->cy = rect.bottom - rect.top;
          pos->flags &= ~static_cast<UINT>(SWP_NOMOVE | SWP_NOSIZE);
        }
        break;
      }
      case WM_NCCALCSIZE:
        if (wparam != 0) {
          return 0;  // Whole window is client area.
        }
        break;
      default:
        break;
    }
  }
  return CallWindowProcW(g_previous_proc, window, message, wparam, lparam);
}

// Plain GWLP_WNDPROC subclassing (no comctl32 dependency). The core is never unloaded, so the procedure
// stays valid; a window the game destroys simply takes it with it. The game sets its own window
// procedure after creating the window, so this is checked again on the game thread (pump).
bool subclass(HWND window) {
  const auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
  if (g_window == window && current == &subclass_proc) {
    return false;
  }
  g_previous_proc =
      reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&subclass_proc)));
  g_window = window;
  return true;
}

void apply(HWND window, bool on) {
  if (window == nullptr || !IsWindow(window)) {
    return;
  }
  if (on) {
    subclass(window);
    const bool was_active = g_active.exchange(true);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const RECT rect = monitor_rect(window);
    RECT now{};
    GetWindowRect(window, &now);
    if (!was_active || (style & kFrameStyles) != 0 || !EqualRect(&now, &rect)) {
      SetWindowLongPtrW(window, GWL_STYLE, (style & ~kFrameStyles) | WS_POPUP | WS_VISIBLE);
      SetWindowPos(window, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                   SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
      char line[160]{};
      std::snprintf(line, sizeof(line), "Borderless window: %ldx%ld at %ld,%ld", rect.right - rect.left, rect.bottom - rect.top,
                    rect.left, rect.top);
      core::log().info(line);
    }
  } else if (g_active.exchange(false)) {
    core::log().info("Borderless window off (the game's own window mode applies)");
  }
}

// UE3's game window: class "<package>UnrealUWindowsClient" (MOHAGame: "LaunchUnrealUWindowsClient").
bool is_game_class(const wchar_t* name) {
  return name != nullptr && !IS_INTRESOURCE(name) && wcsstr(name, L"UnrealUWindowsClient") != nullptr;
}

LRESULT CALLBACK cbt_proc(int code, WPARAM wparam, LPARAM lparam) {
  if (code == HCBT_CREATEWND) {
    auto* create = reinterpret_cast<CBT_CREATEWNDW*>(lparam);
    CREATESTRUCTW* cs = create != nullptr ? create->lpcs : nullptr;
    if (cs != nullptr && (cs->style & WS_CHILD) == 0) {
      const HWND window = reinterpret_cast<HWND>(wparam);
      const bool game = is_game_class(cs->lpszClass);
      {
        std::lock_guard lock(g_report_mutex);
        if (g_report.size() < 12) {
          char line[200]{};
          std::snprintf(line, sizeof(line), "Window created: class \"%ls\", style 0x%08lX, %dx%d%s",
                        IS_INTRESOURCE(cs->lpszClass) ? L"#atom" : cs->lpszClass, static_cast<unsigned long>(cs->style), cs->cx,
                        cs->cy, game && g_hook_borderless ? " -> borderless, monitor-sized" : "");
          g_report.emplace_back(line);
        }
      }
      if (game) {
        g_known_window = window;
        if (g_hook_borderless) {
          const RECT rect = monitor_rect(nullptr);
          cs->style = (cs->style & ~static_cast<LONG>(kFrameStyles)) | WS_POPUP;
          cs->dwExStyle &= ~static_cast<DWORD>(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
          cs->x = rect.left;
          cs->y = rect.top;
          cs->cx = rect.right - rect.left;
          cs->cy = rect.bottom - rect.top;
          SetWindowLongPtrW(window, GWL_STYLE, cs->style);
          subclass(window);
          g_active = true;
        }
      }
    }
  } else if ((code == HCBT_ACTIVATE || code == HCBT_SETFOCUS || code == HCBT_MINMAX || code == HCBT_MOVESIZE) &&
             g_hook_borderless && g_active.load() && g_window != nullptr && reinterpret_cast<HWND>(wparam) == g_window &&
             reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g_window, GWLP_WNDPROC)) != &subclass_proc) {
    // The game has set its own window procedure since creating the window: subclass again now, the
    // style and position are put right on the game thread (pump).
    char line[160]{};
    std::snprintf(line, sizeof(line), "Window procedure 0x%p replaced (CBT code %d): subclassed again",
                  reinterpret_cast<void*>(GetWindowLongPtrW(g_window, GWLP_WNDPROC)), code);
    core::log().info(line);
    subclass(g_window);
    request_borderless(g_window, true);
  }
  return CallNextHookEx(g_hook, code, wparam, lparam);
}

}  // namespace

void install_creation_hook(bool borderless) {
  g_hook_borderless = borderless;
  if (g_hook == nullptr) {
    g_hook = SetWindowsHookExW(WH_CBT, cbt_proc, nullptr, GetCurrentThreadId());
  }
}

std::vector<std::string> creation_report() {
  std::lock_guard lock(g_report_mutex);
  std::vector<std::string> out = g_report;
  if (g_hook == nullptr) {
    out.emplace_back("Window creation hook not installed: borderless applies once the game reads the mouse");
  }
  return out;
}

void request_borderless(HWND window, bool on) {
  {
    std::lock_guard lock(g_mutex);
    g_requested_window = window;
    g_requested_on = on;
  }
  if (window != nullptr) {
    g_known_window = window;
  }
  g_pending = true;
  // Wake the owner thread even when the game does not read the mouse (menus in the background).
  PostMessageW(window, WM_NULL, 0, 0);
}

void pump() {
  if (!g_pending.load(std::memory_order_relaxed)) {
    // Borderless from window creation: the game replaces the window procedure (and sets a framed
    // window style) after creating the window. Put both right once, on the owner thread.
    if (g_active.load(std::memory_order_relaxed) && g_window != nullptr &&
        reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g_window, GWLP_WNDPROC)) != &subclass_proc &&
        GetWindowThreadProcessId(g_window, nullptr) == GetCurrentThreadId()) {
      core::log().info("Window procedure replaced by the game: borderless set again");
      g_active = false;
      apply(g_window, true);
    }
    return;
  }
  HWND window = nullptr;
  bool on = false;
  {
    std::lock_guard lock(g_mutex);
    window = g_requested_window;
    on = g_requested_on;
  }
  if (window != nullptr && GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) {
    return;  // Not the owner thread: wait for a call from the right one.
  }
  g_pending = false;
  apply(window, on);
}

void note_game_window(HWND window) {
  if (window != nullptr && g_known_window.load() == nullptr) {
    g_known_window = window;
  }
}

bool borderless_active() {
  return g_active.load();
}

HWND game_window() {
  return g_known_window.load();
}

void set_render_size(int width, int height) {
  if (width >= 640 && height >= 480) {
    g_render_aspect = static_cast<float>(width) / static_cast<float>(height);
  }
}

float aspect() {
  // The game window while it shows the game (not while it is being set up, minimized or tiny);
  // otherwise the resolution the game was started with.
  const HWND window = g_known_window.load();
  RECT rect{};
  if (window != nullptr && IsWindowVisible(window) && !IsIconic(window) && GetClientRect(window, &rect) && rect.right >= 640 &&
      rect.bottom >= 480) {
    const float a = static_cast<float>(rect.right) / static_cast<float>(rect.bottom);
    if (a >= 1.0F && a <= 4.0F) {
      return a;
    }
  }
  if (g_render_aspect > 0.0F) {
    return g_render_aspect;
  }
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsHeight > 0) {
    return static_cast<float>(mode.dmPelsWidth) / static_cast<float>(mode.dmPelsHeight);
  }
  return 16.0F / 9.0F;
}

}  // namespace mohpcf::window
