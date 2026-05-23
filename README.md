# UtopianModLauncher

Runtime patching launcher for Pathologic Classic HD mods.

## LoadOrder

Mods are loaded from `Final\mods` in the exact order listed in `GameModLauncher.ini`.

Example:

```ini
[Mods]
LoadOrder=OynonTools.dll@suspended, PGOG.dll@engine, PPMM.dll@ui+3000
```

Centralized logging is controlled from the same file:

```ini
[Logging]
Enabled=1
```

When enabled, normal mod debug lines go to `Final\mods\Debug.log`, while console-capture lines go to `Final\mods\Console.log`.

Stage suffixes are optional:

- `@suspended` load before `Game.exe` is resumed
- `@resume` load right after resume
- `@engine` wait for `Engine.dll`
- `@ui` wait for `UI.dll`
- `+3000` adds an extra delay in milliseconds after the stage is reached and before that mod is loaded

If no stage suffix is specified, `@resume` is used.

## Build

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

## Usage

1. Place `GameModLauncher.exe` next to `Game.exe`
2. Place mod DLLs in `mods`
3. Adjust `GameModLauncher.ini` if needed
4. Run `GameModLauncher.exe`
