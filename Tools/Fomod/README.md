# DragonBoardVR FOMOD Builder

`Build-Fomod.ps1` creates the release installer from the authoritative
`DragonBoardVR.rar` package.

## Build

```powershell
.\Tools\Fomod\Build-Fomod.ps1
```

To use a different source archive or keep the generated staging directory:

```powershell
.\Tools\Fomod\Build-Fomod.ps1 `
  -SourceArchive 'D:\Releases\DragonBoardVR.rar' `
  -KeepStaging
```

The generated ZIP archive is written to `artifacts/fomod/`. The build fails if
the staging directory contains another ZIP, 7z, RAR, or similar archive.

## Installer Pages

1. Core DragonBoard, always installed.
2. Physical DragonBoard and Floating DragonBoard, with at least one required.
3. Independent Physical Size and Floating Size presets.
4. Language selection, defaulting to English.

The Physical DragonBoard option owns the ESP and physical-only meshes. The
Floating DragonBoard option owns the Spell Wheel VR integration INI. A
Floating-only installation therefore does not install `DragonBoardVR.esp`.

The builder generates complete active and default INI variants because FOMOD
installers copy whole files and do not merge individual INI keys.

The Floating DragonBoard Pro preset uses `fMenuScale=1.35` with offsets
`0, -20, -3.5`. Mini is 10% smaller and Pro Max is 10% larger.

Installer screenshots are copied from the project `Assets` directory into the
archive's `fomod/images` directory.
