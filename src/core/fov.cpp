#include "core/fov.h"

#include "common/logger.h"
#include "core/core.h"
#include "core/window.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

namespace mohpcf::fov {
namespace {

// UE3 (moh.exe 1.0.4.0) object layout, measured on the live camera and its class default.
constexpr std::uintptr_t kFieldOffset = 0x244;    // Camera.DefaultFOV from the start of the object.
constexpr std::uintptr_t kAspectOffset = 16;      // Camera.DefaultAspectRatio from DefaultFOV.
constexpr std::uintptr_t kIndexOffset = 0x04;     // UObject.ObjectInternalInteger = index in GObjObjects.
constexpr std::uintptr_t kClassOffset = 0x34;     // UObject.Class.
constexpr std::uintptr_t kArchetypeOffset = 0x38; // UObject.ObjectArchetype.
constexpr std::uint32_t kAspectBits = 0x3FAAAAAB; // 4/3 as a float.

std::atomic<float> g_configured{0.0F};
std::atomic<float> g_applied{0.0F};
std::atomic<std::int32_t> g_objects{0};
std::atomic<bool> g_list_found{false};
std::atomic<std::uint32_t> g_live_object{0};
std::atomic<bool> g_started{false};
HANDLE g_wake = nullptr;

std::uintptr_t g_module_begin = 0;
std::uintptr_t g_module_end = 0;
std::uint32_t g_class = 0;    // MOHAPlayerCamera's UClass.
std::uint32_t g_vtable = 0;
std::uintptr_t g_list = 0;    // Address of GObjObjects (TArray<UObject*>: Data, Num, Max).
float g_last_written = kGameDefault;

// Reads/writes of engine memory that may change under us (objects are created and destroyed by the
// game thread): no C++ objects in these functions, so structured exception handling is allowed.
bool peek32(std::uintptr_t address, std::uint32_t& value) {
  __try {
    value = *reinterpret_cast<volatile const std::uint32_t*>(address);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool poke32(std::uintptr_t address, std::uint32_t value) {
  __try {
    *reinterpret_cast<volatile std::uint32_t*>(address) = value;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool read_block(std::uintptr_t address, void* out, std::size_t size) {
  SIZE_T got = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &got) && got == size;
}

std::uint32_t bits(float f) {
  std::uint32_t u = 0;
  std::memcpy(&u, &f, 4);
  return u;
}

float as_float(std::uint32_t u) {
  float f = 0.0F;
  std::memcpy(&f, &u, 4);
  return f;
}

void find_module() {
  auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  g_module_begin = base;
  g_module_end = base + nt->OptionalHeader.SizeOfImage;
}

struct Header {
  std::uint32_t vtable = 0;
  std::uint32_t index = 0;
  std::uint32_t cls = 0;
  std::uint32_t archetype = 0;
};

bool read_header(std::uintptr_t object, Header& h) {
  return peek32(object, h.vtable) && peek32(object + kIndexOffset, h.index) && peek32(object + kClassOffset, h.cls) &&
         peek32(object + kArchetypeOffset, h.archetype);
}

bool looks_like_object(const Header& h) {
  return h.vtable >= g_module_begin && h.vtable < g_module_end && h.cls >= 0x10000 && h.index > 0 && h.index < 0x400000;
}

// Full scan of private read-write memory for DefaultFOV fields holding one of `values`, followed by
// the 4/3 aspect ratio, inside something that looks like a UObject. Returns object addresses.
std::vector<std::uintptr_t> scan(const std::vector<std::uint32_t>& values) {
  std::vector<std::uintptr_t> found;
  std::vector<std::uint8_t> buffer;
  MEMORY_BASIC_INFORMATION info{};
  std::uintptr_t address = 0x10000;
  while (address < 0x7FFF0000 && VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info)) {
    const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const std::size_t size = info.RegionSize;
    if (info.State == MEM_COMMIT && info.Type == MEM_PRIVATE && (info.Protect & PAGE_READWRITE) != 0 &&
        (info.Protect & PAGE_GUARD) == 0) {
      constexpr std::size_t kPiece = 1024 * 1024;
      constexpr std::size_t kOverlap = kAspectOffset + 4;
      buffer.resize(kPiece + kOverlap);
      for (std::size_t offset = 0; offset < size; offset += kPiece) {
        const std::size_t length = std::min(size - offset, kPiece + kOverlap);
        if (!read_block(base + offset, buffer.data(), length)) {
          continue;
        }
        for (std::size_t i = 0; i + kAspectOffset + 4 <= length && i < kPiece; i += 4) {
          std::uint32_t value = 0, aspect = 0;
          std::memcpy(&value, buffer.data() + i, 4);
          if (std::find(values.begin(), values.end(), value) == values.end()) {
            continue;
          }
          std::memcpy(&aspect, buffer.data() + i + kAspectOffset, 4);
          const std::uintptr_t field = base + offset + i;
          if (aspect != kAspectBits || field < kFieldOffset) {
            continue;
          }
          const std::uintptr_t object = field - kFieldOffset;
          Header h;
          if (read_header(object, h) && looks_like_object(h) && (g_class == 0 || (h.cls == g_class && h.vtable == g_vtable))) {
            found.push_back(object);
          }
        }
      }
    }
    address = base + size;
  }
  return found;
}

// GObjObjects: a TArray {Data, Num, Max} in the exe's writable data whose Data[index] is the object.
std::uintptr_t find_object_list(std::uintptr_t object, std::uint32_t index) {
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_module_begin);
  auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_module_begin + dos->e_lfanew);
  const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
  for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0) {
      continue;
    }
    const std::uintptr_t begin = g_module_begin + section->VirtualAddress;
    const std::uintptr_t end = begin + section->Misc.VirtualSize;
    for (std::uintptr_t a = begin; a + 12 <= end; a += 4) {
      std::uint32_t data = 0, num = 0, max = 0;
      if (!peek32(a, data) || !peek32(a + 4, num) || !peek32(a + 8, max)) {
        a = (a | 0xFFF);  // Skip an unreadable page.
        continue;
      }
      if (data < 0x10000 || num <= index || max < num || max > 0x400000) {
        continue;
      }
      std::uint32_t entry = 0;
      if (peek32(data + 4 * index, entry) && entry == object) {
        return a;
      }
    }
  }
  return 0;
}

float target_now() {
  const float c = g_configured.load();
  return hor_plus(c > 0.0F ? std::clamp(c, kMin, kMax) : kGameDefault);
}

// Sets DefaultFOV of one camera object if it holds the game's or our previous value.
// Returns the value now in the object, or 0 if it is not a camera any more.
float update_object(std::uintptr_t object, std::uint32_t want) {
  Header h;
  if (!read_header(object, h) || h.cls != g_class || h.vtable != g_vtable) {
    return 0.0F;
  }
  std::uint32_t value = 0, aspect = 0;
  if (!peek32(object + kFieldOffset, value) || !peek32(object + kFieldOffset + kAspectOffset, aspect) || aspect != kAspectBits) {
    return 0.0F;
  }
  if (value != want && (value == bits(kGameDefault) || value == bits(g_last_written))) {
    if (poke32(object + kFieldOffset, want)) {
      value = want;
    }
  }
  return as_float(value);
}

std::vector<std::uintptr_t> g_known;  // Fallback (no object list): objects found by scanning.

void update(bool rescan) {
  const float wanted = target_now();
  const std::uint32_t want = bits(wanted);
  std::int32_t count = 0;
  float live = 0.0F;
  std::uint32_t live_object = 0;
  auto visit = [&](std::uintptr_t object) {
    const float v = update_object(object, want);
    if (v > 0.0F) {
      ++count;
      Header h;
      // A live camera's archetype is the class default (another camera object); the default's is not.
      std::uint32_t arch_class = 0;
      if (read_header(object, h) && peek32(h.archetype + kClassOffset, arch_class) && arch_class == g_class) {
        live = v;
        live_object = static_cast<std::uint32_t>(object);
      }
    }
  };
  if (g_list != 0) {
    std::uint32_t data = 0, num = 0;
    if (peek32(g_list, data) && peek32(g_list + 4, num) && num < 0x400000) {
      std::vector<std::uint32_t> objects(num);
      if (num > 0 && read_block(data, objects.data(), num * 4u)) {
        for (std::uint32_t object : objects) {
          std::uint32_t cls = 0;
          if (object >= 0x10000 && peek32(object + kClassOffset, cls) && cls == g_class) {
            visit(object);
          }
        }
      }
    }
  } else {
    if (rescan) {
      for (std::uintptr_t object : scan({bits(kGameDefault), bits(g_last_written), want})) {
        if (std::find(g_known.begin(), g_known.end(), object) == g_known.end()) {
          g_known.push_back(object);
        }
      }
    }
    for (std::uintptr_t object : g_known) {
      visit(object);
    }
  }
  g_last_written = wanted;
  if (count != g_objects.load()) {
    char line[128]{};
    std::snprintf(line, sizeof(line), "FOV: %d camera object(s), field of view %.1f", count, wanted);
    core::log().info(line);
  }
  g_objects = count;
  g_applied = live;
  g_live_object = live_object;
}

void worker() {
  find_module();
  const DWORD start = GetTickCount();
  // 1. Find the camera objects (the class default exists once MOHAGame is loaded, at the main menu).
  std::vector<std::uintptr_t> found;
  while (found.empty() && GetTickCount() - start < 10u * 60u * 1000u) {
    found = scan({bits(kGameDefault)});
    if (found.empty()) {
      Sleep(3000);
    }
  }
  if (found.empty()) {
    core::log().warn("FOV: camera objects not found (game build differs?): the field of view stays at the game's");
    return;
  }
  // All MOHAPlayerCamera objects share class and vtable; take the most common pair.
  std::map<std::pair<std::uint32_t, std::uint32_t>, int> votes;
  for (std::uintptr_t object : found) {
    Header h;
    if (read_header(object, h)) {
      ++votes[{h.cls, h.vtable}];
    }
  }
  auto best = std::max_element(votes.begin(), votes.end(), [](auto& a, auto& b) { return a.second < b.second; });
  g_class = best->first.first;
  g_vtable = best->first.second;
  for (std::uintptr_t object : found) {
    Header h;
    if (read_header(object, h) && h.cls == g_class && h.vtable == g_vtable) {
      g_known.push_back(object);
      if (g_list == 0) {
        g_list = find_object_list(object, h.index);
      }
    }
  }
  g_list_found = g_list != 0;
  char line[200]{};
  std::snprintf(line, sizeof(line), "FOV: camera class 0x%08X, %u object(s) found; engine object list %s", g_class,
                static_cast<unsigned>(g_known.size()), g_list != 0 ? "found" : "not found (slow full scans instead)");
  core::log().info(line);

  // 2. Keep every camera at the wanted FOV (new cameras after level loads, setting changes).
  for (int tick = 0;; ++tick) {
    update(tick % 5 == 0);
    WaitForSingleObject(g_wake, 1000);
  }
}

}  // namespace

void start(float fov) {
  g_configured = fov;
  if (!g_started.exchange(true)) {
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread(worker).detach();
  }
}

void set_fov(float fov) {
  g_configured = fov > 0.0F ? std::clamp(fov, kMin, kMax) : 0.0F;
  if (g_wake != nullptr) {
    SetEvent(g_wake);
  }
}

float configured() {
  return g_configured.load();
}

float applied() {
  return g_applied.load();
}

std::int32_t objects() {
  return g_objects.load();
}

bool object_list_found() {
  return g_list_found.load();
}

std::uint32_t live_camera() {
  return g_live_object.load();
}

std::uintptr_t object_list() {
  return g_list_found.load() ? g_list : 0;
}

float target() {
  return target_now();
}

float hor_plus(float fov_16_9) {
  constexpr double kRad = 3.14159265358979323846 / 180.0;
  const double aspect = window::aspect();
  if (!(aspect > 0.5) || std::fabs(aspect - 16.0 / 9.0) < 0.01) {
    return fov_16_9;
  }
  const double half = std::tan(fov_16_9 * 0.5 * kRad) * aspect / (16.0 / 9.0);
  return std::clamp(static_cast<float>(2.0 * std::atan(half) / kRad), 1.0F, 170.0F);
}

}  // namespace mohpcf::fov
