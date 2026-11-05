# MoH 2010 PC Fix

A fix and quality-of-life mod for the **Medal of Honor (2010) single-player campaign** on PC: the
startup hang, borderless window at the desktop resolution, ultrawide support, 4K HUD and subtitles,
mouse, field of view and more, with a settings panel in ReShade.

**[English](#english)** · **[Русский](#русский)**

> **Beta.** I made this mostly for myself and finished the campaign once with it without running into
> serious problems. Other systems may behave differently: please report what you find in
> [Issues](../../issues).

---

## English

### Features

- **Startup hang fixed.** The game hangs forever on the splash screen with some mice and keyboards
  (for example Razer devices). The SecuROM DRM inside `moh.exe` reads from every device DirectInput
  opens, and a device interface that never sends data blocks that read. The mod opens those devices past
  the SecuROM hook. The DRM itself is not touched.
- **Borderless window at the desktop resolution and refresh rate**, from the first frame: no display mode
  change at startup and no Windows taskbar showing through. Alt+Tab is instant. Exclusive fullscreen
  (at the monitor's highest refresh rate) and windowed mode are options.
- **Any aspect ratio.** 16:9, 16:10, 21:9 and 32:9: the view gets wider on wide screens (Hor+) instead of
  being cut at the top and bottom, aiming zoom included.
- **Intro videos skipped**, straight to the main menu.
- **HUD at high resolutions.** The game draws subtitles, objective markers, squadmate names and the
  crosshair in 720p pixels (tiny at 1440p and 4K). The mod scales them to the resolution.
- **Field of view.** The game uses 70° (at 16:9), like on consoles. Set any value, or the CoD4 / MW
  Remastered equivalent (cg_fov 65 = 80.7°).
- **Graphics.** The game's hardware detection files current graphics cards under "Minspec" (no dynamic
  shadows, no depth of field, 1x texture filtering). On its first start the mod applies the game's own
  "High end" preset with 16x filtering and sharper shadows. The frame rate cap (62 FPS in the game) is
  set to your monitor's refresh rate.
- **Mouse.** The game reads the mouse raw (DirectInput). The mod turns off the game's mouse smoothing on
  its first start. Optional: a sensitivity multiplier, the game's slow-movement curve off, aim assist off.
- **CoD4 / MW Remastered preset** (off by default): the same turn per mouse count as MWR at your MWR
  sensitivity, the view of cg_fov 65, and aiming like MWR (sensitivity follows the zoom, MWR-sized zoom).
- **Key names in English** in the game's hints ("WASD", not "ЦФЫВ" with a Russian layout).
- **Anti-aliasing on NVIDIA.** The game's own AA option does nothing in the campaign. One click sets
  MSAA + sparse grid supersampling in the driver profile of the game (only that profile).
- **Settings panel** in ReShade (Home → Add-ons → MoH 2010 PC Fix), in English and Russian.

The multiplayer is a separate program and is not affected.

### Requirements

- Medal of Honor (2010), **Steam** version (`Binaries\moh.exe` 1.0.4.0). Other versions were not tested.
- [ReShade](https://reshade.me) **with full add-on support**, 6.x (for the settings panel and the
  borderless window). Without ReShade the fixes still work with the settings from the ini file, and the
  game runs in exclusive fullscreen at the desktop resolution instead of borderless.

### Installation

1. Close the game. Find its folder: Steam → Medal of Honor → right click → Manage → Browse local files.
   You see `Binaries`, `MOHAGame` and others.
2. Install ReShade:
   1. Download **"ReShade with full add-on support"** from [reshade.me](https://reshade.me).
   2. Run it, choose `Binaries\moh.exe` in the game folder.
   3. Choose **Direct3D 9**.
   4. Effects are optional: you can skip them all.
3. Download `MoH2010-PCFix-<version>.zip` from [Releases](../../releases) and extract it **into the game
   folder** (the one with `Binaries`). Four files go to `Binaries\`: `dinput8.dll`,
   `MoH2010.PCFix.addon32`, `MoH2010.PCFix.ini`, `MoH2010.NvProfile.exe`.
4. Start the game from Steam. Steam may show the CD key window: close it. If you never started the game
   before, start it a second time: the game creates its settings files on its first start, and the mod
   applies its display and graphics settings from then on.
5. Press **Home** in the game: ReShade's menu → **Add-ons** → **MoH 2010 PC Fix**. Hover over **(?)**
   for an explanation of each setting. Switch the language at the top right.

### Uninstall

1. In the panel: Display → Anti-aliasing → **Undo**, if you used it (puts the NVIDIA profile back).
2. Delete from `Binaries\`: `dinput8.dll`, `MoH2010.PCFix.addon32`, `MoH2010.PCFix.ini`,
   `MoH2010.PCFix.log`, `MoH2010.NvProfile.exe` (and `MoH2010.NvProfile.*` files if present).
3. Intro videos: in Steam, Properties → Installed Files → **Verify integrity of game files**, or rename
   `MOHAGame\CookedMovies\*.vp6.skip` back to `*.vp6`.
4. ReShade: run its installer again and choose Uninstall.

The game's settings (`Documents\EA Games\Medal of Honor\Config`) keep the graphics and mouse values the
mod set; change them in the game menu or the panel.

### Known issues and limits

- The aiming data is found the first time you aim in each level (within a quarter of a second): the
  very first aim after a load may use the game's own aiming.
- The CoD4 / MWR aiming was checked with rifle sights; scoped rifles were not checked separately.
- Ultrawide was checked at 3440x1440 in a window on a 16:9 monitor. Pre-rendered videos stay 16:9.
- The game itself crashed once in about ten hours ("pure virtual function call", inside the engine,
  right after a checkpoint save). The log and crash dump did not point at the mod.
- Settings file and log: `Binaries\MoH2010.PCFix.ini`, `Binaries\MoH2010.PCFix.log`. Please attach the
  log to bug reports.

### Tested on

| | |
|---|---|
| Game | Medal of Honor (2010), Steam, `moh.exe` 1.0.4.0, Russian language; campaign completed once |
| OS | Windows 11 Pro (build 26300) |
| CPU / RAM | Intel Core i9-14900K, 64 GB |
| GPU | NVIDIA GeForce RTX 4080 SUPER, driver 621.01 |
| Monitor | MSI MPG322UX OLED, 3840x2160 at 240 Hz |
| Mouse | Razer Basilisk V3 (the device that made the game hang) |
| ReShade | 6.8.0 with add-on support |

### Building from source

Visual Studio 2022 (C++ x86 tools) and CMake. `tools\build.ps1` builds and runs the tests,
`tools\package.ps1` makes the release zip. Technical notes: [docs/RE_NOTES.md](docs/RE_NOTES.md),
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) (Russian).

License: MIT ([LICENSE](LICENSE)). Medal of Honor is a trademark of Electronic Arts; this project is not
affiliated with EA.

---

## Русский

> **Бета-версия.** Делал больше для себя. Прошёл с модом кампанию один раз и серьёзных проблем не
> встретил. На других системах возможны отличия: пишите о найденном в [Issues](../../issues).

### Возможности

- **Исправлено зависание при запуске.** С некоторыми мышами и клавиатурами (например, Razer) игра вечно
  висит на заставке. Защита SecuROM внутри `moh.exe` читает из каждого устройства, которое открывает
  DirectInput, а интерфейс устройства, который никогда не шлёт данных, блокирует это чтение. Мод
  открывает такие устройства в обход перехвата SecuROM. Сама защита не меняется.
- **Окно без рамки с разрешением и частотой рабочего стола** с первого кадра: экран не переключается при
  запуске, панель задач Windows не просвечивает, Alt+Tab мгновенный. Можно выбрать эксклюзивный полный
  экран (с максимальной частотой монитора) или окно.
- **Любое соотношение сторон.** 16:9, 16:10, 21:9 и 32:9: на широких экранах обзор расширяется по бокам
  (Hor+), а не обрезается сверху и снизу. Зум прицела тоже.
- **Заставки пропускаются**, игра сразу открывает главное меню.
- **Интерфейс в высоком разрешении.** Игра рисует субтитры, метки заданий, имена напарников и прицел в
  пикселях 720p (в 1440p и 4K они мелкие). Мод масштабирует их под разрешение.
- **Поле зрения (FOV).** В игре 70° (при 16:9), как на консолях. Можно поставить любое значение или как
  в CoD4 / MW Remastered (cg_fov 65 = 80.7°).
- **Графика.** Определение железа игры записывает современные видеокарты в «Minspec» (без динамических
  теней, без глубины резкости, фильтрация текстур 1x). При первом запуске мод ставит пресет самой игры
  «High end» с фильтрацией 16x и более чёткими тенями. Ограничение FPS (в игре 62) — по частоте
  монитора.
- **Мышь.** Игра читает мышь напрямую (DirectInput). При первом запуске мод выключает сглаживание мыши
  игры. По желанию: множитель чувствительности, без кривой медленных движений, без помощи в прицеливании.
- **Пресет CoD4 / MW Remastered** (по умолчанию выключен): тот же поворот на отсчёт мыши, что в MWR при
  вашей чувствительности MWR, обзор как cg_fov 65, прицеливание как в MWR (замедление мыши следует за
  зумом, зум как в MWR).
- **Названия клавиш на английском** в подсказках игры («WASD», а не «ЦФЫВ» при русской раскладке).
- **Сглаживание на NVIDIA.** Своё сглаживание игры в кампании не работает. Одна кнопка включает MSAA +
  суперсэмплинг (SGSSAA) в профиле драйвера этой игры (только в нём).
- **Панель настроек** в ReShade (Home → Дополнения → MoH 2010 PC Fix), на русском и английском.

Мультиплеер — отдельная программа, мод его не затрагивает.

### Что нужно

- Medal of Honor (2010), версия **Steam** (`Binaries\moh.exe` 1.0.4.0). Другие версии не проверялись.
- [ReShade](https://reshade.me) **с полной поддержкой дополнений (add-on support)**, 6.x — для панели
  настроек и окна без рамки. Без ReShade исправления работают с настройками из ini-файла, а игра
  запускается в эксклюзивном полном экране с разрешением рабочего стола вместо окна без рамки.

### Установка

1. Закройте игру. Откройте её папку: Steam → Medal of Honor → правый клик → Управление → Просмотреть
   локальные файлы. В ней есть `Binaries`, `MOHAGame` и другие.
2. Установите ReShade:
   1. Скачайте **«ReShade with full add-on support»** с [reshade.me](https://reshade.me).
   2. Запустите установщик и выберите `Binaries\moh.exe` в папке игры.
   3. Выберите **Direct3D 9**.
   4. Эффекты не обязательны, их можно пропустить.
3. Скачайте `MoH2010-PCFix-<версия>.zip` в [Releases](../../releases) и распакуйте **в папку игры** (ту,
   где `Binaries`). В `Binaries\` попадут четыре файла: `dinput8.dll`, `MoH2010.PCFix.addon32`,
   `MoH2010.PCFix.ini`, `MoH2010.NvProfile.exe`.
4. Запустите игру из Steam. Steam может показать окно с CD-ключом — закройте его. Если вы ни разу не
   запускали игру, запустите её второй раз: файлы настроек игра создаёт при первом запуске, а настройки
   экрана и графики мод применяет начиная со следующего.
5. В игре нажмите **Home**: меню ReShade → **Дополнения** → **MoH 2010 PC Fix**. Наведите на **(?)** —
   появится пояснение к настройке. Язык переключается справа вверху.

### Удаление

1. В панели: Экран → Сглаживание → **Отменить**, если пользовались (вернёт профиль NVIDIA).
2. Удалите из `Binaries\`: `dinput8.dll`, `MoH2010.PCFix.addon32`, `MoH2010.PCFix.ini`,
   `MoH2010.PCFix.log`, `MoH2010.NvProfile.exe` (и файлы `MoH2010.NvProfile.*`, если есть).
3. Заставки: в Steam — Свойства → Установленные файлы → **Проверить целостность файлов игры**, или
   переименуйте `MOHAGame\CookedMovies\*.vp6.skip` обратно в `*.vp6`.
4. ReShade: снова запустите его установщик и выберите удаление.

Настройки игры (`Документы\EA Games\Medal of Honor\Config`) сохранят значения графики и мыши, которые
поставил мод; их можно поменять в меню игры или в панели.

### Известные ограничения

- Данные прицела находятся при первом прицеливании в каждом уровне (за четверть секунды): самое первое
  прицеливание после загрузки может пройти по правилам игры.
- Прицеливание как в CoD4 / MWR проверено на прицелах винтовок; снайперские прицелы отдельно не
  проверялись.
- Ультраширокий экран проверен в окне 3440x1440 на мониторе 16:9. Видеоролики остаются 16:9.
- Сама игра один раз вылетела примерно за десять часов («pure virtual function call», внутри движка,
  сразу после сохранения на контрольной точке). Лог и дамп падения на мод не указывали.
- Файл настроек и лог: `Binaries\MoH2010.PCFix.ini`, `Binaries\MoH2010.PCFix.log`. Прикладывайте лог к
  сообщениям об ошибках.

### На чём проверено

| | |
|---|---|
| Игра | Medal of Honor (2010), Steam, `moh.exe` 1.0.4.0, русский язык; кампания пройдена один раз |
| ОС | Windows 11 Pro (сборка 26300) |
| Процессор / память | Intel Core i9-14900K, 64 ГБ |
| Видеокарта | NVIDIA GeForce RTX 4080 SUPER, драйвер 621.01 |
| Монитор | MSI MPG322UX OLED, 3840x2160, 240 Гц |
| Мышь | Razer Basilisk V3 (из-за неё игра и зависала) |
| ReShade | 6.8.0 с поддержкой дополнений |

### Сборка из исходников

Visual Studio 2022 (C++ x86) и CMake. `tools\build.ps1` собирает и запускает тесты, `tools\package.ps1`
делает zip релиза. Технические заметки: [docs/RE_NOTES.md](docs/RE_NOTES.md),
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

Лицензия: MIT ([LICENSE](LICENSE)). Medal of Honor — товарный знак Electronic Arts; проект не связан с EA.
