#include "core/ads.h"

#include "common/logger.h"
#include "core/core.h"
#include "core/fov.h"
#include "core/window.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace mohpcf::ads {
namespace {

// moh.exe 1.0.4.0, measured (see ads.h).
constexpr std::uintptr_t kZoomOffset = 0xDCC;     // Zoom block in the player's actor.
constexpr std::uintptr_t kIndexOffset = 0x04;     // UObject.ObjectInternalInteger (index in GObjObjects).
constexpr std::uintptr_t kClassOffset = 0x34;     // UObject.Class.
constexpr std::uintptr_t kArchetypeOffset = 0x38; // UObject.ObjectArchetype.
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

std::atomic<std::int32_t> g_mode{kGame};
std::atomic<std::int32_t> g_fov_mode{kGame};
std::atomic<float> g_custom{0.75F};
std::atomic<bool> g_started{false};
std::atomic<std::uint32_t> g_object{0};  // Actor holding the zoom block (0 = not found).
std::atomic<std::uint32_t> g_class{0};   // Its class, once known: later searches only look at it.

// Last values seen by update(), for the panel.
std::atomic<float> g_seen_hip{0.0F};
std::atomic<float> g_seen_fov{0.0F};
std::atomic<float> g_seen_game{1.0F};
std::atomic<float> g_seen_result{1.0F};
std::atomic<float> g_seen_aim_game{0.0F};
std::atomic<float> g_seen_aim{0.0F};

// Game thread only (update()).
float g_written_to = 0.0F;  // Aim FOV the fix wrote into the zoom block (0 = none).
float g_game_to = 0.0F;     // The weapon's own aim FOV it replaced.
DWORD g_last_aim = 0;       // GetTickCount() when last seen aiming.

// Engine memory may change under us (the game thread creates and frees objects): structured exception
// handling, no C++ objects in this function.
bool peek32(std::uintptr_t address, std::uint32_t& value) {
  __try {
    value = *reinterpret_cast<volatile const std::uint32_t*>(address);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool pokef(std::uintptr_t address, float value) {
  __try {
    *reinterpret_cast<volatile float*>(address) = value;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool peekf(std::uintptr_t address, float& value) {
  __try {
    value = *reinterpret_cast<volatile const float*>(address);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

struct Zoom {
  float mult = 0.0F;  // The game's aim multiplier.
  float fov = 0.0F;   // Current FOV.
  float from = 0.0F;
  float to = 0.0F;    // Aim FOV while aiming, hip FOV otherwise.
};

bool fov_ok(float f) {
  return std::isfinite(f) && f >= 1.0F && f <= 170.0F;
}

bool read_zoom(std::uintptr_t object, Zoom& z) {
  const std::uintptr_t a = object + kZoomOffset;
  return peekf(a, z.mult) && peekf(a + 4, z.fov) && peekf(a + 8, z.from) && peekf(a + 12, z.to) && std::isfinite(z.mult) &&
         z.mult > 0.01F && z.mult <= 4.0F && fov_ok(z.fov) && fov_ok(z.from) && fov_ok(z.to);
}

// Still the same live object: listed in GObjObjects at its own index, same class.
bool still_valid(std::uint32_t object) {
  const std::uintptr_t list = fov::object_list();
  std::uint32_t index = 0, data = 0, num = 0, entry = 0, cls = 0;
  if (list == 0 || !peek32(object + kIndexOffset, index) || !peek32(list, data) || !peek32(list + 4, num) || index >= num ||
      !peek32(data + 4u * index, entry) || entry != object) {
    return false;
  }
  return peek32(object + kClassOffset, cls) && cls == g_class.load();
}

// Looks through GObjObjects for a live object (not a class default) whose zoom block rests at the hip
// FOV: eases to the camera's FOV and is within a few degrees of it (weapon sway moves it slightly).
// Every class is looked at: the player's pawn class changes with the level (another soldier).
std::uint32_t search(float hip) {
  const std::uintptr_t list = fov::object_list();
  std::uint32_t data = 0, num = 0;
  if (list == 0 || !peek32(list, data) || !peek32(list + 4, num) || num == 0 || num > 0x400000) {
    return 0;
  }
  std::vector<std::uint32_t> objects(num);
  SIZE_T got = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(data)), objects.data(),
                         num * 4u, &got) ||
      got != num * 4u) {
    return 0;
  }
  const std::uint32_t known_class = g_class.load();
  std::vector<std::uint32_t> found;
  for (const std::uint32_t object : objects) {
    std::uint32_t cls = 0, archetype = 0, archetype_class = 0;
    if (object < 0x10000 || !peek32(object + kClassOffset, cls) || cls < 0x10000) {
      continue;
    }
    Zoom z;
    if (!read_zoom(object, z) || std::fabs(z.to - hip) > 0.05F || std::fabs(z.fov - hip) > 5.0F) {
      continue;
    }
    if (!peek32(object + kArchetypeOffset, archetype) || !peek32(archetype + kClassOffset, archetype_class) ||
        archetype_class != cls) {
      continue;  // A class default object, not the live actor.
    }
    found.push_back(object);
  }
  if (found.empty()) {
    return 0;
  }
  // Several candidates: the class seen before wins.
  std::uint32_t pick = found.front();
  for (const std::uint32_t object : found) {
    std::uint32_t cls = 0;
    if (known_class != 0 && peek32(object + kClassOffset, cls) && cls == known_class) {
      pick = object;
      break;
    }
  }
  std::uint32_t cls = 0, index = 0;
  peek32(pick + kClassOffset, cls);
  peek32(pick + kIndexOffset, index);
  Zoom z;
  read_zoom(pick, z);
  char line[220]{};
  std::snprintf(line, sizeof(line),
                "ADS: zoom block in object 0x%08X (index %u, class 0x%08X, %u candidate(s)), game aim multiplier %.3f", pick,
                index, cls, static_cast<unsigned>(found.size()), z.mult);
  core::log().info(line);
  g_class = cls;
  return pick;
}

void worker() {
  for (;;) {
    // Searching: the block is filled the first time the player aims, so look often until found.
    Sleep(g_object.load() == 0 ? 250 : 1000);
    // Nothing to change: the game's aiming at 16:9 (other aspect ratios get the aim FOV Hor+).
    if (g_mode.load() == kGame && g_fov_mode.load() == kGame && fov::hor_plus(50.0F) == 50.0F) {
      continue;
    }
    std::uint32_t object = g_object.load();
    if (object != 0 && !still_valid(object)) {
      g_object = 0;
      object = 0;
      core::log().info("ADS: zoom block gone (level change); looking again");
    }
    const float hip = fov::applied();
    if (object == 0 && hip > 0.0F) {
      g_object = search(hip);
    }
  }
}

// The weapon's aim FOV read as CoD degrees (horizontal at 4:3), shown at this window's aspect.
float mwr_aim_fov(float game_fov) {
  const double half = std::tan(game_fov * 0.5 * kDegToRad) * 0.75 * window::aspect();
  return static_cast<float>(2.0 * std::atan(half) / kDegToRad);
}

void publish(float hip, float fov, float game, float result) {
  g_seen_hip.store(hip, std::memory_order_relaxed);
  g_seen_fov.store(fov, std::memory_order_relaxed);
  g_seen_game.store(game, std::memory_order_relaxed);
  g_seen_result.store(result, std::memory_order_relaxed);
}

}  // namespace

void start(std::int32_t mode, float custom, std::int32_t fov_mode) {
  set(mode, custom);
  set_fov_mode(fov_mode);
  if (!g_started.exchange(true)) {
    std::thread(worker).detach();
  }
}

void set(std::int32_t mode, float custom) {
  g_mode = std::clamp(mode, kGame, kCustom);
  g_custom = std::isfinite(custom) ? std::clamp(custom, 0.05F, 2.0F) : 0.75F;
}

void set_fov_mode(std::int32_t fov_mode) {
  g_fov_mode = std::clamp(fov_mode, kGame, kMwr);
}

std::int32_t fov_mode() {
  return g_fov_mode.load();
}

std::int32_t mode() {
  return g_mode.load();
}

float custom() {
  return g_custom.load();
}

float update() {
  const std::int32_t mode = g_mode.load(std::memory_order_relaxed);
  const std::int32_t fov_mode = g_fov_mode.load(std::memory_order_relaxed);
  const std::uint32_t object = g_object.load(std::memory_order_relaxed);
  const float hip = fov::applied();
  Zoom z;
  if (object == 0 || hip <= 0.0F || !read_zoom(object, z)) {
    g_written_to = 0.0F;
    publish(hip, 0.0F, 1.0F, 1.0F);
    return 1.0F;
  }
  // The weapon's own target, also when the block holds the value the fix wrote.
  const bool ours = g_written_to != 0.0F && z.to == g_written_to;
  const float to_game = ours ? g_game_to : z.to;
  const bool aiming = to_game < hip - 0.5F;
  const DWORD now = GetTickCount();
  if (aiming) {
    g_last_aim = now;
    // The weapon's FOV is made for 16:9: kept Hor+ like the hip FOV (no change at 16:9).
    float want_to = to_game;
    if (const float wide = fov::hor_plus(to_game); wide != to_game) {
      want_to = std::clamp(wide, 1.0F, std::max(1.0F, hip - 1.0F));
    }
    if (fov_mode == kMwr) {
      want_to = std::clamp(mwr_aim_fov(to_game), 1.0F, hip - 1.0F);
    }
    // Checked right before writing, on the game thread (which is the one that frees objects): never
    // into an object freed since the worker last looked.
    if (want_to != z.to && still_valid(object) && pokef(object + kZoomOffset + 12, want_to)) {
      z.to = want_to;
    }
    g_written_to = want_to != to_game ? want_to : 0.0F;
    g_game_to = to_game;
    g_seen_aim_game.store(to_game, std::memory_order_relaxed);
    g_seen_aim.store(z.to, std::memory_order_relaxed);
  } else {
    g_written_to = 0.0F;
  }
  // Easing back out after aiming (the current FOV also sways by ~0.6 deg at the hip: not a zoom).
  const bool easing_out = !aiming && now - g_last_aim < 600 && z.fov < hip - 0.75F;
  const float game = aiming ? z.mult : 1.0F;
  if (mode == kGame || (!aiming && !easing_out)) {
    publish(hip, z.fov, game, game);
    return 1.0F;
  }
  float want = game;
  if (mode == kMwr) {
    // MWR / CoD4: tan(current / 2) / tan(hip / 2), following the zoom as it eases (never below the
    // target: sway can dip the current FOV a little under it).
    const float current = aiming ? std::max(z.fov, z.to) : z.fov;
    want = static_cast<float>(std::tan(current * 0.5 * kDegToRad) / std::tan(hip * 0.5 * kDegToRad));
  } else {
    want = aiming ? g_custom.load(std::memory_order_relaxed) : 1.0F;
  }
  want = std::clamp(want, 0.01F, 1.0F);
  publish(hip, z.fov, game, want);
  return want / game;
}

State state() {
  State s;
  s.found = g_object.load() != 0;
  s.hip_fov = g_seen_hip.load();
  s.fov = g_seen_fov.load();
  s.game_mult = g_seen_game.load();
  s.result = g_seen_result.load();
  s.aim_fov_game = g_seen_aim_game.load();
  s.aim_fov = g_seen_aim.load();
  return s;
}

}  // namespace mohpcf::ads
