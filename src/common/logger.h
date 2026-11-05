#pragma once

#include <mutex>
#include <string>

namespace mohpcf {

class Logger {
 public:
  bool open(const std::wstring& path);
  void info(const std::string& message);
  void warn(const std::string& message);
  void error(const std::string& message);
  void close();

 private:
  void write_line(const char* level, const std::string& message);
  void flush_locked();

  std::mutex mutex_;
  void* file_ = nullptr;
  std::string buffer_;
};

}  // namespace mohpcf
