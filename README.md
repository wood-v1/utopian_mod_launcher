# UtopianModLauncher

Runtime patching launcher for Pathologic Classic HD mods.

Current version: `1.0`.

Run `GameModLauncher.exe` to open the native launcher UI. The main screen is focused
on mod load order, per-mod settings, logging, and launching the game. Use
`GameModLauncher.exe --noui` to run the headless launch path directly.
`--launch` is kept as a deprecated alias for old scripts.

## LoadOrder

Mods are loaded from `Final\mods` in the exact order listed in `GameModLauncher.ini`.
This `LoadOrder` applies only to DLL mods.

Example:

```ini
[Mods]
LoadOrder=OynonTools.dll@suspended, PGOG.dll@engine, PPMM.dll@ui+3000
```

Shared DLL dependencies are still listed in `LoadOrder`, but their role is
declared separately. This is for common hook/helper libraries that can be used by
several mods and should not be deleted with one ordinary mod:

```ini
[SharedDlls]
Names=OynonTools.dll

[SharedDll:OynonTools.dll]
Name=OynonTools
Stage=suspended
DelayMs=0
Manifest=shared-OynonTools.dll
RequiredBy=StaminaSystem.dll
```

Packages can include the same `[SharedDlls]` section in
`bin\Final\GameModLauncher.ini`. The launcher then installs that DLL as
`DLL Mod Shared Dependency`. Shared dependencies own their DLL and matching INI
through a separate manifest, while package resources remain owned by the primary
mod package.

DLL packages installed through the launcher are tracked as a single component:

```ini
[Packages]
Order=stamina-system

[Package:stamina-system]
Name=Stamina System
Manifest=stamina-system
PrimaryDll=StaminaSystem.dll
Dlls=PPMM.dll, StaminaSystem.dll
SharedDlls=OynonTools.dll
```

`Dlls` are ordinary package members and are deleted together with the package.
`SharedDlls` stay installed and are only referenced by the package.

Centralized logging is controlled from the same file:

```ini
[Logging]
Enabled=1
```

Stage dependencies and timeouts are also configured in `GameModLauncher.ini`:

```ini
[Stages]
EngineModule=Engine.dll
EngineTimeoutMs=15000
UiModule=UI.dll
UiTimeoutMs=15000
```

If these keys are missing or a timeout is invalid, the launcher uses `15000` ms.
These advanced values are intentionally edited in the INI, not on the main UI.

When enabled, normal mod debug lines go to `Final\mods\Debug.log`, while console-capture lines go to `Final\mods\Console.log`.

Stage suffixes are optional:

- `@suspended` load before `Game.exe` is resumed
- `@resume` load right after resume
- `@engine` wait for `Engine.dll`
- `@ui` wait for `UI.dll`
- `+3000` adds an extra delay in milliseconds after the stage is reached and before that mod is loaded

If no stage suffix is specified, `@resume` is used.

Selecting a mod and pressing `Mod settings...` opens the mod name editor. DLL
mods also show an editor for a matching mod INI, such as `PPMM.dll` -> `PPMM.ini`.
Resource mods do not require an INI, so their settings dialog only edits the
display name and shows resource status.

Resource-only mods are tracked separately:

```ini
[ResourceMods]
Order=resource-pack

[ResourceMod:resource-pack]
Name=Resource Pack
Stage=resume
DelayMs=0
Manifest=resource-pack
```

The stage and delay fields are saved for ordering/notes in the UI. They do not
affect game launch because resource mods are installed as files on disk.

## Installing mods

In the UI, `Install Mod` accepts folders and `.zip`/`.rar` packages. Folder
install is also available from CLI with `install --folder <path>`. RAR support
uses Windows shell support first, then installed WinRAR/UnRAR if available.
Selecting an installed mod shows the files tracked by its install manifest.
Double-click opens the mod settings and rename dialog.

Zip/folder packages should use the game-root layout:

```text
bin\Final\mods\SomeMod.dll
bin\Final\mods\SomeMod.ini
data\Scripts\some_resource.bin
data\Textures\some_texture.tex
data\Sounds\some_sound.wav
```

Release folders may also contain helper files at the package root. The launcher
ignores root-level docs/scripts/archives and bundled launcher files such as
`bin\Final\GameModLauncher.exe`, `bin\Final\GameModLauncher.ini`,
`bin\Final\mods\.launcher\banner.txt`, `bin\Final\mods\.launcher\banner.bmp`,
and `bin\Final\mods\.launcher\banner.png`; installable DLLs/resources still
come from `bin\Final\mods` and `data`.

If a UI package does not use game-root layout and contains only loose resource
files, the launcher asks where to install it: `data` or one of the common
subfolders such as `data\Scripts`, `data\Textures`, `data\Actors`, or
`data\Sounds`.

Packages with DLLs under `bin\Final\mods` are installed as `DLL Mod`. If a DLL
package contains multiple DLLs, the UI asks which additional DLLs should also be
added to `LoadOrder`. Packages without DLLs but with files under `data` are
installed as `Resource Mod`. The install dialog suggests the mod name from the
archive/folder name and lets you change it before installation.

After install, DLL mods are added to `LoadOrder` with the default `resume` stage.
DLLs listed in package `[SharedDlls]` are shown as `DLL Mod Shared Dependency`;
they cannot be deleted while another mod lists them in `RequiredBy`.
Other DLLs selected from the same package are shown as `DLL Mod Dependency` and
are deleted together with the primary package.
Resource mods are added to `[ResourceMods]` and are not injected at runtime.

`Delete Mod` removes files created by that install and restores overwritten files
from backup only if they have not changed since the mod was installed. If a file
was changed later by another mod or by hand, the launcher leaves it in place and
reports it as skipped. Older DLL mods without a readable manifest are deleted
conservatively: only the selected DLL and matching INI are removed. Resource mods
without a manifest are removed from the launcher config only.

The launcher also detects mod-to-mod file conflicts through install manifests. A
vanilla overwrite is not a conflict by itself; it becomes a conflict only when
another installed mod manifest owns the same relative file path. Conflict rows are
highlighted in the UI, and package install warns before continuing.

## CLI modes

- `GameModLauncher.exe` or `GameModLauncher.exe --ui`: open the UI
- `GameModLauncher.exe --noui`: launch the game without showing the UI
- `GameModLauncher.exe --noresourcemods`: headless launch for component use by another launcher
- `GameModLauncher.exe --launch`: deprecated alias for `--noui`
- `GameModLauncher.exe --self-test`: run built-in tests
- `GameModLauncher.exe help`: print command help and launcher version
- `GameModLauncher.exe --version`: print launcher version
- `GameModLauncher.exe list`: print installed DLL and resource mods
- `GameModLauncher.exe install --zip <path> [--name <name>] [--dll <dllName>]`: install a zip package
- `GameModLauncher.exe install --rar <path> [--name <name>] [--dll <dllName>]`: install a rar package
- `GameModLauncher.exe install --folder <path> [--name <name>] [--dll <dllName>]`: install a folder package
- `GameModLauncher.exe delete --mod <name|id|dll>`: safe-delete an installed mod
- `GameModLauncher.exe rename --mod <name|id|dll> --name <newName>`: change display name
- `GameModLauncher.exe set-logging --enabled 0|1`: update logging
- `GameModLauncher.exe set-stage --mod <name|id|dll> --stage suspended|resume|engine|ui [--delay-ms N]`: update stage metadata
- `GameModLauncher.exe move --mod <name|id|dll> --up|--down`: move within DLL or resource order
- `GameModLauncher.exe vanilla-files [--all|--changed|--backed-up]`: list overwritten vanilla files tracked by manifests
- `GameModLauncher.exe conflicts [--mod <name|id|dll>|--package-folder <path>]`: list manifest-based file conflicts

`--noresourcemods` does not disable resource files already installed into `data`;
it only skips UI/resource-manager behavior and runs the normal DLL launch path.

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
