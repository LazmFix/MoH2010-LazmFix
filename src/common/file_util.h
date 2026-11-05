#pragma once

#include <string>

namespace mohpcf {

std::wstring module_directory(void* module_handle);
std::wstring process_exe_path();
std::wstring join_path(const std::wstring& left, const std::wstring& right);

}  // namespace mohpcf
