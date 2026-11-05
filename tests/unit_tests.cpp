// Unit tests without the game: HID path matching, import slot lookup, counts scaling.

#include "common/game_ini.h"
#include "common/mouse_math.h"
#include "core/hid_guard.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>

static int g_failures = 0;

#define CHECK(expr)                                                         \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #expr);         \
      ++g_failures;                                                         \
    }                                                                       \
  } while (0)

static void test_hid_paths() {
  using mohpcf::is_hid_device_path;
  CHECK(is_hid_device_path(L"\\\\?\\hid#vid_1532&pid_0099&mi_03#8&16d63b97&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"));
  CHECK(is_hid_device_path(L"\\\\.\\HID#VID_046D&PID_C52B"));
  CHECK(is_hid_device_path("\\\\?\\HiD#x"));
  CHECK(!is_hid_device_path(L"C:\\Games\\moh.exe"));
  CHECK(!is_hid_device_path(L"\\\\?\\usb#vid_1532"));
  CHECK(!is_hid_device_path(L"\\\\?\\hidx"));
  CHECK(!is_hid_device_path(static_cast<const wchar_t*>(nullptr)));
  CHECK(!is_hid_device_path(L""));
  CHECK(!is_hid_device_path("\\\\?"));
}

static void test_import_slot() {
  // This test program imports GetTickCount64 and QueryPerformanceCounter from kernel32 by name.
  volatile ULONGLONG keep = GetTickCount64();
  (void)keep;
  void** slot = mohpcf::find_import_slot(GetModuleHandleW(nullptr), "GetTickCount64");
  CHECK(slot != nullptr);
  if (slot != nullptr) {
    void* k32 = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount64"));
    void* kb = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetTickCount64"));
    CHECK(*slot == k32 || *slot == kb);
  }
  CHECK(mohpcf::find_import_slot(GetModuleHandleW(nullptr), "NoSuchFunctionAnywhere") == nullptr);
  CHECK(mohpcf::find_import_slot(nullptr, "GetTickCount64") == nullptr);
}

static void test_export_lookup() {
  HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
  HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
  void* create = mohpcf::find_export(kb, "CreateFileW");
  CHECK(create != nullptr);
  CHECK(create == reinterpret_cast<void*>(GetProcAddress(kb, "CreateFileW")));
  CHECK(mohpcf::module_contains(kb, create));
  CHECK(!mohpcf::module_contains(k32, create));
  CHECK(mohpcf::module_contains(k32, reinterpret_cast<void*>(GetProcAddress(k32, "CreateFileW"))));
  int on_stack = 0;
  CHECK(!mohpcf::module_contains(kb, &on_stack));
  CHECK(mohpcf::find_export(kb, "NoSuchExportAnywhere") == nullptr);
}

static void test_game_ini_bools() {
  using mohpcf::game_ini::format_bool;
  using mohpcf::game_ini::parse_bool;
  const std::wstring da = L"\x0414\x0430";
  const std::wstring net = L"\x041d\x0435\x0442";
  CHECK(parse_bool(da));
  CHECK(!parse_bool(net));
  CHECK(parse_bool(L"TRUE") && parse_bool(L"True") && parse_bool(L"yes") && parse_bool(L"1"));
  CHECK(!parse_bool(L"FALSE") && !parse_bool(L"0") && !parse_bool(L""));
  CHECK(format_bool(true, net) == da);
  CHECK(format_bool(false, da) == net);
  CHECK(format_bool(false, L"TRUE") == L"FALSE");
  CHECK(format_bool(true, L"") == L"True");
}

static void test_scale() {
  double acc = 0.0;
  CHECK(mohpcf::scale_axis(7, 1.0F, acc) == 7);
  CHECK(acc == 0.0);
  // 0.5: two single counts make one.
  CHECK(mohpcf::scale_axis(1, 0.5F, acc) == 0);
  CHECK(mohpcf::scale_axis(1, 0.5F, acc) == 1);
  CHECK(acc == 0.0);
  // Sum over many reads equals the exact product (no drift).
  acc = 0.0;
  long long sum = 0;
  for (int i = 0; i < 1000; ++i) {
    sum += mohpcf::scale_axis(3, 0.37F, acc);
  }
  CHECK(sum == 1109 || sum == 1110);  // 3 * 0.37 * 1000 = 1110 (float 0.37 is slightly below).
  // Negative movement scales symmetrically.
  acc = 0.0;
  sum = 0;
  for (int i = 0; i < 1000; ++i) {
    sum += mohpcf::scale_axis(-3, 0.37F, acc);
  }
  CHECK(sum == -1109 || sum == -1110);
  acc = 0.0;
  CHECK(mohpcf::scale_axis(10, 2.5F, acc) == 25);
}

int main() {
  test_hid_paths();
  test_import_slot();
  test_export_lookup();
  test_game_ini_bools();
  test_scale();
  if (g_failures == 0) {
    std::printf("unit tests passed\n");
    return 0;
  }
  std::printf("%d failure(s)\n", g_failures);
  return 1;
}
