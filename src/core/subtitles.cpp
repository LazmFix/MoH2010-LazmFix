#include "core/subtitles.h"

#include "common/logger.h"
#include "core/core.h"
#include "core/window.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace mohpcf::subtitles {
namespace {

constexpr float kLowRez = 21.0F;   // Engine.SubtitleFontSizeLowRez as the game sets it.
constexpr float kNormal = 21.75F;  // Engine.SubtitleFontSize.

std::atomic<float> g_configured{0.0F};
std::atomic<float> g_applied{1.0F};
std::atomic<std::int32_t> g_found{0};
std::atomic<bool> g_started{false};
std::mutex g_mutex;
std::vector<std::uintptr_t> g_fields;  // Address of the first of the two floats.

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

// Pattern: FString {ptr -> L"$DefaultFont", 13, 13} followed by 21.0f, 21.75f.
std::vector<std::uintptr_t> scan() {
  std::vector<std::uintptr_t> found;
  std::uint8_t pattern[16];
  const std::uint32_t len = 13;
  std::memcpy(pattern, &len, 4);
  std::memcpy(pattern + 4, &len, 4);
  std::memcpy(pattern + 8, &kLowRez, 4);
  std::memcpy(pattern + 12, &kNormal, 4);
  std::vector<std::uint8_t> buffer;
  MEMORY_BASIC_INFORMATION info{};
  std::uintptr_t address = 0x10000;
  while (address < 0x7FFF0000 && VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info)) {
    const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const std::size_t size = info.RegionSize;
    if (info.State == MEM_COMMIT && info.Type == MEM_PRIVATE && (info.Protect & PAGE_READWRITE) != 0 &&
        (info.Protect & PAGE_GUARD) == 0) {
      // 1 MB pieces (a 32-bit game has little address space to spare), overlapping by the pattern
      // plus the pointer in front of it.
      constexpr std::size_t kPiece = 1024 * 1024;
      constexpr std::size_t kOverlap = 20;
      buffer.resize(kPiece + kOverlap);
      for (std::size_t offset = 0; offset < size; offset += kPiece) {
        const std::size_t begin = offset >= 4 ? offset - 4 : 0;
        const std::size_t length = std::min(size - begin, kPiece + kOverlap);
        if (!read(base + begin, buffer.data(), length)) {
          continue;
        }
        for (std::size_t i = 4; i + 16 <= length; i += 4) {
          if (std::memcmp(buffer.data() + i, pattern, 16) == 0) {
            std::uint32_t pointer = 0;
            std::memcpy(&pointer, buffer.data() + i - 4, 4);
            const std::uintptr_t field = base + begin + i + 8;
            if (is_default_font(pointer) && (found.empty() || found.back() != field)) {
              found.push_back(field);
            }
          }
        }
      }
    }
    address = base + size;
  }
  return found;
}

float wanted_scale() {
  const float configured = g_configured.load();
  if (configured > 0.0F) {
    return configured;
  }
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

void apply(float scale) {
  std::lock_guard lock(g_mutex);
  const float values[2] = {kLowRez * scale, kNormal * scale};
  for (std::uintptr_t field : g_fields) {
    float current[2]{};
    if (read(field, current, sizeof(current)) && (current[0] != values[0] || current[1] != values[1])) {
      write(field, values, sizeof(values));
    }
  }
  g_applied = scale;
}

void worker() {
  const DWORD start = GetTickCount();
  bool logged = false;
  // Find the fields: they exist once the engine has started (main menu at the latest).
  while (GetTickCount() - start < 10u * 60u * 1000u) {
    std::vector<std::uintptr_t> found = scan();
    if (!found.empty()) {
      {
        std::lock_guard lock(g_mutex);
        for (std::uintptr_t field : found) {
          bool known = false;
          for (std::uintptr_t existing : g_fields) {
            known |= existing == field;
          }
          if (!known) {
            g_fields.push_back(field);
          }
        }
        g_found = static_cast<std::int32_t>(g_fields.size());
      }
      apply(wanted_scale());
      if (!logged || found.size() > 1) {
        char line[160]{};
        std::snprintf(line, sizeof(line), "Subtitle size: %d engine field(s) found, scale %.2f (%.1f / %.1f px)", g_found.load(),
                      g_applied.load(), kLowRez * g_applied.load(), kNormal * g_applied.load());
        core::log().info(line);
        logged = true;
      }
      if (g_found.load() >= 2) {
        break;  // Class default and live engine object.
      }
    }
    Sleep(2000);
  }
  if (g_found.load() == 0) {
    core::log().warn("Subtitle size: engine fields not found (game build differs?): subtitles stay at their size");
    return;
  }
  // Keep the size (the game could reload its localized values) and follow setting / resolution changes.
  for (;;) {
    Sleep(1000);
    apply(wanted_scale());
  }
}

}  // namespace

void start(float scale) {
  g_configured = scale;
  if (!g_started.exchange(true)) {
    std::thread(worker).detach();
  }
}

void set_scale(float scale) {
  g_configured = scale;
  if (g_found.load() > 0) {
    apply(wanted_scale());
  }
}

float configured_scale() {
  return g_configured.load();
}

float applied_scale() {
  return g_applied.load();
}

std::int32_t fields_found() {
  return g_found.load();
}

}  // namespace mohpcf::subtitles
