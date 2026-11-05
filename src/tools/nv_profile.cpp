// MoH2010.NvProfile.exe: sets driver anti-aliasing (4x MSAA + sparse grid supersampling) in the NVIDIA
// profile that owns moh.exe, through NvAPI DRS - the same calls NVIDIA Profile Inspector makes.
// Only that profile is changed; the global/base profile is never written. Values the profile had
// before are saved to a backup file next to this exe and "restore" puts them back.
//
//   MoH2010.NvProfile.exe show    [path\to\moh.exe]
//   MoH2010.NvProfile.exe apply   [path\to\moh.exe] [--sgssaa 2|4|8] [--compat 0x080000C1]
//   MoH2010.NvProfile.exe restore [path\to\moh.exe]
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "nvapi.h"

namespace {

FILE* g_log = nullptr;  // MoH2010.NvProfile.log next to the exe: the ReShade panel shows it.

void out(const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::vprintf(format, args);
  va_end(args);
  if (g_log != nullptr) {
    va_start(args, format);
    std::vfprintf(g_log, format, args);
    va_end(args);
    std::fflush(g_log);
  }
}

using QueryInterfaceFn = void*(__cdecl*)(unsigned int);
using InitializeFn = NvAPI_Status(__cdecl*)();
using CreateSessionFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle*);
using DestroySessionFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle);
using LoadSettingsFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle);
using SaveSettingsFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle);
using FindApplicationByNameFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvAPI_UnicodeString, NvDRSProfileHandle*,
                                                       NVDRS_APPLICATION*);
using GetProfileInfoFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_PROFILE*);
using GetSettingFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32, NVDRS_SETTING*);
using SetSettingFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_SETTING*);
using DeleteProfileSettingFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32);
using CreateProfileFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NVDRS_PROFILE*, NvDRSProfileHandle*);
using CreateApplicationFn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_APPLICATION*);

struct Api {
  InitializeFn initialize = nullptr;
  CreateSessionFn create_session = nullptr;
  DestroySessionFn destroy_session = nullptr;
  LoadSettingsFn load_settings = nullptr;
  SaveSettingsFn save_settings = nullptr;
  FindApplicationByNameFn find_application = nullptr;
  GetProfileInfoFn get_profile_info = nullptr;
  GetSettingFn get_setting = nullptr;
  SetSettingFn set_setting = nullptr;
  DeleteProfileSettingFn delete_setting = nullptr;
  CreateProfileFn create_profile = nullptr;
  CreateApplicationFn create_application = nullptr;
};

// Function ids from nvapi_interface.h.
bool load_api(Api& api) {
  HMODULE nvapi = LoadLibraryW(L"nvapi.dll");
  if (!nvapi) return false;
  auto query = reinterpret_cast<QueryInterfaceFn>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
  if (!query) return false;
  auto get = [&](auto& fn, unsigned int id) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(query(id)); };
  get(api.initialize, 0x0150e828);
  get(api.create_session, 0x0694d52e);
  get(api.destroy_session, 0xdad9cff8);
  get(api.load_settings, 0x375dbd6b);
  get(api.save_settings, 0xfcbc7e14);
  get(api.find_application, 0xeee566b2);
  get(api.get_profile_info, 0x61cd6fd6);
  get(api.get_setting, 0x73bf8338);
  get(api.set_setting, 0x577dd202);
  get(api.delete_setting, 0xe4a26362);
  get(api.create_profile, 0xcc176068);
  get(api.create_application, 0x4347a9de);
  return api.initialize && api.create_session && api.destroy_session && api.load_settings && api.save_settings &&
         api.find_application && api.get_profile_info && api.get_setting && api.set_setting && api.delete_setting &&
         api.create_profile && api.create_application;
}

struct Wanted {
  NvU32 id;
  const char* name;
  NvU32 value;
};

constexpr NvU32 kAaCompatDx9 = 0x00D55F7D;
constexpr NvU32 kAaFixDx9 = 0x000858F7;
constexpr NvU32 kAaBehaviorFlags = 0x10ECDB82;
constexpr NvU32 kAaMode = 0x107EFC5B;
constexpr NvU32 kAaSetting = 0x10D773D2;
constexpr NvU32 kAaTransparency = 0x10D48A85;
constexpr NvU32 kLodBiasAuto = 0x00638E8F;
constexpr NvU32 kLodBias = 0x00738E8F;

// Sparse grid count -> {MSAA setting, transparency supersampling value, LOD bias in 1/8 steps}.
bool wanted_settings(int sgssaa, NvU32 compat, std::vector<Wanted>& out) {
  NvU32 msaa = 0, tr = 0;
  int lod_eighths = 0;
  switch (sgssaa) {
    case 2: msaa = 0x0000000E; tr = 0x00000018; lod_eighths = -4; break;  // 2x MSAA (diagonal) + 2x SGSSAA, -0.5
    case 4: msaa = 0x00000010; tr = 0x00000028; lod_eighths = -8; break;  // 4x MSAA + 4x SGSSAA, -1.0
    case 8: msaa = 0x00000025; tr = 0x00000038; lod_eighths = -12; break; // 8x MSAA + 8x SGSSAA, -1.5
    default: return false;
  }
  out = {
      {kAaCompatDx9, "Antialiasing - Compatibility (DX9)", compat},
      {kAaFixDx9, "Antialiasing Fix (DX9) = On", 0},
      {kAaBehaviorFlags, "Antialiasing - Behavior Flags = None", 0},
      {kAaMode, "Antialiasing - Mode = Override", 1},
      {kAaSetting, "Antialiasing - Setting", msaa},
      {kAaTransparency, "Antialiasing - Transparency Supersampling (sparse grid)", tr},
      {kLodBiasAuto, "Texture Filtering - Driver Controlled LOD Bias = Off", 0},
      {kLodBias, "Texture Filtering - LOD Bias (DX)", static_cast<NvU32>(lod_eighths)},
  };
  return true;
}

const NvU32 kShownIds[] = {kAaCompatDx9, kAaFixDx9, kAaBehaviorFlags, kAaMode, kAaSetting, kAaTransparency,
                           kLodBiasAuto, kLodBias};

std::wstring default_game_exe() {
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring dir(self);
  dir.resize(dir.find_last_of(L"\\/") + 1);
  return dir + L"moh.exe";  // installed into Binaries\ next to the game
}

std::wstring backup_path() {
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring path(self);
  path.resize(path.find_last_of(L"\\/") + 1);
  return path + L"MoH2010.NvProfile.backup.txt";
}

void to_unicode(const std::wstring& s, NvAPI_UnicodeString out) {
  std::memset(out, 0, sizeof(NvAPI_UnicodeString));
  for (size_t i = 0; i < s.size() && i + 1 < NVAPI_UNICODE_STRING_MAX; ++i) out[i] = static_cast<NvU16>(s[i]);
}

std::wstring from_unicode(const NvAPI_UnicodeString s) {
  std::wstring out;
  for (size_t i = 0; i < NVAPI_UNICODE_STRING_MAX && s[i]; ++i) out.push_back(static_cast<wchar_t>(s[i]));
  return out;
}

const char* location_name(NVDRS_SETTING_LOCATION loc) {
  switch (loc) {
    case NVDRS_CURRENT_PROFILE_LOCATION: return "game profile";
    case NVDRS_GLOBAL_PROFILE_LOCATION: return "global";
    case NVDRS_BASE_PROFILE_LOCATION: return "base";
    case NVDRS_DEFAULT_PROFILE_LOCATION: return "driver default";
    default: return "?";
  }
}

struct Current {
  bool found = false;           // value exists anywhere (profile, global, default)
  bool in_profile = false;      // set in the game profile itself
  NvU32 value = 0;
};

Current read_setting(const Api& api, NvDRSSessionHandle s, NvDRSProfileHandle p, NvU32 id, const char** where) {
  NVDRS_SETTING setting = {};
  setting.version = NVDRS_SETTING_VER;
  Current c;
  NvAPI_Status st = api.get_setting(s, p, id, &setting);
  if (st == NVAPI_OK) {
    c.found = true;
    c.in_profile = setting.settingLocation == NVDRS_CURRENT_PROFILE_LOCATION;
    c.value = setting.u32CurrentValue;
    if (where) *where = location_name(setting.settingLocation);
  } else if (where) {
    *where = "not set";
  }
  return c;
}

int fail(const char* what, NvAPI_Status st) {
  out("%s failed: NvAPI status %d%s\n", what, static_cast<int>(st),
              st == NVAPI_INVALID_USER_PRIVILEGE ? " (needs administrator rights)" : "");
  return 2;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  {
    std::wstring log_path = backup_path();
    log_path.replace(log_path.size() - std::wstring(L"backup.txt").size(), std::wstring::npos, L"log");
    g_log = _wfopen(log_path.c_str(), L"w");
  }
  if (argc < 2) {
    out("usage: MoH2010.NvProfile.exe show|apply|restore [path\\to\\moh.exe] [--sgssaa 2|4|8] [--compat 0x..]\n");
    return 1;
  }
  std::wstring command = argv[1];
  std::wstring game = default_game_exe();
  int sgssaa = 4;
  NvU32 compat = 0x080000C1;
  for (int i = 2; i < argc; ++i) {
    std::wstring a = argv[i];
    if (a == L"--sgssaa" && i + 1 < argc) sgssaa = _wtoi(argv[++i]);
    else if (a == L"--compat" && i + 1 < argc) compat = static_cast<NvU32>(std::wcstoul(argv[++i], nullptr, 16));
    else game = a;
  }

  Api api;
  if (!load_api(api)) {
    out("NVIDIA driver API (nvapi.dll) is not available\n");
    return 2;
  }
  NvAPI_Status st = api.initialize();
  if (st != NVAPI_OK) return fail("NvAPI_Initialize", st);
  NvDRSSessionHandle session = nullptr;
  if ((st = api.create_session(&session)) != NVAPI_OK) return fail("DRS_CreateSession", st);
  if ((st = api.load_settings(session)) != NVAPI_OK) return fail("DRS_LoadSettings", st);

  NvAPI_UnicodeString app_name;
  to_unicode(game, app_name);
  NvDRSProfileHandle profile = nullptr;
  NVDRS_APPLICATION app = {};
  app.version = NVDRS_APPLICATION_VER;
  st = api.find_application(session, app_name, &profile, &app);
  if (st == NVAPI_EXECUTABLE_NOT_FOUND && command == L"apply") {
    // No driver profile knows moh.exe: make a separate one for it (still not the global profile).
    NVDRS_PROFILE np = {};
    np.version = NVDRS_PROFILE_VER;
    to_unicode(L"Medal of Honor (2010) - MoH2010.PCFix", np.profileName);
    if ((st = api.create_profile(session, &np, &profile)) != NVAPI_OK) return fail("DRS_CreateProfile", st);
    NVDRS_APPLICATION na = {};
    na.version = NVDRS_APPLICATION_VER;
    to_unicode(L"moh.exe", na.appName);
    if ((st = api.create_application(session, profile, &na)) != NVAPI_OK) return fail("DRS_CreateApplication", st);
    out("created profile \"Medal of Honor (2010) - MoH2010.PCFix\" for moh.exe\n");
  } else if (st != NVAPI_OK) {
    return fail("DRS_FindApplicationByName", st);
  }

  NVDRS_PROFILE info = {};
  info.version = NVDRS_PROFILE_VER;
  if ((st = api.get_profile_info(session, profile, &info)) != NVAPI_OK) return fail("DRS_GetProfileInfo", st);
  std::wstring profile_name = from_unicode(info.profileName);
  out("profile: %ls (%s, %u apps, %u settings)\n", profile_name.c_str(),
              info.isPredefined ? "predefined" : "user", info.numOfApps, info.numOfSettings);

  auto show = [&]() {
    std::vector<Wanted> names;
    wanted_settings(4, compat, names);
    for (const Wanted& w : names) {
      const char* where = nullptr;
      Current c = read_setting(api, session, profile, w.id, &where);
      out("  0x%08X  %-58s %s%08X  (%s)\n", w.id, w.name, c.found ? "0x" : "  ", c.found ? c.value : 0, where);
    }
  };

  if (command == L"show") {
    show();
  } else if (command == L"apply") {
    std::vector<Wanted> wanted;
    if (!wanted_settings(sgssaa, compat, wanted)) {
      out("--sgssaa must be 2, 4 or 8\n");
      return 1;
    }
    // Back up what the game profile had, once (the first apply keeps the original state).
    std::wstring backup = backup_path();
    if (GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES) {
      std::ofstream file(backup);
      file << "# profile values before MoH2010.NvProfile apply: id in_profile value\n";
      for (NvU32 id : kShownIds) {
        Current c = read_setting(api, session, profile, id, nullptr);
        char line[64];
        std::snprintf(line, sizeof line, "%08X %d %08X\n", id, c.in_profile ? 1 : 0, c.value);
        file << line;
      }
      out("backup: %ls\n", backup.c_str());
    }
    for (const Wanted& w : wanted) {
      NVDRS_SETTING s = {};
      s.version = NVDRS_SETTING_VER;
      s.settingId = w.id;
      s.settingType = NVDRS_DWORD_TYPE;
      s.u32CurrentValue = w.value;
      if ((st = api.set_setting(session, profile, &s)) != NVAPI_OK) return fail(w.name, st);
    }
    if ((st = api.save_settings(session)) != NVAPI_OK) return fail("DRS_SaveSettings", st);
    out("applied %dx MSAA + %dx sparse grid supersampling:\n", sgssaa, sgssaa);
    api.load_settings(session);
    show();
  } else if (command == L"restore") {
    std::ifstream in(backup_path());
    if (!in) {
      out("no backup file, nothing to restore\n");
      return 1;
    }
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::istringstream ls(line);
      std::string id_hex, value_hex;
      int in_profile = 0;
      ls >> id_hex >> in_profile >> value_hex;
      NvU32 id = static_cast<NvU32>(std::strtoul(id_hex.c_str(), nullptr, 16));
      NvU32 value = static_cast<NvU32>(std::strtoul(value_hex.c_str(), nullptr, 16));
      if (in_profile) {
        NVDRS_SETTING s = {};
        s.version = NVDRS_SETTING_VER;
        s.settingId = id;
        s.settingType = NVDRS_DWORD_TYPE;
        s.u32CurrentValue = value;
        st = api.set_setting(session, profile, &s);
      } else {
        st = api.delete_setting(session, profile, id);
        if (st == NVAPI_SETTING_NOT_FOUND) st = NVAPI_OK;
      }
      if (st != NVAPI_OK) return fail("restore setting", st);
    }
    if ((st = api.save_settings(session)) != NVAPI_OK) return fail("DRS_SaveSettings", st);
    in.close();
    DeleteFileW(backup_path().c_str());
    out("restored:\n");
    api.load_settings(session);
    show();
  } else {
    out("unknown command\n");
    return 1;
  }
  api.destroy_session(session);
  return 0;
}
