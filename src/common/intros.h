#pragma once

// The startup video (EA / Danger Close logos and the legal screen) is
// MOHAGame\CookedMovies\MOHA_Legal_ID_Only[_RUS].vp6. Without the file the game skips it and goes to its
// loading screen and menu. Skipping renames the files to *.vp6.skip; turning it off renames them back.
// Steam's "verify files" also restores them.

#include <Windows.h>

#include <string>

namespace mohpcf::intros {

inline const wchar_t* const kVideos[] = {L"MOHA_Legal_ID_Only.vp6", L"MOHA_Legal_ID_Only_RUS.vp6"};

// game_dir: the folder holding Binaries\ and MOHAGame\.
inline std::wstring movies_dir(const std::wstring& game_dir) {
  return game_dir + L"\\MOHAGame\\CookedMovies\\";
}

inline bool exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline bool skipped(const std::wstring& game_dir) {
  bool any_skipped = false;
  for (const wchar_t* name : kVideos) {
    const std::wstring video = movies_dir(game_dir) + name;
    if (exists(video)) {
      return false;
    }
    any_skipped |= exists(video + L".skip");
  }
  return any_skipped;
}

inline bool set_skipped(const std::wstring& game_dir, bool skip) {
  bool ok = true;
  for (const wchar_t* name : kVideos) {
    const std::wstring video = movies_dir(game_dir) + name;
    const std::wstring hidden = video + L".skip";
    const std::wstring& from = skip ? video : hidden;
    const std::wstring& to = skip ? hidden : video;
    if (exists(from) && !exists(to)) {
      ok = MoveFileExW(from.c_str(), to.c_str(), 0) != FALSE && ok;
    }
  }
  return ok;
}

}  // namespace mohpcf::intros
