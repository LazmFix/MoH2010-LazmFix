#include "core/hid_guard.h"

#include "common/logger.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <set>
#include <string>

namespace mohpcf {
namespace {

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

struct Slot {
  void** address = nullptr;
  void* genuine = nullptr;  // KernelBase export, read from its export table.
  void* shim = nullptr;
  std::atomic<void*> previous{nullptr};
};

Logger* g_logger = nullptr;
bool g_enabled = false;
std::mutex g_mutex;
int g_depth = 0;
Slot g_w;
Slot g_a;
std::atomic<bool> g_foreign_seen{false};
std::atomic<std::int32_t> g_bypassed{0};
std::atomic<bool> g_logged_first_scope{false};

template <typename CharT>
bool hid_path_impl(const CharT* path) {
  if (path == nullptr) {
    return false;
  }
  // "\\?\hid#..." or "\\.\hid#..."
  if (path[0] != '\\' || path[1] != '\\' || (path[2] != '?' && path[2] != '.') || path[3] != '\\') {
    return false;
  }
  const CharT* rest = path + 4;
  const char* hid = "hid#";
  for (int i = 0; i < 4; ++i) {
    CharT c = rest[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<CharT>(c - 'A' + 'a');
    }
    if (c != static_cast<CharT>(hid[i])) {
      return false;
    }
  }
  return true;
}

std::mutex g_seen_mutex;
std::set<std::string> g_seen_paths;

void note_bypass(const std::string& path) {
  ++g_bypassed;
  if (g_logger == nullptr) {
    return;
  }
  std::lock_guard lock(g_seen_mutex);
  if (g_seen_paths.size() < 128 && g_seen_paths.insert(path).second) {
    g_logger->info("HID device opened past the CreateFile hook: " + path);
  }
}

std::string narrow(const wchar_t* text) {
  if (text == nullptr) {
    return "(null)";
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string out(size > 0 ? size - 1 : 0, '\0');
  if (size > 1) {
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  }
  return out;
}

HANDLE WINAPI shim_create_file_w(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition,
                                 DWORD flags, HANDLE template_file) {
  if (is_hid_device_path(name)) {
    note_bypass(narrow(name));
    return reinterpret_cast<CreateFileWFn>(g_w.genuine)(name, access, share, security, disposition, flags, template_file);
  }
  auto previous = reinterpret_cast<CreateFileWFn>(g_w.previous.load());
  if (previous == nullptr) {
    previous = reinterpret_cast<CreateFileWFn>(g_w.genuine);
  }
  return previous(name, access, share, security, disposition, flags, template_file);
}

HANDLE WINAPI shim_create_file_a(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition,
                                 DWORD flags, HANDLE template_file) {
  if (is_hid_device_path(name)) {
    note_bypass(name);
    return reinterpret_cast<CreateFileAFn>(g_a.genuine)(name, access, share, security, disposition, flags, template_file);
  }
  auto previous = reinterpret_cast<CreateFileAFn>(g_a.previous.load());
  if (previous == nullptr) {
    previous = reinterpret_cast<CreateFileAFn>(g_a.genuine);
  }
  return previous(name, access, share, security, disposition, flags, template_file);
}

bool write_slot(void** address, void* value) {
  DWORD old_protect = 0;
  if (!VirtualProtect(address, sizeof(void*), PAGE_READWRITE, &old_protect)) {
    return false;
  }
  *address = value;
  DWORD ignored = 0;
  VirtualProtect(address, sizeof(void*), old_protect, &ignored);
  return true;
}

std::string hex(const void* pointer) {
  char buffer[32]{};
  std::snprintf(buffer, sizeof(buffer), "0x%p", pointer);
  return buffer;
}

void install(Slot& slot, const char* name) {
  if (slot.address == nullptr || slot.genuine == nullptr) {
    return;
  }
  void* current = *slot.address;
  if (current == slot.shim) {
    return;
  }
  // Genuine = anywhere inside KernelBase or Kernel32 (SecuROM's code lives outside every module).
  const bool foreign = !module_contains(GetModuleHandleW(L"kernelbase.dll"), current) &&
                       !module_contains(GetModuleHandleW(L"kernel32.dll"), current);
  if (!foreign) {
    return;  // Nothing hooks DirectInput's import: nothing to route around.
  }
  if (!g_foreign_seen.exchange(true) && g_logger != nullptr) {
    g_logger->info(std::string("dinput8 ") + name + " import is hooked by " + hex(current) +
                   " (SecuROM): HID device opens will go to KernelBase directly");
  }
  slot.previous.store(current);
  if (!write_slot(slot.address, slot.shim) && g_logger != nullptr) {
    g_logger->error(std::string("Could not redirect the dinput8 ") + name + " import");
  }
}

void restore(Slot& slot) {
  if (slot.address == nullptr) {
    return;
  }
  void* previous = slot.previous.exchange(nullptr);
  if (previous != nullptr && *slot.address == slot.shim) {
    write_slot(slot.address, previous);
  }
}

void setup_slot(Slot& slot, HMODULE real_dinput8, const char* name, void* shim) {
  slot.address = find_import_slot(real_dinput8, name);
  // Not GetProcAddress: SecuROM hooks it and hands out its own CreateFile hooks.
  slot.genuine = find_export(GetModuleHandleW(L"kernelbase.dll"), name);
  slot.shim = shim;
  if (g_logger != nullptr) {
    g_logger->info(std::string("dinput8 ") + name + " import slot " + hex(slot.address) + " -> " +
                   hex(slot.address != nullptr ? *slot.address : nullptr) + " (KernelBase " + hex(slot.genuine) + ")");
  }
}

}  // namespace

bool is_hid_device_path(const wchar_t* path) {
  return hid_path_impl(path);
}

bool module_contains(HMODULE module, const void* address) {
  if (module == nullptr || address == nullptr) {
    return false;
  }
  const auto* base = reinterpret_cast<const std::uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const auto* p = static_cast<const std::uint8_t*>(address);
  return p >= base && p < base + nt->OptionalHeader.SizeOfImage;
}

void* find_export(HMODULE module, const char* function_name) {
  if (module == nullptr) {
    return nullptr;
  }
  auto* base = reinterpret_cast<std::uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return nullptr;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (dir.VirtualAddress == 0) {
    return nullptr;
  }
  const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
  const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
  const auto* ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
  const auto* functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
  for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
    if (std::strcmp(reinterpret_cast<const char*>(base + names[i]), function_name) != 0) {
      continue;
    }
    const DWORD rva = functions[ordinals[i]];
    if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) {
      return nullptr;  // Forwarder string, not code.
    }
    return base + rva;
  }
  return nullptr;
}

bool is_hid_device_path(const char* path) {
  return hid_path_impl(path);
}

void** find_import_slot(HMODULE module, const char* function_name) {
  if (module == nullptr) {
    return nullptr;
  }
  auto* base = reinterpret_cast<std::uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return nullptr;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (dir.VirtualAddress == 0) {
    return nullptr;
  }
  for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name != 0; ++desc) {
    if (desc->OriginalFirstThunk == 0 || desc->FirstThunk == 0) {
      continue;
    }
    const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
    for (; names->u1.AddressOfData != 0; ++names, ++slots) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
        continue;
      }
      const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), function_name) == 0) {
        return reinterpret_cast<void**>(&slots->u1.Function);
      }
    }
  }
  return nullptr;
}

void HidGuard::initialize(HMODULE real_dinput8, Logger* logger, bool enabled) {
  std::lock_guard lock(g_mutex);
  g_logger = logger;
  g_enabled = enabled;
  setup_slot(g_w, real_dinput8, "CreateFileW", reinterpret_cast<void*>(&shim_create_file_w));
  setup_slot(g_a, real_dinput8, "CreateFileA", reinterpret_cast<void*>(&shim_create_file_a));
}

HidGuard::Scope::Scope() {
  std::lock_guard lock(g_mutex);
  if (!g_enabled) {
    return;
  }
  if (g_depth++ == 0) {
    install(g_w, "CreateFileW");
    install(g_a, "CreateFileA");
    if (!g_logged_first_scope.exchange(true) && g_logger != nullptr && !g_foreign_seen.load()) {
      g_logger->info("dinput8 CreateFile imports are not hooked: nothing to route around");
    }
  }
}

HidGuard::Scope::~Scope() {
  std::lock_guard lock(g_mutex);
  if (!g_enabled) {
    return;
  }
  if (--g_depth == 0) {
    restore(g_w);
    restore(g_a);
  }
}

bool HidGuard::enabled() {
  return g_enabled;
}

bool HidGuard::foreign_hook_seen() {
  return g_foreign_seen.load();
}

std::int32_t HidGuard::opens_bypassed() {
  return g_bypassed.load();
}

}  // namespace mohpcf
