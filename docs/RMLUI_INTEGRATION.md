# RmlUi integration

DragonBoardVR renders its local Settings, Developer and Item Editor panels with RmlUi 6.2
directly into the existing 1920x1080 DragonBoard texture. RmlUi is not a web
browser: RML is HTML-like markup and RCSS is a supported CSS subset with
RmlUi-specific elements and properties. There is no JavaScript or Chromium.

The original ImGui views remain compiled as runtime fallbacks. The tracking,
raycast, scene-graph attachment and physical texture bridge are shared by both
UI implementations and do not belong to RmlUi.

## File map

- `Src/ui/rml/DragonBoardRmlRenderer.*`: minimal D3D11 implementation of
  `Rml::RenderInterface`; owns shaders, geometry, textures, scissor state and
  the per-panel draw-call counter.
- `Src/ui/rml/DragonBoardRmlUi.*`: RmlUi context, documents, font, input bridge,
  DOM updates and event-to-request conversion.
- `Src/ui/imgui/DragonBoardSettingsMenu.*`: standalone panel controller,
  Present hook, render-target texture, game/render thread bridge and fallback
  selection. The historical class name is kept for source compatibility.
- `Src/ui/imgui/DragonBoardSettingsView.cpp`: native Settings snapshot,
  apply/save logic and ImGui fallback.
- `Src/ui/imgui/DragonBoardDeveloperView.cpp`: developer command loading,
  command queue, game snapshot and ImGui fallback.
- `Assets/ui/rml/settings.rml` and `settings.rcss`: Settings structure and shared
  visual foundation.
- `Assets/ui/rml/dev.rml` and `dev.rcss`: Developer structure and additional
  styles.
- `Assets/ui/rml/edit.rml` and `edit.rcss`: Item Editor structure and
  editor-specific styles.

## Runtime flow

1. The DragonBoard opens Settings, Developer or Item Editor through
   `DragonBoardSettingsMenu::Open()`, `OpenDev()` or `OpenItemEdit()`.
2. The existing DXGI Present hook calls `RenderPresentThread()`.
3. `InitializeStandaloneRenderer()` creates the 1920x1080 D3D11 texture and
   initializes one `DragonBoardRmlUi` instance.
4. The instance creates one RmlUi context and loads all three documents. Only
   the selected document is shown; the other documents are hidden.
5. `ProcessInput()` converts the DragonBoard UV raycast to RmlUi mouse position.
   The dominant trigger is exclusively click/drag input and dominant grip is
   exclusively the scroll modifier. With grip held, vertical pointer motion
   scrolls the document and the dominant thumbstick provides precision
   scrolling. The VR bridge adds pointer capture and geometric fallback because
   browser-style mouse hit testing is not reliable enough for a moving VR ray.
6. RmlUi renders through `DragonBoardRmlRenderer` into the same texture used by
   the physical `ImGuiScreen.nif` surface.

Document lookup order:

1. `Data/SKSE/Plugins/DragonBoardVR/ui/*.rml`
2. `SKSE/Plugins/DragonBoardVR/ui/*.rml`
3. `Assets/ui/rml/*.rml` for local development

The font uses `DragonBoardVR_Font.ttf` when distributed, with Windows font
fallbacks for local development.

## VR trigger and grip input

### Interaction contract

The local RmlUi panels use this fixed interaction model:

- Dominant trigger: click buttons and drag range controls.
- Dominant grip: arm page/list scrolling by pointer movement or thumbstick.
- `Grip + Y`: keep the existing global DragonBoard activation behavior.
- Left-handed mode: swap the physical dominant hand without changing the
  meanings of trigger and grip.

Do not solve a local RmlUi problem by changing the global grip classifier in
`VRFrameUpdater`. Global grip state is also consumed by DragonBoard activation,
grabbing and other VRUI behavior. The panel receives a local copy through
`DragonBoardSettingsMenu::OnDominantVrButtonEvent()` while the original global
events continue to reach `VRMenuManager`.

`VRMenuManager::isDominantTriggerButtonDown()` must select the physical trigger
opposite the menu hand. This keeps the pointing hand correct in both normal and
left-handed configurations.

### Original failure

Large Settings pages and the nested Developer command list exposed two
independent RmlUi behaviors:

1. Grip scrolling was submitted with `Context::ProcessMouseWheel()`. RmlUi's
   default `Auto` behavior created a smooth-scroll queue. Releasing grip stopped
   new wheel events but did not cancel the queued motion, so the page could keep
   moving while the next trigger press was already active.
2. Some controls were visibly under the VR cursor but `GetHoverElement()`
   returned a parent scroll container or no actionable ancestor. This happened
   most consistently with range controls and `dev-command-N` buttons inside a
   scrollable list. RmlUi's normal click also requires the release target to
   match the press target, which is fragile when the controller ray jitters.

The decisive runtime evidence was:

```text
RmlUi page scrollTop ... (trigger=true, grip=false)
RmlUi trigger captured none target
```

After the final fix, the expected evidence is:

```text
RmlUi trigger captured slider target
RmlUi slider 'menuScale' changed to ...
RmlUi trigger captured button target
RmlUi click on 'dev-command-N'
```

### Final implementation

`DragonBoardRmlUi` resolves the problem at the local RmlUi boundary:

- The context calls
  `SetDefaultScrollBehavior(Rml::ScrollBehavior::Instant, 1.0f)`. Grip motion is
  already sampled continuously by the VR bridge, so RmlUi's own persistent
  smooth-scroll queue remains disabled.
- While `gripDown && !triggerDown`, the bridge captures the closest scrollable
  container and maintains a local target `scrollTop`. Pointer deltas are
  low-pass filtered, use reduced sensitivity and ease toward that target each
  frame. The dominant thumbstick adjusts the same target for precision.
- Releasing grip immediately discards the local target and filtered motion.
  This gives smooth movement while held without inertia leaking into a later
  trigger press.
- A trigger scroll lock records page and nested-list offsets for the duration of
  a press. This is a final invariant: a trigger interaction cannot mutate a
  scroll position even if an internal RmlUi control attempts it.
- Normal RmlUi hover resolution is tried first.
- `BindClick()` and `BindSlider()` also call `RegisterInteractive()`. This makes
  one registry for all actionable controls in Settings, Developer, Item Editor
  and future documents using the same bindings.
- If normal hit testing returns no action, `FindInteractiveAtPoint()` tests the
  real rectangles of registered controls in the active document and selects
  the smallest matching control.
- Buttons capture the press target until release. A geometrically recovered
  button is invoked through `HandleClick()` on release, avoiding a duplicate
  native mouse click.
- Sliders capture their ID and vertical axis. Horizontal ray movement is mapped
  directly through the element's `min`, `max` and `step`; the result follows the
  same `HandleSliderChange()` path as a DOM change event.
- Slider haptics remain rate-limited to one pulse every 55 ms.

Dynamic Developer commands are rebuilt with `SetInnerRML()` and rebound as
`dev-command-0`, `dev-command-1`, and so on. Because `BindClick()` registers
them, they automatically participate in the same geometric fallback. No
Developer-specific trigger code is required.

### Approaches that did not solve the problem

- Reclassifying or suppressing grip globally broke `Grip + Y` and could select
  the wrong dominant trigger in left-handed mode.
- Checking only `gripDown && !triggerDown` prevented new wheel events but did
  not stop an existing smooth-scroll queue.
- Repeatedly restoring `scrollTop` hid motion during the press but did not make
  the original RmlUi hit target reliable.
- A fallback hard-coded only for Settings slider IDs fixed one document but
  left dynamic Developer buttons with the same hit-test failure. The shared
  interactive registry replaced this temporary design.

### Regression checklist

Test all of these after changing local panel input:

1. Open DragonBoard with `Grip + Y`.
2. Verify normal and left-handed dominant trigger selection.
3. Scroll a large Settings page with grip; trigger must never scroll it.
4. Click Settings tabs, toggles, Save and Close.
5. Drag multiple Settings sliders and confirm value changes plus haptics.
6. Scroll the nested Developer command list with grip.
7. Select several `dev-command-N` rows with trigger and execute one.
8. Test Item Editor tabs, sliders, pin cards and apply buttons.
9. Restart Skyrim after replacing the DLL and confirm the SKSE log timestamp is
   newer than the build. Build/package hashes alone do not prove which DLL the
   running game loaded.

## Startup warm-up

`kDataLoaded` calls `DragonBoardSettingsMenu::RequestRmlWarmup()`. This only
installs or reuses the DXGI Present hook and arms a one-shot request. The next
valid Present initializes the standalone D3D11 texture, ImGui fallback context,
RmlUi context, font and all local documents while Skyrim is normally still at
the main menu. `kPostLoadGame` and `kNewGame` repeat the request only when the
early swap-chain lookup was unavailable.

This warm-up is deliberately independent from `VRMenuManager::initialize()`:
it does not load inventory models, inspect the player skeleton, attach
`ImGuiScreen.nif`, publish a physical surface or open a panel. Scene attachment
still happens lazily in `UpdateScenePanelGameThread()` only when local content
is visible. RmlUi initialization and all D3D11 calls remain on the Present
thread.

The log records both the individual document load time and the total warm-up
time. A successful run should contain `RmlUi warm-up completed` before the
first `opened RmlUi ...` message.

## Thread boundary

The boundary is mandatory because Present is not Skyrim's game thread.

- Game thread: reads `RE::PlayerCharacter`, cells, worldspaces and
  `VRUISettings`; executes console commands; applies and saves settings.
- Present thread: owns RmlUi, its DOM, the D3D11 panel renderer and RML events.
- Shared state: plain snapshots protected by `_draftMutex`, `_devMutex` or
  `_itemEditMutex`, plus atomic pending flags.

RmlUi events must never call Skyrim/CommonLib gameplay APIs directly.

### Settings

`CaptureSettingsGameThread()` copies `VRUISettings` into `SettingsDraft`.
`SyncRmlSettingsFromDraft()` copies the draft into the range controls when the
document opens. A slider `change` event produces a `SliderChange`; the Present
thread updates the protected draft and sets `_applyPending`. The game thread
consumes that flag, calls `ApplyDraftGameThread()` and refreshes active panels.
`Save INI` sets `_savePending`, also consumed on the game thread.

Slider IDs in RML must match the mapping in:

- `kSliders` and `SetSliderValue()` in `DragonBoardRmlUi.cpp`
- `SyncRmlSettingsFromDraft()` and `ApplyRmlSliderChange()` in
  `DragonBoardSettingsMenu.cpp`

The numeric value label convention is `value-<slider-id>`.

The General page also exposes the native `editModeEnabled` master switch as
`toggle-edit-mode`. This setting gates secondary-button item editing in every
dynamic container. Keep it synchronized through `SetEditModeEnabled()` and
`ConsumeEditModeToggleRequested()`; do not duplicate the state in RML.

### Developer panel

`LoadDevCommandsGameThread()` loads defaults or
`Data/SKSE/Plugins/DragonBoardVR_DevCommands.ini`. The Present thread sends a
plain `DeveloperCommand` list to RmlUi. Dynamic buttons use stable numeric IDs:
`dev-command-0`, `dev-command-1`, and so on.

Clicking `Execute Command` returns only the selected index. The controller
validates the index against the protected native command list and calls
`QueueDevCommand()`. `UpdateGameThread()` consumes the request and performs the
actual console action. Dangerous commands keep the existing delayed/close
behavior.

`CaptureDevGameInfoGameThread()` publishes the player/location snapshot every
0.5 seconds. FPS, frame time and panel draw calls are supplied by the Present
thread. The draw-call value counts only RmlUi/ImGui panel batches, not the whole
Skyrim frame.

### Item Editor

Inventory, favorites, magic and mod containers still populate the native
`VRUIItemEditPanel` backend. `VRMenuManager::switchToPanel("ItemEditPanel")`
intercepts that destination, calls `OpenItemEdit()` and places `MainPanel`
behind the local texture so inventory meshes cannot intersect the flat editor.
The source container name is captured in the draft and restored when Back,
Close or Reset is used.

`VRUIItemEditPanel::EditState` is copied into `ItemEditDraft` on the game
thread. RmlUi edits only this plain draft. Slider changes set
`_itemEditApplyPending`; `UpdateGameThread()` then calls
`setWorkingTransform()`. Apply, reset, pin and label buttons produce an
`ItemEditAction`, also consumed on the game thread. The document must not call
`VRUILayoutManager`, touch preview NIFs or save settings directly.

The classic 3D `ItemEditPanel` remains registered as a fallback when the local
renderer cannot open. If RmlUi initializes but `edit.rml` is invalid, a minimal
ImGui item editor fallback remains available without affecting the source
container.

World pinning for Magic items is displayed as unavailable in the RmlUi editor:
the old action derives its world transform from the classic editor's live 3D
preview NIF, while the texture-based editor has no equivalent pose. Dashboard
and left-hand pins use numeric transforms and remain available. Do not reuse a
hidden classic preview node because its world transform can be stale.

## Events and IDs

Static clickable elements are registered after a document loads. Dynamic
developer command buttons are created with `SetInnerRML()` and rebound after
each rebuild. Click listeners are attached to actionable elements; change
events are observed at the document root. `BindClick()` and `BindSlider()` also
register every control with the shared VR geometric resolver. A click on nested
button text resolves to the owning button. The shared listener translates DOM
events into small requests:

- `click`: tabs, close, fallback, save, command selection and execution.
- `change`: Settings and Item Editor range controls.

The cursor element must keep `pointer-events: none`; otherwise it intercepts
the raycast click even though its position appears correct.

When adding a new native-backed control, always route it through `BindClick()`
or `BindSlider()`. Attaching an RmlUi listener manually without registering the
control bypasses the VR fallback and can recreate the `captured none` failure.

## Haptic feedback

RmlUi hover, click and slider events request haptics on the Present thread.
`DragonBoardSettingsMenu` transfers only the highest-priority cue through an
atomic value, and `UpdateGameThread()` executes it through
`VRMenuManager::triggerHaptic()` on the dominant pointing hand.

- Button/input hover: light pulse, gated by `bHapticOnHover`.
- Normal click: standard pulse, gated by `bHapticOnPress`.
- Slider change: short micro-pulse limited to one every 55 ms.
- Dangerous Developer command: stronger pulse.

External documents can override the click cue declaratively:

```xml
<button data-haptic="none">Silent</button>
<button data-haptic="light">Light</button>
<button data-haptic="normal">Normal</button>
<button data-haptic="strong">Dangerous action</button>
<button data-haptic="error">Unavailable action</button>
```

Never trigger OpenVR haptics directly from an RmlUi event listener.

## Adding a Developer tab

1. Add a `dev-tab-<name>` button and `dev-page-<name>` section to `dev.rml`.
2. Add `<name>` to `kDeveloperPages` in `DragonBoardRmlUi.cpp`.
3. Add visual rules to `dev.rcss`.
4. If the page needs game data, extend a plain snapshot in
   `DragonBoardSettingsMenu.h` and populate it from `UpdateGameThread()`.
5. Add DOM update fields to `DeveloperInfo` and `SetDeveloperInfo()`.
6. For actions, expose a pending request and consume it on the game thread.

Do not read game objects from `SetDeveloperInfo()`, event listeners or any
method called by `RenderStandalone()`.

## RCSS limitations

RCSS can be edited with a normal CSS editor, but it is not full browser CSS.
Use only features supported by the bundled RmlUi version. Range controls expose
RmlUi-specific internal elements such as `slidertrack`, `sliderprogress`,
`sliderbar`, `sliderarrowdec` and `sliderarrowinc`.

Visual changes to RML/RCSS do not require a DLL rebuild, but the documents are
loaded once when the renderer initializes. Restart Skyrim to guarantee a clean
reload. C++ bindings, IDs or new native data still require rebuilding the DLL.

## Standalone visual previewer

`DragonBoardRmlPreview` renders any RmlUi document outside Skyrim. It uses the
production `DragonBoardRmlRenderer` at 1920x1080, but has no SKSE, CommonLib,
Present hook, ImGui or access to game state. This makes it useful both as an
RCSS editor preview and as a diagnostic boundary.

```powershell
xmake build DragonBoardRmlPreview
xmake run DragonBoardRmlPreview
xmake run DragonBoardRmlPreview "C:\path\to\panel.rml"
```

Use `Ctrl+O`, `File > Open RML` or drag-and-drop to open a document. The
`Documents` menu lists all RML files beside the current document. The
previewer watches that folder recursively for RML, RCSS and font changes and
reloads automatically. Use `F1` for Settings, `F2` for Developer, `F3` for
Item Editor, `F5`
for a forced reload, `F8` for the RmlUi debugger and `F9` for the visual
inspector. Click and change events
are caught at the document root, so generic buttons and sliders work without
native registration. DragonBoard game values and commands use mock data.

The visual inspector provides a DOM tree, click-to-select and editable common
properties with immediate preview plus undo/redo. `Save RCSS` writes
`<document>.editor-overrides.rcss`. The editor reloads this sidecar
automatically; distribution documents must include it explicitly with a
`<link type="text/rcss" href="...editor-overrides.rcss" />` element.

See `Tools/RmlPreview/README.md` for the complete controls and diagnostic
scope. The target is optional and is never added to the mod package.

## Build and package

Run:

```powershell
xmake build DragonBoardVR
```

The `after_build` step copies the DLL and all `Assets/ui/rml/*.rml` and
`*.rcss` files into `install_output/SKSE/Plugins/DragonBoardVR/ui`.

The runtime package must contain:

```text
SKSE/Plugins/DragonBoardVR.dll
SKSE/Plugins/DragonBoardVR/ui/settings.rml
SKSE/Plugins/DragonBoardVR/ui/settings.rcss
SKSE/Plugins/DragonBoardVR/ui/dev.rml
SKSE/Plugins/DragonBoardVR/ui/dev.rcss
SKSE/Plugins/DragonBoardVR/ui/edit.rml
SKSE/Plugins/DragonBoardVR/ui/edit.rcss
```

If one document is missing or fails to parse, its panel uses the existing ImGui
fallback while the other valid RmlUi document can continue to work.
