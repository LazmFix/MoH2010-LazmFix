#include "common/file_util.h"

#include <Windows.h>

#include <filesystem>

namespace mohpcf {
namespace {

std::wstring path_from_module(HMODULE module) {
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD length = 0;
  while (true) {
    length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size() - 1) {
      buffer.resize(length);
      return buffer;
    }
    buffer.resize(buffer.size() * 2);
  }
}

}  // namespace

std::wstring module_directory(void* module_handle) {
  const auto path = path_from_module(static_cast<HMODULE>(module_handle));
  return std::filesystem::path(path).parent_path().wstring();
}

std::wstring process_exe_path() {
  return path_from_module(nullptr);
}

std::wstring join_path(const std::wstring& left, const std::wstring& right) {
  return (std::filesystem::path(left) / right).wstring();
}

}  // namespace mohpcf
