# Architecture

YEET17PCSET is an unpackaged WinUI 3 desktop app. The process is a single elevated `YEET17PCSET.exe`. UI lives in C++/WinRT XAML pages; all side effects go through small C++23 modules that talk to JSON catalogs, winget, the registry, SCM, and DISM.

```
App.xaml.cpp
  logger · theme · elevation check · Russian resource load
        |
        v
  MainWindow  (NavigationView + Mica/Acrylic + custom title bar)
    Установка  -> InstallPage   -> install/{PackageCatalog, WingetClient}
    Твики      -> TweaksPage    -> tweaks/{TweakEngine, UndoStore, RestorePoint}
    Система    -> ConfigPage    -> config/{Features, Repairs}
    Обновления -> UpdatesPage   -> updates/UpdatePolicy
    footer     -> ThemeService + ConfigStore save/load
        |
        v
  core/{Logger, Settings, Localization, Strings}
  persistence/{ConfigStore, Preset}
  %LOCALAPPDATA%/YEET17PCSET/{logs, settings.json, config.json}
```

## Modules

| Target | Path | Responsibility |
|---|---|---|
| `yeet17_core` | `src/core` | spdlog file logger, JSON settings, Russian string table + `.resw` fallback |
| `yeet17_install` | `src/modules/install` | `packages.json` catalog, winget process client |
| `yeet17_tweaks` | `src/modules/tweaks` | declarative tweaks, in-memory + Win32 apply/undo, restore points |
| `yeet17_config` | `src/modules/config` | DISM feature list, repair command builders |
| `yeet17_updates` | `src/modules/updates` | Windows Update policy (full / security-only / pause / reset) |
| `yeet17_persistence` | `src/persistence` | user config + named presets (minimal / standard / maximal) |
| `yeet17_ui` | `src/ui` | ThemeService + page code-behind |
| `YEET17PCSET` | `src/app` | WinUI Application, MainWindow, Elevation |

## Data flow

1. **Startup.** `App` initializes `core::Logger` (`%LOCALAPPDATA%/YEET17PCSET/logs`), loads `core::Settings`, applies `ui::ThemeService`, then `app::Elevation::EnsureAdministrator`. UI strings come from `resources/strings/ru-RU/Resources.resw`; `core::Strings` is the in-process fallback so a missing PRI cannot leave English on screen.
2. **Install.** `InstallPage` binds to `install::PackageCatalog` (reads `catalog/packages.json` next to the exe). Actions call `install::WingetClient`, which launches `winget.exe` and streams stdout. The COM package-manager API is intentionally a TODO — we never report success unless winget’s exit code is 0.
3. **Tweaks.** `TweakEngine` loads `catalog/tweaks.json`. Apply snapshots previous registry/service values into `UndoStore`, optionally calls `RestorePoint::Create`, then runs operations. Undo walks the snapshot in reverse. On non-Windows hosts the engine still applies to an in-memory map so the logic is reviewable.
4. **Config / repairs.** `Features` lists DISM enable/disable candidates. `Repairs` only *builds* command lines (`netsh`, `SFC`, `DISM`, `winget source reset`). Execution is `#ifdef _WIN32` and always visible (no silent remote control).
5. **Updates.** `UpdatePolicy` writes the documented AU policy values (or pauses updates via the official pause keys) and can reset them.
6. **Save / load.** Footer buttons serialize selection + theme + policy through `persistence::ConfigStore` to `%LOCALAPPDATA%/YEET17PCSET/config.json`. Preset chips load `presets/*.json`.

## Safety invariants

- Every tweak is reversible or marked `reversible: false` with `requiresConfirm: true`.
- Advanced tweaks render a warning banner; dangerous ones demand an extra confirm flag.
- No autorun, no service install of ourselves, no credential APIs, no hidden IPC.
- Windows-only APIs stay behind `#ifdef _WIN32`.

## Why this split

WinUI pages must stay thin: XAML + event handlers. Catalogs are data, not C++ constants, so a tweak or package can be reviewed as JSON without a rebuild. Winget is a child process (observable, killable) rather than an in-proc COM dependency we cannot compile on every machine.
