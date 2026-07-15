# DragonBoard standalone panels

This directory keeps the standalone Settings and Developer panels independent
from the optional `imgui-vr-helper.dll` client renderer. RmlUi is now the
primary local UI and these ImGui views remain as fallbacks.

The complete RmlUi architecture, thread rules, external file map and extension
procedure are documented in `docs/RMLUI_INTEGRATION.md`.

## File map

- `DragonBoardSettingsMenu.cpp`: controller, Present hook, standalone DX11
  renderer, input bridge, and physical DragonBoard surface hosting.
- `DragonBoardSettingsView.cpp`: Settings UI plus game-thread capture/apply of
  `VRUISettings`.
- `DragonBoardDeveloperView.cpp`: Developer tabs, command file loading,
  command queueing, and game-information snapshots.
- `StandaloneImGuiStyle.cpp`: shared standalone theme and cursor scale.
- `StandaloneImGuiStyle.h`: panel resolution and physical presentation
  constants shared by the renderer and views.

`DragonBoardSettingsMenu` keeps its existing name and public API for source
compatibility with the menu composition code. It is the controller for every
local standalone panel, not only Settings.

## Thread boundary

- `UpdateGameThread` may read Skyrim/CommonLib objects and publishes plain
  snapshots protected by the view mutexes.
- `RenderPresentThread` and all `Draw*` functions may read only those snapshots,
  atomics, ImGui state, and DX11 objects owned by the standalone renderer.
- ImGui buttons must queue game actions. They must not execute console commands
  or touch `RE::PlayerCharacter` directly from Present.
- Texture/context creation and ImGui draw submission remain on Present.

This boundary is intentional. Do not move game-object reads into a view merely
because the value is displayed there.

## Adding a Developer tab

1. Add the tab body to `DragonBoardDeveloperView.cpp`.
2. If it needs game data, extend `DevGameInfoSnapshot` (or add a dedicated plain
   snapshot) in `DragonBoardSettingsMenu.h`.
3. Populate that snapshot from `UpdateGameThread`.
4. For actions, add a pending request consumed by `UpdateGameThread`.

Visual-only changes belong in `StandaloneImGuiStyle.cpp`. Tracking, raycast,
panel texture, and physical host changes belong in `DragonBoardSettingsMenu.cpp`.

## Visual assets

The standalone style first looks for the distributable condensed font at
`Data/SKSE/Plugins/DragonBoardVR/DragonBoardVR_Font.ttf`. During development it
can fall back to `C:/Windows/Fonts/BarlowCondensed-Regular.ttf`, and finally to
the built-in ImGui font. A release package should include the first path so the
layout is identical on every system.
