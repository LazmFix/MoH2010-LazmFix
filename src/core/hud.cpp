#include "core/hud.h"

#include "common/logger.h"
#include "core/core.h"
#include "core/fov.h"
#include "core/window.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace mohpcf::hud {
namespace {

constexpr float kMarkerSize = 20.0F;           // Rich text FontSize in the packages (720p pixels).
constexpr std::size_t kTickStride = 80;        // sizeof(CursorData) in memory.
constexpr std::size_t kApplyScaleOffset = 68;  // CursorData.bApplyScale.
constexpr int kTicks = 4;
// First two crosshair ticks: PixelStart, PixelSize, UV0 (MOHAHUDCursor.DefaultCursorData[0..1]).
constexpr float kTick0[6] = {-2.0F, -8.0F, 4.0F, 8.0F, 504.0F, 498.0F};
constexpr float kTick1[4] = {-2.0F, 0.0F, 4.0F, 8.0F};

std::atomic<float> g_marker_setting{0.0F};
std::atomic<float> g_marker_applied{1.0F};
std::atomic<bool> g_crosshair{true};
std::atomic<std::int32_t> g_marker_count{0};
std::atomic<std::int32_t> g_cursor_count{0};
std::atomic<bool> g_started{false};
HANDLE g_wake = nullptr;
std::mutex g_mutex;
std::vector<std::uintptr_t> g_markers;  // Address of the first of the two sizes.
std::vector<std::uintptr_t> g_cursors;  // Address of DefaultCursorData[0].

bool read(std::uintptr_t address, void* out, std::size_t size) {
  SIZE_T got = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &got) && got == size;
}

bool write(std::uintptr_t address, const void* data, std::size_t size) {
  SIZE_T done = 0;
  return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), data, size, &done) && done == size;
}

bool is_default_font(std::uint32_t pointer) {
  static const wchar_t kText[] = L"$DefaultFont";
  wchar_t buffer[13]{};
  return pointer != 0 && read(pointer, buffer, sizeof(buffer)) && std::memcmp(buffer, kText, sizeof(kText)) == 0;
}

float auto_scale() {
  int height = 0;
  if (HWND window = window::game_window(); window != nullptr) {
    RECT client{};
    if (GetClientRect(window, &client)) {
      height = client.bottom - client.top;
    }
  }
  if (height < 480) {
    height = GetSystemMetrics(SM_CYSCREEN);
  }
  return height > 0 ? static_cast<float>(height) / 720.0F : 1.0F;
}

float wanted_marker_scale() {
  const float s = g_marker_setting.load();
  return s > 0.0F ? std::clamp(s, 1.0F, 4.0F) : auto_scale();
}

void add_unique(std::vector<std::uintptr_t>& list, std::uintptr_t value) {
  if (std::find(list.begin(), list.end(), value) == list.end()) {
    list.push_back(value);
  }
}

// One pass over private read-write memory for both kinds of data.
void scan() {
  const float applied = g_marker_applied.load();
  const float sizes[2] = {kMarkerSize, kMarkerSize * applied};
  std::vector<std::uintptr_t> markers;
  std::vector<std::uintptr_t> cursors;
  std::vector<std::uint8_t> buffer;
  MEMORY_BASIC_INFORMATION info{};
  std::uintptr_t address = 0x10000;
  while (address < 0x7FFF0000 && VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info)) {
    const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const std::size_t size = info.RegionSize;
    if (info.State == MEM_COMMIT && info.Type == MEM_PRIVATE && (info.Protect & PAGE_READWRITE) != 0 &&
        (info.Protect & PAGE_GUARD) == 0) {
      constexpr std::size_t kPiece = 1024 * 1024;
      constexpr std::size_t kOverlap = kTickStride + 16;
      buffer.resize(kPiece + kOverlap + 4);
      for (std::size_t offset = 0; offset < size; offset += kPiece) {
        const std::size_t begin = offset >= 4 ? offset - 4 : 0;
        const std::size_t length = std::min(size - begin, kPiece + kOverlap + 4);
        if (!read(base + begin, buffer.data(), length)) {
          continue;
        }
        const std::uint8_t* b = buffer.data();
        for (std::size_t i = 4; i + 24 <= length && i < kPiece + 4; i += 4) {
          float f[6];
          std::memcpy(f, b + i, 24);
          // Crosshair table.
          if (f[0] == kTick0[0] && f[1] == kTick0[1] && std::memcmp(f, kTick0, sizeof(kTick0)) == 0 &&
              i + kTickStride + 16 <= length && std::memcmp(b + i + kTickStride, kTick1, sizeof(kTick1)) == 0) {
            add_unique(cursors, base + begin + i);
            continue;
          }
          // Rich text: {ptr, 13, 13, size, size} with ptr -> "$DefaultFont".
          std::uint32_t len[2];
          std::memcpy(len, b + i, 8);
          if (len[0] != 13 || len[1] != 13) {
            continue;
          }
          float s[2];
          std::memcpy(s, b + i + 8, 8);
          if (s[0] != s[1] || (s[0] != sizes[0] && s[0] != sizes[1])) {
            continue;
          }
          std::uint32_t pointer = 0;
          std::memcpy(&pointer, b + i - 4, 4);
          if (is_default_font(pointer)) {
            add_unique(markers, base + begin + i + 8);
          }
        }
      }
    }
    address = base + size;
  }
  std::lock_guard lock(g_mutex);
  for (std::uintptr_t m : markers) {
    add_unique(g_markers, m);
  }
  for (std::uintptr_t c : cursors) {
    add_unique(g_cursors, c);
  }
}

void apply() {
  std::lock_guard lock(g_mutex);
  const float scale = wanted_marker_scale();
  const float previous = g_marker_applied.load();
  const float values[2] = {kMarkerSize * scale, kMarkerSize * scale};
  std::int32_t markers = 0;
  for (auto it = g_markers.begin(); it != g_markers.end();) {
    float current[2]{};
    const bool ours = read(*it, current, sizeof(current)) && current[0] == current[1] &&
                      (current[0] == kMarkerSize || current[0] == kMarkerSize * previous || current[0] == values[0]);
    if (!ours) {
      it = g_markers.erase(it);  // Memory reused for something else.
      continue;
    }
    if (current[0] != values[0]) {
      write(*it, values, sizeof(values));
    }
    ++markers;
    ++it;
  }
  g_marker_applied = scale;
  g_marker_count = markers;

  const std::uint32_t flag = g_crosshair.load() ? 1 : 0;
  std::int32_t cursors = 0;
  for (auto it = g_cursors.begin(); it != g_cursors.end();) {
    float head[6]{};
    if (!read(*it, head, sizeof(head)) || std::memcmp(head, kTick0, sizeof(kTick0)) != 0) {
      it = g_cursors.erase(it);
      continue;
    }
    for (int k = 0; k < kTicks; ++k) {
      const std::uintptr_t field = *it + k * kTickStride + kApplyScaleOffset;
      std::uint32_t current = 2;
      if (read(field, &current, 4) && current <= 1 && current != flag) {
        write(field, &flag, 4);
      }
    }
    ++cursors;
    ++it;
  }
  g_cursor_count = cursors;
}

void worker() {
  const DWORD start = GetTickCount();
  DWORD last_scan = 0;
  std::uint32_t camera = 0;
  DWORD level_change = 0;
  bool logged = false;
  for (;;) {
    const DWORD now = GetTickCount();
    const std::uint32_t live = fov::live_camera();
    if (live != camera) {
      camera = live;
      level_change = now;
    }
    const DWORD since_level = now - level_change;
    const bool searching = g_cursors.empty() && g_markers.empty() && now - start < 10u * 60u * 1000u;
    // Scans: while nothing is found yet (every 3 s), 1 / 4 / 10 s after a level load, then every 30 s.
    const bool after_level = level_change != 0 && ((since_level >= 1000 && last_scan < level_change + 1000) ||
                                                    (since_level >= 4000 && last_scan < level_change + 4000) ||
                                                    (since_level >= 10000 && last_scan < level_change + 10000));
    if ((searching && now - last_scan >= 3000) || after_level || now - last_scan >= 30000) {
      scan();
      last_scan = GetTickCount();
    }
    apply();
    if (!logged && (g_cursor_count.load() > 0 || g_marker_count.load() > 0)) {
      char line[200]{};
      std::snprintf(line, sizeof(line), "HUD: crosshair tables %d (scale with resolution %s), marker text objects %d (x%.2f)",
                    g_cursor_count.load(), g_crosshair.load() ? "on" : "off", g_marker_count.load(), g_marker_applied.load());
      core::log().info(line);
      logged = true;
    }
    WaitForSingleObject(g_wake, 1000);
  }
}

}  // namespace

void start(float marker_scale, bool crosshair) {
  g_marker_setting = marker_scale;
  g_crosshair = crosshair;
  if (!g_started.exchange(true)) {
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread(worker).detach();
  }
}

void set_marker_scale(float scale) {
  g_marker_setting = scale;
  if (g_wake != nullptr) {
    SetEvent(g_wake);
  }
}

void set_crosshair(bool scale_with_resolution) {
  g_crosshair = scale_with_resolution;
  if (g_wake != nullptr) {
    SetEvent(g_wake);
  }
}

float marker_scale_setting() {
  return g_marker_setting.load();
}

float marker_scale() {
  return g_marker_applied.load();
}

bool crosshair_setting() {
  return g_crosshair.load();
}

std::int32_t marker_fields() {
  return g_marker_count.load();
}

std::int32_t cursor_tables() {
  return g_cursor_count.load();
}

}  // namespace mohpcf::hud
