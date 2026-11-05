#pragma once

// The game's own settings files (Documents\EA Games\Medal of Honor\Config\MOHA*.ini), read and written
// the way the game's options menu does it, so the ReShade panel and the game menu see the same values.
//
// MOHAEngine.ini is UTF-16 LE with a BOM; the Win32 profile functions keep that encoding. On a Russian
// install the game writes booleans localized ("Да" / "Нет"); it reads "True"/"False" too. Writes keep
// whatever spelling the file already uses for that key.

#include <Windows.h>
#include <ShlObj.h>

#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <string>

namespace mohpcf::game_ini {

// Last write time of a file (0 if it is missing). The profile functions open and parse the whole file
// on every call (MOHAInput.ini is 120 KB), so the panel re-reads a file only when this changes.
inline std::uint64_t write_time(const std::wstring& file) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) {
    return 0;
  }
  return (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
}

// Re-read check for a file shown in the panel: true on the first call and whenever the file's write time
// changed; looks at the file at most once a second. reset() forces a re-read (after writing it).
struct FileWatch {
  std::uint64_t seen = ~0ULL;
  double checked_at = -10.0;

  bool changed(const std::wstring& file, double now) {
    if (now - checked_at < 1.0) {
      return false;
    }
    checked_at = now;
    const std::uint64_t time = write_time(file);
    if (time == seen) {
      return false;
    }
    seen = time;
    return true;
  }
  void reset() {
    seen = ~0ULL;
    checked_at = -10.0;
  }
};

// Looked up once: the panel asks for it every frame.
inline const std::wstring& config_dir() {
  static const std::wstring dir = [] {
    PWSTR documents = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) && documents != nullptr) {
      path = std::wstring(documents) + L"\\EA Games\\Medal of Honor\\Config";
    }
    CoTaskMemFree(documents);
    return path;
  }();
  return dir;
}

inline std::wstring engine_ini() {
  return config_dir() + L"\\MOHAEngine.ini";
}

inline std::wstring input_ini() {
  return config_dir() + L"\\MOHAInput.ini";
}

inline std::wstring read(const std::wstring& file, const wchar_t* section, const wchar_t* key, const wchar_t* fallback = L"") {
  wchar_t buffer[256]{};
  GetPrivateProfileStringW(section, key, fallback, buffer, 256, file.c_str());
  return buffer;
}

inline bool write(const std::wstring& file, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
  return WritePrivateProfileStringW(section, key, value.c_str(), file.c_str()) != FALSE;
}

// CharLowerBuffW, not towlower: towlower in the "C" locale leaves Cyrillic ("Да") unchanged.
inline std::wstring lower(std::wstring text) {
  if (!text.empty()) {
    CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
  }
  return text;
}

// UE3 GetBool: "True", "Yes", "On", "1" and the localized "True" ("Да").
inline bool parse_bool(const std::wstring& value) {
  const std::wstring v = lower(value);
  return v == L"true" || v == L"yes" || v == L"on" || v == L"1" || v == L"\x0434\x0430";  // да
}

// Spelling for a new boolean value, matching the old one: Да/Нет, TRUE/FALSE, True/False.
inline std::wstring format_bool(bool value, const std::wstring& old) {
  const std::wstring v = lower(old);
  if (v == L"\x0434\x0430" || v == L"\x043d\x0435\x0442") {  // да / нет
    return value ? L"\x0414\x0430" : L"\x041d\x0435\x0442";  // Да / Нет
  }
  if (old == L"TRUE" || old == L"FALSE") {
    return value ? L"TRUE" : L"FALSE";
  }
  return value ? L"True" : L"False";
}

inline bool read_bool(const std::wstring& file, const wchar_t* section, const wchar_t* key, bool fallback) {
  const std::wstring value = read(file, section, key);
  return value.empty() ? fallback : parse_bool(value);
}

inline bool write_bool(const std::wstring& file, const wchar_t* section, const wchar_t* key, bool value) {
  return write(file, section, key, format_bool(value, read(file, section, key)));
}

inline int read_int(const std::wstring& file, const wchar_t* section, const wchar_t* key, int fallback) {
  const std::wstring value = read(file, section, key);
  if (value.empty()) {
    return fallback;
  }
  wchar_t* end = nullptr;
  const double number = std::wcstod(value.c_str(), &end);
  return end == value.c_str() ? fallback : static_cast<int>(number + (number >= 0 ? 0.5 : -0.5));
}

inline bool write_int(const std::wstring& file, const wchar_t* section, const wchar_t* key, int value) {
  return write(file, section, key, std::to_wstring(value));
}

}  // namespace mohpcf::game_ini
