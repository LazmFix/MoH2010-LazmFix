# RE notes: startup hang

Date: 2026-11-04. PC: i9-14900K (24C/32T), RTX 4080 SUPER, Windows 11, Razer Basilisk V3 (VID 1532 PID 0099).

## moh.exe

- PE32, 0x14C, timestamp 2010-10-26, FileVersion 1.0.4.0, LargeAddressAware.
- Sections `.securom`, `Stext`/`Sdata` (entropy 8.0): SecuROM-wrapped, code decrypted at run time.
- Static imports include `DINPUT8.dll!DirectInput8Create`, `d3d9.dll!Direct3DCreate9`, d3dx9_42/d3dx10_42,
  XINPUT1_3. No `RegisterRawInputDevices` / `GetRawInputData`.
- Must be started through Steam: direct start shows "Application load error 5:0000065434".
- Steam shows the CD key dialog (`ShowCDKey`) on every launch and waits for a click.

## Symptom

Process alive, window `SplashScreen`, ~90 threads, CPU idle. Config generation in
`Documents\EA Games\Medal of Honor\Config` stops after MOHAEngine/MOHAInput/... (MOHAGame.ini is never
written) — same set of files as the failed attempt of 2024-12-23.

## Stack (cdb -pv, non-invasive)

```text
0:000> ~0 kb
034fb4d8 763716ee 0000121c 00000000 00000000 ntdll!NtReadFile+0xc
034fb53c 01df4577 0000121c fd6a0000 00000008 KERNELBASE!ReadFile+0x5e        <- 8 bytes
034fb7b8 01dae009 0000121c ...                0x1df4577                       <- SecuROM
034fc8a4 5cec84a1 393c0abc c0000000 00000003 0x1dae009                       <- CreateFileW hook
034fcb64 5cec7db3 ...                         DINPUT8!DIHid_GetDevInfo+0x14a
...                                           DINPUT8!DIHid_BuildHidList / DIBus_BuildList / Excl_Init
...                                           DINPUT8!DirectInput8Create+0x141
...                                           gameoverlayrenderer (Steam overlay hook)
0:000> du 393c0abc
"\\?\hid#vid_1532&pid_0099&mi_03#8&16d63b97&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"
0:000> dps DINPUT8!_imp__CreateFileW L1
5cee2184  01dac5c8                      <- not KernelBase: SecuROM
0:000> dps DINPUT8!_imp__ReadFile L1
5cee2180  01db2faa                      <- SecuROM
KERNELBASE!CreateFileW: mov edi,edi / push ebp ...   <- KernelBase itself is not patched
```

The SecuROM code lives right after the image (0x01D9B000+, outside the module list). The hook is an
IAT patch per module, KernelBase is clean — so calling `KernelBase!CreateFileW` directly skips it.

After `CancelSynchronousIo` on the main thread: next hang in `DINPUT8!CDIObj_EnumDevicesW`
(`DI8DEVCLASS_GAMECTRL`) on the same device (handle 0x128c); after a second cancel the game reached the
EA intro and kept running.

## Why only some HID collections hang

Mouse/keyboard top-level collections are opened exclusively by Windows: DirectInput's `CreateFileW`
fails and SecuROM has no handle to read. Vendor and consumer collections open fine; the 8-byte read
then waits for the next input report. Razer's MI_03 interface never sends one.

## SecuROM also hooks GetProcAddress

First version of the fix compared the import slot with `GetProcAddress(kernel32, "CreateFileW")` to
decide "hooked or not". In the game that call returns SecuROM's hook (0x01DAC5C8, the same value as the
slot), so the fix thought nothing was hooked and the game hung again. Now: the genuine function is read
from KernelBase's export table directly, and "foreign" means "outside the KernelBase / Kernel32 images".

## Mouse path (seen in game, 2026-11-04)

```text
DirectInput8Create(version 0x0800, Unicode)          (two DirectInput objects, both through the proxy)
CreateDevice(GUID_SysMouse)
SetDataFormat: 16 bytes, 7 objects, flags 0x2 (DIDF_RELAXIS)  -> c_dfDIMouse (X at 0, Y at 4)
SetProperty(DIPROP_BUFFERSIZE) = 1023
SetCooperativeLevel 0x6 = DISCL_NONEXCLUSIVE | DISCL_FOREGROUND
GetDeviceData (buffered events)
```

While the window is in the background the game calls SetCooperativeLevel/Acquire every frame (~18 ms).
Buffered mode: each report gives an X and a Y event, 1023 events ≈ 0.5 s at 1000 Hz, read every frame,
so nothing overflows at normal frame rates (the core counts DI_BUFFEROVERFLOW).
ReShade 6.8.0 also hooks dinput8 (its `d3d9!DirectInput8Create` sits between the proxy and the system
DLL) and logs two harmless `CreateDevice failed with E_FAIL` of its own.
`MOHAInput.ini [Engine.PlayerInput]`: `MouseSensitivity=35.0`, `bEnableMouseSmoothing=true` (UE3
SmoothMouse; set to false by the installer). `bUseViewAcceleration` and the yaw/pitch speed and
acceleration keys look like gamepad stick settings (aTurn/aLookUp from XboxTypeS_RightX/Y); whether they
touch the mouse is still to be checked in game.

## HUD scale at 4K (2026-11-04)

What scales and what does not (3840x2160, borderless):

| Element | Drawn by | At 4K |
|---|---|---|
| Menus, pause menu, objective banner, hints, ammo | Scaleform movies | scaled correctly |
| Subtitles, crosshair (4 ticks), world markers ("Mother", "6 m" edge arrow) | native HUD text/canvas | 720p pixel sizes (subtitles ~13 px) |

Findings (tools/re/upk_*.py on zlib-decompressed MOHAGame.upk / Engine.upk):

- `Default__MOHAHUDBase`: `ResolutionScale_640x480=0.667 … _960x720=1.0, _1280x720=1.0, _1280x1024=1.422,
  _1600x1200=1.667` = ResY/720, and `Small/Medium/LargeTextFontSize = 16/20/26` (720p pixels). No entry
  for 1080p or 4K. These are `Config(Game)` properties.
- MoH's `DefaultGame.ini`, `DefaultPlayer.ini`, `DefaultWeapon*.ini`, `DefaultAI.ini` are encrypted
  (`SDF` header); the game never writes a user `MOHAGame.ini`. Creating one in Documents makes the game
  use it *instead of* the encrypted defaults: it hangs on the loading screen (tested, file removed).
- Setting every `ResolutionScale_*` copy in memory (23 copies: class defaults + instances) to 3.0 before
  loading a level changed nothing visible: the subtitles / crosshair / markers do not use that table.
- `[Engine.Engine] SubtitleFontSize` / `SubtitleFontSizeLowRez` are `Const | Localized` (flags 0x8002):
  not read from MOHAEngine.ini. Adding `[Engine] SubtitleFontSize=48` to Engine.rus changed nothing either
  (restored). `SubtitleFontName` *is* read (UI_Fonts.UtilityOT_30DS gave boxes, same height): the text
  height is set elsewhere.
- Candidates for a real fix: `MOHAHUDRichText.bAutoScaleForResolution`, `MOHAHUDObj.bScaleWithResolution`,
  `MOHAHUDRichText.ScaleIcons(iconScale)` (archetype objects in the packages, not config), or the native
  draw code (SecuROM-encrypted on disk, decrypted in memory at run time).

## Keyboard layout in hints

Hints name keys with the active keyboard layout ("Используйте цфыв для перемещения" on a Russian
layout). The core switches the game to US English once on the game thread (LoadKeyboardLayout
00000409 + KLF_SETFORPROCESS) when the game sets up its mouse: "Используйте WASD для перемещения"
(tested). DirectInput controls use scan codes and are not affected.

### Subtitle size: fixed (2026-11-04)

The HUD instance already has `resolutionScale = 3.0` and `CurrentScreenResolution = 3840x2160` at 4K
(the banner / hints use it); the subtitles do not. They take their size from two floats in the engine
object right after `SubtitleFontName` (FString -> L"$DefaultFont", len 13): **21.0 and 21.75** pixels
(= SubtitleFontSizeLowRez / SubtitleFontSize as set at run time, made for 720p). The engine object
layout there: Tiny/Small/Medium/LargeFontName FStrings, SubtitleFontName, then the two sizes, then
AdditionalFontNames (TArray). Two copies: Default__GameEngine and the live GameEngine.

Writing 63 / 65.25 into both copies made the subtitles 3x at once, also in a running level and after
a checkpoint reload. `src/core/subtitles.cpp` does that: it scans the game's private read-write memory
(ReadProcessMemory on itself, 1 MB pieces) for `{ptr, 13, 13, 21.0f, 21.75f}` with ptr -> "$DefaultFont",
writes 21 * s / 21.75 * s (s = render height / 720, or the panel's value) and re-applies every second.
Found at start-up within ~2 s of the main menu (log: "Subtitle size: 2 engine field(s) found").

Still unscaled: crosshair (not a package object or property; likely inside the HUD Scaleform movie) and
world markers ("Mother" label + icon, edge arrows). Candidates for the markers: rich-text objects with
`$DefaultFont` followed by font sizes 20 / 20 (15 copies in a level); tripling them could not be
checked yet (the marker was out of view by the time the write finished). tools/re/memtool.ps1 and
fstring_refs.ps1 do these experiments.

## Mouse sensitivity measured (2026-11-04)

Camera rotator of the player (Location X/Y/Z floats, then Pitch/Yaw/Roll int32) found with
tools/re/yaw_finder.ps1 at 0x034FD754 (same address in two runs). Calibration hotkeys in the core
(Ctrl+Shift+F10 / F9 / F8 = 1000 counts right in portions of 200 / of 2 per read / down) inject
counts into the game's buffered GetDeviceData result, after the fix multiplier.

| Run | Multiplier | Counts the game got | Change |
|---|---|---|---|
| F10 | 0.627 | 627 | Yaw +10972 (twice) |
| F10 | 0.6866 (MWR 3) | 686.6 | Yaw +12004 |
| F9 (2 per read) | 0.6866 | 686.6 | Yaw +12024 |
| F8 | 0.6866 | 686.6 | Pitch -12008 |

=> 0.5000 units per count per MouseSensitivity unit, linear, same for pitch (MouseSensitivity 35).
MWR 3 = 0.066 deg = 12.015 units per count => multiplier 0.6866 at MouseSensitivity 35.

## World markers / crosshair: experiments so far

- Tripling the 15 rich-text objects `{"$DefaultFont", 20, 20}`: no visible change of the "Mother" label.
- Tripling HUD Small/Large font sizes (16, 26 after "$DefaultFont" in MOHAHUDBase instances): could not be
  judged, the marker left the view (scripted squad movement) before the screenshot.
- tools/re/scale_text.ps1 (-Size, -Factor, -Single) does these writes in one pass.

## FOV (2026-11-04)

- `Default__MOHAPlayerCamera.DefaultFOV = 70` (MOHAGame), `Default__Camera` / MP camera: 90.
- In memory: Camera.DefaultFOV at object + 0x244, DefaultAspectRatio (4/3) 16 bytes later. UObject header:
  +0 vtable (in moh.exe), +4 index into GObjObjects, +0x34 Class, +0x38 ObjectArchetype (live camera ->
  class default). GObjObjects is a TArray {Data, Num, Max} in moh.exe's writable data; found by looking for
  Data[index] == object.
- Writing 90 into the live camera: wider view at once; CameraCache.POV.FOV follows. Mouse: +12030 units per
  686.6 counts at FOV 90 (12015 expected) -> UE3 FOVScale does not apply to the mouse here.
- src/core/fov.cpp keeps every object of that class at the setting (only values 70 or the previous setting).

## Crosshair and marker text (2026-11-04)

- `MOHAHUDCursor.DefaultCursorData`: 4 ticks, PixelStart/PixelSize (-2,-8,4,8), (-2,0,4,8), (-8,-2,8,4),
  (0,-2,8,4), UV0 (504,498)..., bApplyScale = 0. All other cursor sets (RedDot, Shotgun, Cannon, ...)
  have bApplyScale = 1. In memory the struct is 80 bytes, bApplyScale at +68. Setting it to 1 (both copies:
  class default + HUD instance) makes the ticks and their spread 3x at 4K (tested).
- Marker / name tag labels: `MOHAHUDRichText3DSPObjective` / `...SPSquadmate`, text like
  `[$NPC_NEPTUNE_IG][n]Voodoo` (icon markup + name). The font size is the rich text {"$DefaultFont", 20, 20}
  pair. Patching the 5 copies found at the main menu (templates) makes every label created later 3x;
  patching only the 15 live copies in a level changed the existing ones, new ones came back at 20.
- MOHAHUDBase Large/Medium/Small = 26/20/16 (three {FString, size} entries in a row): tripling them changed
  nothing visible on markers.
- The icons next to the labels (trident) keep their size.
- tools/re: cursor_scale.ps1, hud_fonts.ps1, font_sizes.ps1, find_wstr.ps1, find_ptr.ps1, fov_scan.ps1,
  shot.ps1 (screenshot only while moh.exe is in front).

## NVIDIA profile (2026-11-04)

- Driver profile "Medal of Honor" (predefined, moh.exe) has `AA Behavior Flags = 0x1` (treat Override as
  application-controlled), so an Override set elsewhere did nothing.
- NvAPI DRS: reading works without admin; DRS_SetSetting returns NVAPI_INVALID_USER_PRIVILEGE (-137)
  without it. MoH2010.NvProfile.exe (src/tools/nv_profile.cpp, headers from github.com/NVIDIA/nvapi, MIT)
  runs elevated from the panel.

## Aiming down sights (2026-11-04)

- MoH: 1000 counts turn 12022 units from the hip and 9017 aimed (red dot), at hip FOV 70 and 80.7
  alike: a fixed x0.75. The aim FOV is 50 whatever the hip FOV.
- Zoom block in the player's actor at object + 0xDCC (object found via GObjObjects; class 0x435A6000 /
  0x43AF6000 in two sessions): +0 aim multiplier 0.75, +4 current FOV (eases in ~150 ms; sways by up to
  ~0.6 deg at the hip), +8 FOV it eases from, +12 FOV it eases to (aim FOV while aiming, hip otherwise),
  +16 weapon model FOV (57 -> 30). The block is zero/1.0 until the first aim in a level.
- Writing +12 while aiming (every mouse read, game thread) makes the view ease to that FOV and stay.
- The actor's class changes from level to level (another soldier): the first build only looked at the
  first level's class again and never found the block after a level change. The search now looks at every
  object; the write is checked on the game thread (object still in GObjObjects at its index, same class)
  right before it happens.

## Crash 2026-11-05 00:42 (R6025 pure virtual call, then "General protection fault! History:")

- WER dump (LocalAppData\CrashDumps\moh.exe.21180.dmp): main thread only, moh.exe frames + msvcr90
  (_purecall path); no frames of dinput8.dll (the fix), the add-on or ReShade's d3d9.dll. 20 s after a
  checkpoint save (MOHASAVEDGAME 00:41:53). The fix's ADS writes were not running at the time (block not
  found since the first level change, see above); the FOV writes check class, vtable, value and aspect.
  Read as a game crash; tools: a minidump parser in the session scratchpad (stack scan for module addresses).
- Camera.CameraCache.POV.FOV (camera + 0x3AC / 0x3D0) does not change while aiming.
- MWR (h1_sp64_ship.exe, RVAs from MWR-RawMouseFix): `cl+0x48` = RVA 0x252B1D8 is the zoom multiplier:
  1.0 at the hip, M21 scope 0.2067 = tan(7.5) / tan(32.5) (passes 0.3956 while the scope comes up).
  sensitivity dvar ptr at RVA 0x252AF08, m_yaw at 0x252AF30 (value at ptr + 0x10).
- MWR config: dvar names are hashed (h1-mod generate_hash: h = 0xB3CB2E29 * (lower(c0) ^ 0x319712C3),
  then h = 0xB3CB2E29 * (h ^ lower(c)) for each next char including the terminating 0).
  `players2/config.cfg`: sensitivity 3, m_yaw 0.022, no cg_fov (default 65); `config_mp.cfg`: cg_fov 65.
- tools/re: input.ps1 (SendInput keys / mouse / RMB / front), peek.ps1 (sample floats, RMB at sample n),
  peekx.ps1 (any process, RVA + dvar pointers), yaw_finder.ps1 -Tolerance (fractional carry makes two
  1000-count turns differ by a few units).

## ReShade menu frame rate (2026-11-04)

- With the fix's add-on tab open: 36 FPS (Home tab of the same menu: 268). Display panel called
  EnumDisplaySettings for every mode each frame and re-read MOHAEngine.ini / MOHAInput.ini every second.
  Cached / re-read on write time: 250 FPS with the tab open.

## 0.9.0 beta: startup display, ultrawide (2026-11-05)

- The proxy's DllMain runs before the engine reads its settings (moh.exe imports DINPUT8.dll): it
  writes MOHAEngine.ini ResX/ResY = the desktop mode (EnumDisplaySettings, physical pixels) and
  Fullscreen, renames the intro videos, and installs a thread CBT hook (WH_CBT) on the main thread.
  Only Win32 file / registry / window calls there (Documents through the registry, not the shell).
- The game window is class "LaunchUnrealUWindowsClient" (created 1920x1080, style 0x00CA0000). The
  hook makes it a monitor-sized popup at creation. The game sets its own window procedure right after
  creating the window (0x00F9A450 in moh.exe), replacing the subclass: the hook subclasses again on
  HCBT_MOVESIZE / HCBT_ACTIVATE, and the mouse reads check it too.
- The game's own windowed mode (Fullscreen=False) at ResX = desktop size sizes a framed window that does
  not fit the screen: the back buffer came out 3671x2065 at 3840x2160, stretched, and ReShade's overlay
  stopped drawing. Borderless therefore keeps Fullscreen=True and the ReShade add-on turns the
  fullscreen request into a window (back buffer 3840x2160, no display mode change).
- The game is Vert- (Camera.DefaultFOV is horizontal whatever the aspect). At 3440x1440 the fix writes
  2*atan(tan(FOV/2) * aspect / (16/9)) (80.7 -> 97.6): same vertical framing as 16:9 (checked side by
  side), more at the sides. The weapon aim FOV gets the same conversion.
- Removed for the release: the polling rate meter (second DirectInput mouse), the info overlay, the
  calibration hotkeys (injected counts) and the Ctrl+End / Ctrl+PgUp hotkeys.
