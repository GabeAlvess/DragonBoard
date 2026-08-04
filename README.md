## Requirements

- Windows 10 or 11 x64
- Visual Studio 2022 with Desktop Development with C++
- xmake 3.0.1 or newer
- Python 3 for the INI scanner tests
- Skyrim VR and SKSE VR

CommonLibVR is pinned as a Git submodule. The remaining C++ dependencies,
including SimpleIni and RmlUi, are downloaded and resolved by xmake.

Clone with submodules:

```powershell
git clone --recursive https://github.com/GabeAlvess/DragonBoard.git
cd DragonBoard
```

For an existing clone:

```powershell
git submodule update --init --recursive
```

## Build

From the repository root:

```powershell
xmake f -p windows -a x64 -m releasedbg `
  --skyrim_vr=y --skyrim_se=n --skyrim_ae=n
xmake build DragonBoardVR
```

The complete mod package is generated in:

```text
install_output/
```

## Tests

```powershell
xmake build RmlVirtualListTests RmlEntranceAnimationTests IniEditorTests
.\build\windows\x64\releasedbg\RmlVirtualListTests.exe
.\build\windows\x64\releasedbg\RmlEntranceAnimationTests.exe
.\build\windows\x64\releasedbg\IniEditorTests.exe

python -m unittest discover -s Tools\IniScanner\tests -v
```

## RmlUi Preview

```powershell
xmake build DragonBoardRmlPreview
xmake run DragonBoardRmlPreview
```

See [`docs/PROJECT_OVERVIEW.md`](docs/PROJECT_OVERVIEW.md) and
[`docs/RMLUI_INTEGRATION.md`](docs/RMLUI_INTEGRATION.md) for technical details.

## License

GPL-3.0.
