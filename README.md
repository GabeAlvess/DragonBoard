# DragonBoardVR

DragonBoardVR is a native Skyrim VR plugin that turns a hand-mounted physical
board into an interactive interface for inventory, magic, quests, settings,
and mod actions.

It combines Skyrim scene-graph objects with RmlUi pages rendered through
Direct3D 11. Interaction supports laser input, front-face finger touch, and
physical grabbing.

DragonBoardVR is VR-only. Skyrim SE and AE are not supported.

## Features

- Inventory, Magic, Journal, Settings, Developer, and Mods pages
- Laser and finger-touch interaction
- Persistent board position, rotation, and two-hand scaling
- 3D item previews and pinned items, spells, and widgets
- Physical left/right-hand equipment actions
- Map and quest markers
- Mod Organizer 2 INI editor with a virtual keyboard
- Optional HIGGS, VRIK, and Spell Wheel VR integrations
- C++ and Papyrus APIs for external RmlUi pages
- Standalone RmlUi preview tool

## Requirements

- Windows 10 or 11 x64
- Visual Studio 2022 with Desktop Development with C++
- xmake 3.0.1 or newer
- Python 3 for the INI scanner tests
- Skyrim VR and SKSE VR

The build expects these local dependencies:

```text
lib/commonlibsse-ng/
ClibUtil/include/
simpleini/
xbyak/
```

RmlUi and the packages in `xmake-requires.lock` are resolved by xmake.

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
