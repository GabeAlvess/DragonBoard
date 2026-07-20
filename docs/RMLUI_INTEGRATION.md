# RmlUi panel framework

DragonBoardVR uses RmlUi 6.2 as its only flat-panel renderer. It has two kinds
of RmlUi output:

- **Page panels** such as Inventory, Magic, Settings and panels registered by
  another SKSE plugin. Only one page is active at a time and it renders to the
  main 1920x1080 DragonBoard texture.
- **Independent surfaces** such as the built-in status widget. Each surface
  owns its RmlUi context, render target, scene quad and transform, so several
  small widgets can eventually be visible at the same time without replacing
  the main page texture.

Both kinds attach to the physical DragonBoard scene and therefore inherit its
hand-following or world-pinned behavior.

RmlUi is not a browser. RML is HTML-like markup, RCSS is RmlUi's CSS subset,
and there is no JavaScript or Chromium runtime.

## Architecture

The integration is split by responsibility:

- `Src/ui/rml/RmlPanelHost.*` owns the DXGI Present hook, render-target
  texture, VR input snapshot, scene texture bridge and the game/render thread
  queues. It is the only class that knows both Skyrim's board and RmlUi's
  renderer.
- `Src/ui/rml/DragonBoardRmlUi.*` owns the RmlUi context, registered documents,
  DOM mutations, generic control discovery, VR hit testing and panel events.
  It never reads or writes Skyrim game objects.
- `Src/ui/rml/BuiltinSettingsPanel.cpp` copies the native settings state to and
  from the Settings draft.
- `Src/ui/rml/BuiltinDeveloperPanel.cpp` owns developer command loading and the
  game/location snapshot.
- `Src/ui/rml/DragonBoardRmlRenderer.*` is the D3D11 implementation of
  `Rml::RenderInterface`.
- `Src/ui/rml/StatusWidget.*` is the reference implementation of one
  independent widget's data adapter. It updates status-specific DOM values but
  never reads Skyrim objects directly.
- `Src/ui/rml/RmlSurface.*` owns the reusable private RmlUi context,
  document, cursor, dirty-state and render path required by every independent
  widget.
- `Src/ui/rml/RmlSurfaceGrabController.*` applies one-hand position/rotation
  and two-hand uniform scaling to an independent surface scene root.
- `Src/DragonBoardVR_API.*` exposes one generic panel API to other plugins.
  The public surface is RmlUi-only; native DragonBoard widgets remain an
  internal implementation detail.
- `Assets/ui/rml/` contains DragonBoardVR's built-in documents.

The old ImGui renderer and ImGui VR Helper client are no longer compiled or
initialized. `ImGuiScreen.nif` and `ImGui0.dds` retain their historical file
names only because the NIF is the packaged physical quad used by existing
installations. The runtime content rendered onto it is exclusively RmlUi.

## Runtime flow

1. `kDataLoaded` requests a warm-up without creating or attaching scene nodes.
2. The next valid Present creates the main D3D11 render target and initializes
   the page-panel `DragonBoardRmlUi` context.
3. Opening a panel selects one document and makes the host visible.
4. The game thread derives the panel UV from the DragonBoard's actual scene
   transform. This keeps the texture and raycast aligned while the board
   follows the hand.
5. Present applies queued DOM commands, sends the VR pointer state to RmlUi and
   renders the active document.
6. RmlUi events are copied to a queue. External callbacks and all Skyrim-facing
   actions run later on the game thread.

An independent surface follows the same thread boundary but has its own
context and render target. The status widget captures gold, carry weight and
location on the game thread, copies a plain snapshot through a mutex, updates
its DOM on Present, and renders only when that snapshot marks it dirty.

No RmlUi event listener may call CommonLib or Skyrim gameplay APIs directly.

### Page-panel button contract

Buttons that target a DragonBoard page use toggle semantics consistently:

- when the requested page is closed, the button opens it;
- when that same page is already open, the button closes it and returns Home;
- when a different page is open, the button switches directly to the requested page.

This contract applies to built-in fixed buttons and configurable slots that
target Inventory, Magic, Journal, Settings, Developer, Mods or another native
DragonBoard panel. Navigation, quick actions and buttons that deliberately open
a Skyrim game menu keep their action-specific behavior.

## Built-in documents

Built-in document lookup uses this order:

1. `Data/SKSE/Plugins/DragonBoardVR/ui/<name>.rml`
2. `SKSE/Plugins/DragonBoardVR/ui/<name>.rml`
3. `Assets/ui/rml/<name>.rml` for local development

The documents are:

- `inventory.rml` / `inventory.rcss`
- `magic.rml` / `magic.rcss`
- `journal.rml` / `journal.rcss`
- `settings.rml` / `settings.rcss`
- `dev.rml` / `dev.rcss`
- `edit.rml` / `edit.rcss`
- `status_widget.rml` / `status_widget.rcss` (independent surface)

If a requested document cannot load, the host logs the error and rejects it.
Settings and Item Editor callers may then use their existing native 3D panel;
there is no ImGui fallback with independent behavior.

## Creating a page panel

A page panel is the correct choice for a full-screen menu that replaces the
current DragonBoard content. Its creation path is:

1. Create an RML document and RCSS stylesheet in the consumer mod.
2. Give every interactive `button` or `input` a stable `id`.
3. Register the document with `RegisterPanel()` or `RegisterPanelV2()`.
4. Wait for `PanelState::Ready` when using API v2.
5. Call `ShowPanel()` from a game-thread action.
6. Use `SetElementText`, `SetElementAttribute` and `SetElementClass` for later
   updates; never mutate RmlUi from the Skyrim game thread directly.
7. Call `UnregisterPanel()` before the callback or `userData` becomes invalid.

The host loads the document into the shared page context. Showing another page
hides the previous document; it does not create another scene quad or another
render target. See [Public API](#public-api) for a complete C++ example and
[Papyrus API](#papyrus-api) for a script-only consumer.

## Creating an independent surface

Use an independent surface for a small widget that must coexist with the Home
screen and have its own position, rotation or scale. The status widget is the
current reference implementation. Its files are:

- `Assets/ui/rml/status_widget.rml` and `status_widget.rcss`: markup and style.
- `Src/ui/rml/RmlSurface.*`: shared context, cursor and render base.
- `Src/ui/rml/StatusWidget.*`: status-specific DOM data adapter.
- `RmlPanelHost::EnsureStatusRenderTargetPresentThread()`: 250x32 D3D11
  texture, RTV and SRV allocation.
- `RmlPanelHost::UpdateStatusSceneSurfaceGameThread()`: quad creation,
  DragonBoard-relative transform and texture bridge.
- `RmlPanelHost::CaptureStatusSurfaceGameThread()`: Skyrim data snapshot.
- `RmlPanelHost::RenderStatusSurfacePresentThread()`: Present-thread DOM update
  and render-on-dirty execution.

### Required isolation

Every independently rendered scene quad must have all of the following:

- a unique `Rml::Context` name;
- its own `ID3D11Texture2D`, `ID3D11RenderTargetView` and
  `ID3D11ShaderResourceView`;
- its own `RE::BSGraphics::Texture` bridge;
- a NIF whose diffuse texture path is unique to that surface;
- its own scene root and geometry runtime data.

The unique diffuse path is mandatory even when the DDS pixels are identical.
Skyrim caches shader resources by texture path and may keep the render pass
created when the NIF loads. Cloning a shader property or calling `SetMaterial`
after load is not sufficient: the widget can display the placeholder on its
first frame and later sample the last main-panel texture.

The build therefore generates `StatusScreen.nif` from `ImGuiScreen.nif` with
the internal path changed from `textures\\ImGui0.dds` to
`textures\\ImGui1.dds`. `Tools/GenerateStatusScreen.ps1` performs a same-length
binary replacement, and `xmake.lua` installs both the generated NIF and
`ImGui1.dds`. Runtime code then replaces `NiSourceTexture::rendererTexture`
directly, which is the same proven binding path used by the main surface.

Do not copy `ImGuiScreen.nif` unchanged for another surface. Allocate the next
texture name (for example `ImGui2.dds`), generate a NIF that already references
that name, and keep the binding direct.

### Thread and visibility lifecycle

Independent surfaces keep Skyrim access and rendering separated:

1. The game thread captures only plain values into a snapshot.
2. It marks the surface data pending/dirty.
3. Present consumes the snapshot and updates the surface document.
4. The private context renders into that surface's RTV.
5. The scene quad samples the corresponding SRV through its texture bridge.

The status widget is visible only when the DragonBoard menu is open and no
main page panel is active. Opening Inventory, Magic, Journal, Settings,
Developer, Item Editor or an external page culls the status scene node.
Returning Home unculls it without binding it to the page panel's document.
Closing DragonBoard culls both surfaces.

### Main page entrance reveal

When the main page surface changes from hidden to visible, `RmlPanelHost`
starts a short center-out entrance reveal. The effect is a renderer post-pass
over the fully composed render target, so it does not reflow, scale or otherwise
change the active RML document. Built-in and external page panels receive the
same reveal. Switching documents while the page surface is already visible
does not restart it, and independent widgets/status surfaces are unaffected.

The dirty scheduler renders continuously only for the configured duration and
then resumes cached frames. Pointer, trigger and grip input are suppressed while
the reveal is active so invisible controls cannot be activated. Runtime INI
controls under `[RmlUi]` are:

- `bEntranceAnimation` (default `true`);
- `fEntranceDuration` (default `0.25`, clamped to `0.05`-`2.0` seconds);
- `fEntranceFeather` (default `0.10`, clamped to `0.0`-`0.5`).

### Transform and grab behavior

Place the surface root relative to the `DragonBoard` geometry transform, not
relative to a hardcoded world position. `RmlSurfaceGrabController` operates on
that root:

- one grip controls position and rotation;
- adding the second grip controls uniform scale;
- releasing writes the resulting local transform to
  `Data/SKSE/Plugins/DragonBoardVR_Layout.json`;
- `SurfaceEvent` reports grab start, transform changes and grab end when a
  callback is attached.

Persisted independent surfaces use the reserved `RmlUiSurfaces` layout
container and their stable surface id as the element id. On first creation the
runtime registers the code-defined transform as the default. On later game
sessions it restores position, rotation matrix and uniform scale before the
surface is attached. Saving occurs once when grab ends, not on every movement
frame.

### Required capability contract

`DragonBoardVR_API::DefaultSurfaceFeatures` is the baseline for every new
independent surface:

- `Visible`;
- `Interactive` (ray-to-UV pointer state and `#vr-cursor`);
- `RenderOnDirty`;
- `Grabbable`;
- `PersistTransform`.

`SurfaceDescriptorV2` and the internal `SurfaceState` both default to
this mask. New capabilities must be added to the shared runtime/base and then
to this contract when they become mandatory; they must not be implemented only
inside one widget. The existing main page surface predates this contract and is
explicitly constructed without independent-surface flags.

### Current public API boundary

`IDragonBoardVR2` reserves `CreateSurface`, `BindPanelToSurface`, visibility and
transform methods so the ABI will not need to be reshaped later. They are not
enabled in the current build: `GetCapabilities()` does not advertise
`IndependentSurfaces`, and the surface methods return failure/no-op values.

Consequently, external mods can currently create page panels, but not arbitrary
independent scene surfaces. New independent widgets must be implemented inside
DragonBoardVR following the status-widget path until the generic surface
registry, resource lifetime and panel-to-surface binding are completed.

### Checklist for another built-in widget

1. Add `<widget>.rml` and `<widget>.rcss` under `Assets/ui/rml/`, including a
   `#vr-cursor` element with `pointer-events: none`.
2. Compose `RmlSurface` inside the widget data adapter; provide a
   globally unique context name, document candidates and logical dimensions.
3. Add a `SurfaceState` entry with a stable internal handle.
4. Allocate a dedicated D3D11 texture/RTV/SRV on Present.
5. Generate a dedicated NIF with a new intrinsic diffuse path and install the
   matching DDS placeholder during `after_build`.
6. Bind the NIF's existing diffuse `NiSourceTexture` directly to that surface's
   `RE::BSGraphics::Texture` bridge. Do not use `SetMaterial` for this binding.
7. Capture Skyrim values on the game thread and pass only a snapshot to
   Present.
8. Define when the surface is visible and cull its scene node outside that
   state.
9. Add surface-specific ray-to-UV mapping and feed the shared surface pointer
   state; do not create widget-specific pointer atomics.
10. Call `RegisterAndApplySurfaceTransform()` after assigning the code-defined
    local transform. Shared grab handling will save it on release.
11. Test first open, page-to-Home return, repeated page switches, grab, scale,
    board close/reopen and a full game restart.

## VR interaction contract

- Dominant trigger clicks buttons and drags range controls.
- Dominant grip arms page/list scrolling by controller movement or thumbstick.
- `Grip + Y` keeps the global DragonBoard activation behavior.
- Left-handed mode changes the physical pointing hand without changing the
  trigger/grip meanings.

The runtime automatically discovers every `button` and `input` with an `id` in
registered external documents. Range inputs enter the slider path; other
inputs enter the click path. This registration is also used by the geometric
fallback that compensates for moving VR-ray jitter.

Use a cursor element with the conventional id when a document needs the local
VR pointer:

```xml
<div id="vr-cursor"></div>
```

It must have `pointer-events: none` in RCSS.

## Public API

Copy `Src/DragonBoardVR_API.h` into the consumer plugin. Request the API during
or after SKSE post-load, register one document, then show it from the mod's own
hotkey, native DragonBoard button or other game-thread action. There is no
version selector or legacy widget interface while DragonBoardVR is still in
development.

```cpp
namespace
{
    DragonBoardVR_API::IDragonBoardVR* g_dragonBoard = nullptr;
    DragonBoardVR_API::PanelHandle g_settingsPanel =
        DragonBoardVR_API::InvalidPanel;

    void OnSettingsEvent(
        const DragonBoardVR_API::PanelEvent* event,
        void*) noexcept
    {
        if (!event || !event->elementId) return;

        if (event->type == DragonBoardVR_API::PanelEventType::Change &&
            std::strcmp(event->elementId, "volume") == 0) {
            // Callback runs on Skyrim's game thread.
            MyMod::SetVolume(event->numericValue);
        } else if (event->type == DragonBoardVR_API::PanelEventType::Click &&
                   std::strcmp(event->elementId, "save") == 0) {
            MyMod::SaveSettings();
            g_dragonBoard->SetElementText(
                g_settingsPanel, "status", "Saved");
        }
    }
}

void RegisterDragonBoardPanel()
{
    g_dragonBoard = DragonBoardVR_API::RequestPluginAPI();
    if (!g_dragonBoard) return;

    const DragonBoardVR_API::PanelDescriptor descriptor{
        .id = "Example.MyMod.Settings",
        .documentPath = "Data/SKSE/Plugins/MyMod/ui/settings.rml",
        .onEvent = &OnSettingsEvent,
        .userData = nullptr
    };
    g_settingsPanel = g_dragonBoard->RegisterPanel(&descriptor);
}

void OpenDragonBoardSettings()
{
    if (g_dragonBoard &&
        g_settingsPanel != DragonBoardVR_API::InvalidPanel) {
        g_dragonBoard->ShowPanel(g_settingsPanel);
    }
}
```

`ShowPanel()` opens DragonBoard when necessary and selects the external
document. `UnregisterPanel()` must be called before the consumer plugin's
callback or `userData` becomes invalid.

### DOM update methods

All DOM update methods are thread-safe requests applied on a later Present:

- `SetElementText()` replaces escaped text content.
- `SetElementAttribute()` sets an attribute. Pass `nullptr` as the value to
  remove it; an empty string creates an empty attribute.
- `SetElementClass()` enables or disables one class.

These methods return whether the request was accepted, not whether the target
element was found later on the render thread. Missing panels/elements are
logged by DragonBoardVR.

### Event contract

The callback receives:

- `Click`: `elementId` plus the element's optional `value` attribute.
- `Change`: `elementId`, a textual numeric value and `numericValue`.

The callback and its string pointers are valid only for the duration of the
call. Copy values that must outlive it.

Add `data-dragonboard-action="close"` to a control when DragonBoard should
close the hosted panel after emitting its click event:

```xml
<button id="close" data-dragonboard-action="close">Close</button>
```

Click haptics can be selected declaratively:

```xml
<button id="save" data-haptic="normal">Save</button>
<button id="delete" data-haptic="strong">Delete</button>
<button id="disabled" data-haptic="error">Unavailable</button>
```

Supported values are `none`, `light`, `normal`, `strong` and `error`.

## Papyrus API

Papyrus-only mods use the native `DragonBoardVR` script instead of the C++
header. Both entry points register documents in the same `RmlPanelHost`; this
is not a second renderer or a separate panel system.

The consumer should attach its integration script to a player
`ReferenceAlias`. Call `RegisterPanel()` from `OnInit()` and again from
`OnPlayerLoadGame()` because DragonBoardVR clears Papyrus registrations while
switching saves.

```papyrus
Scriptname ExampleDragonBoardAlias extends ReferenceAlias

Int Panel = 0
GlobalVariable Property MyModVolume Auto

Event OnInit()
    RegisterDragonBoardPanel()
EndEvent

Event OnPlayerLoadGame()
    RegisterDragonBoardPanel()
EndEvent

Function RegisterDragonBoardPanel()
    If Panel != 0
        DragonBoardVR.UnregisterPanel(Self, Panel)
    EndIf

    Panel = DragonBoardVR.RegisterPanel(
        Self,
        "Example.MyMod.Settings",
        "Data/SKSE/Plugins/MyMod/ui/settings.rml")
EndFunction

Function OpenSettings()
    If Panel != 0
        DragonBoardVR.ShowPanel(Panel)
    EndIf
EndFunction

Event OnDragonBoardPanelEvent(Int panelHandle, String eventType, String elementId, String value, Float numericValue)

    If panelHandle != Panel
        Return
    EndIf

    If eventType == "Change" && elementId == "volume"
        MyModVolume.SetValue(numericValue)
    ElseIf eventType == "Click" && elementId == "save"
        DragonBoardVR.SetElementText(Panel, "status", "Saved")
    EndIf
EndEvent
```

The `DragonBoardVR.psc` compile-time stub is installed under
`Scripts/Source`. DragonBoardVR releases must also contain the compiled runtime
stub at `Scripts/DragonBoardVR.pex`. Generate it with:

```powershell
.\Tools\CompilePapyrus.ps1 `
  -CompilerPath "C:\Tools\Caprica\Caprica.exe" `
  -GameSourcePath "C:\SkyrimVR\Data\Scripts\Source"
```

The script accepts either Caprica with Skyrim support or Bethesda's
`PapyrusCompiler.exe`.

Papyrus exposes `RegisterPanel`, `UnregisterPanel`, `ShowPanel`, `HidePanel`,
`IsPanelVisible`, `SetElementText`, `SetElementAttribute`,
`RemoveElementAttribute` and `SetElementClass`. The receiver alias owns its
handle, and attempts to unregister a panel owned by another alias are rejected.

## Minimal external document

```xml
<rml>
<head>
    <title>My Mod Settings</title>
    <link type="text/rcss" href="settings.rcss" />
</head>
<body>
    <main>
        <h1>My Mod</h1>
        <label for="volume">Volume</label>
        <input id="volume" type="range" min="0" max="1" step="0.05" value="0.5" />
        <div id="status">Ready</div>
        <button id="save">Save</button>
        <button id="close" data-dragonboard-action="close">Close</button>
    </main>
    <div id="vr-cursor"></div>
</body>
</rml>
```

External documents should keep interactive controls static after load. Text,
attributes and classes can change freely; dynamically replacing markup with
new buttons does not automatically register those newly created controls in
the VR geometric fallback.

## Haptics and thread boundary

RmlUi produces abstract haptic cues on Present. `RmlPanelHost` transfers the
highest-priority cue to the game thread, where `VRMenuManager::triggerHaptic()`
uses the configured dominant hand and intensity. External callbacks should not
invoke OpenVR haptics for ordinary UI feedback.

## Preview and build

The standalone previewer uses the production RmlUi renderer but has no SKSE,
CommonLib, Present hook or game state:

```powershell
xmake build DragonBoardRmlPreview
xmake run DragonBoardRmlPreview "C:\path\to\panel.rml"
```

Build the plugin with:

```powershell
xmake build DragonBoardVR
```

The build copies the DLL and built-in RML/RCSS files into
`install_output/SKSE/Plugins/DragonBoardVR/`. It also generates the independent
status NIF and installs its texture under `install_output/meshes/DragonBoardVR/`
and `install_output/textures/`.

## Regression checklist

1. Open DragonBoard with `Grip + Y` in normal and left-handed modes.
2. Open Settings, Developer and Item Editor.
3. Scroll with grip; confirm trigger never scrolls the page.
4. Click tabs/buttons and drag built-in sliders.
5. Confirm the Item Editor's live 3D preview remains visible.
6. Register an external panel, show it and update text from its callback.
7. Confirm external `click` and `change` callbacks run on the game thread.
8. Close/unregister the external panel and confirm the scene quad hides.
9. Open DragonBoard on Home and confirm the status widget renders its RML
   content on the first frame instead of the placeholder DDS.
10. Open each main page and confirm the status widget hides.
11. Return Home after each page and confirm the widget does not inherit the
    last page texture.
12. Grab the status widget with one hand, add the second hand to scale it, and
    confirm the main panel transform remains unchanged.
13. Close/reopen DragonBoard and restart Skyrim to exercise scene and render
    resource recreation.
