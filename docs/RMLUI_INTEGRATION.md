# RmlUi panel framework

DragonBoardVR uses RmlUi 6.2 as its only flat-panel renderer. Settings,
Developer, Item Editor and panels registered by other SKSE plugins all render
to the same 1920x1080 texture attached to the physical DragonBoard. The board
therefore keeps its existing hand-following and world-pinned behavior.

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
2. The next valid Present creates the D3D11 render target and initializes one
   `DragonBoardRmlUi` context.
3. Opening a panel selects one document and makes the host visible.
4. The game thread derives the panel UV from the DragonBoard's actual scene
   transform. This keeps the texture and raycast aligned while the board
   follows the hand.
5. Present applies queued DOM commands, sends the VR pointer state to RmlUi and
   renders the active document.
6. RmlUi events are copied to a queue. External callbacks and all Skyrim-facing
   actions run later on the game thread.

No RmlUi event listener may call CommonLib or Skyrim gameplay APIs directly.

## Built-in documents

Built-in document lookup uses this order:

1. `Data/SKSE/Plugins/DragonBoardVR/ui/<name>.rml`
2. `SKSE/Plugins/DragonBoardVR/ui/<name>.rml`
3. `Assets/ui/rml/<name>.rml` for local development

The documents are:

- `settings.rml` / `settings.rcss`
- `dev.rml` / `dev.rcss`
- `edit.rml` / `edit.rcss`

If a requested document cannot load, the host logs the error and rejects it.
Settings and Item Editor callers may then use their existing native 3D panel;
there is no ImGui fallback with independent behavior.

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
`install_output/SKSE/Plugins/DragonBoardVR/`.

## Regression checklist

1. Open DragonBoard with `Grip + Y` in normal and left-handed modes.
2. Open Settings, Developer and Item Editor.
3. Scroll with grip; confirm trigger never scrolls the page.
4. Click tabs/buttons and drag built-in sliders.
5. Confirm the Item Editor's live 3D preview remains visible.
6. Register an external panel, show it and update text from its callback.
7. Confirm external `click` and `change` callbacks run on the game thread.
8. Close/unregister the external panel and confirm the scene quad hides.
