# DragonBoardVR INI scanner

`dragonboard_ini_scanner.py` performs a read-only scan of the active Mod
Organizer 2 profile and atomically writes `IniCatalog.json`. Runtime INI writes
remain owned by the DragonBoardVR C++ plugin.

## Test

```powershell
python -m unittest discover -s Tools/IniScanner/tests -v
xmake build IniEditorTests
.\build\windows\x64\releasedbg\IniEditorTests.exe
```

## Build the helper executable

```powershell
.\Tools\IniScanner\BuildScanner.ps1 -OutputDirectory .\Assets\tools
```

The normal DragonBoardVR build copies the resulting executable to:

```text
install_output\SKSE\Plugins\DragonBoardVR\tools\DragonBoardIniScanner.exe
```

## Manual scan

```powershell
.\Assets\tools\DragonBoardIniScanner.exe scan `
  --mo2-root "I:\Games\Skyrim VR FUS" `
  --output "$env:TEMP\IniCatalog.json" `
  --pretty
```

When `--mo2-root` is omitted on Windows, the scanner first looks for a running
`ModOrganizer.exe`, then checks local MO2 instances.
