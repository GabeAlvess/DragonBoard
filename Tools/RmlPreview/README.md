# DragonBoard RmlUi Preview

Standalone Windows previewer for any RmlUi document. It renders at the same
logical resolution used in Skyrim VR (1920x1080) and reuses the production
D3D11 RmlUi renderer, but it does not load SKSE, CommonLib, Skyrim, the Present
hook or ImGui.

## Build and run

```powershell
xmake build DragonBoardRmlPreview
xmake run DragonBoardRmlPreview
```

Open a specific document directly:

```powershell
xmake run DragonBoardRmlPreview "C:\path\to\panel.rml"
```

After building, the executable also accepts the RML path directly:

```powershell
DragonBoardRmlPreview.exe "C:\path\to\panel.rml"
```

Without a path, the previewer opens DragonBoard's `settings.rml` when the
repository assets are available. Otherwise it displays the file picker.

## Controls

- `Ctrl+O` or `File > Open RML`: select any RML document
- drag and drop a `.rml` file onto the window
- `Documents` menu: switch between RML files in the current folder
- `F1`: Settings document
- `F2`: Developer document
- `F3`: Item Editor document
- `F5`: force a full RML/RCSS reload
- `F8`: toggle the RmlUi debugger
- `F9`: show or hide the visual inspector
- `Esc`: close the previewer

Changes to RML, RCSS and font files under the current document folder are
detected automatically. The document is reloaded without rebuilding or
restarting the program. Click and change events are captured at document level,
so newly created buttons and sliders respond without being registered in C++.

DragonBoard's game-dependent values and developer commands are deliberate mock
data. They test its layout and event bindings without touching Skyrim state.
Unknown documents are rendered generically and their interactions appear in
the window status.

## Visual inspector

The inspector opens beside the preview by default. Click an element in the
preview or select it in the DOM tree, then edit one or more properties and
press `Apply`. Supported fields currently include text/background color,
font size, dimensions, margin, padding, opacity, borders, display and text
alignment.

`Undo` and `Redo` operate on inspector changes. `Save RCSS` writes a
sidecar beside the current document:

```text
panel.rml
panel.editor-overrides.rcss
```

The previewer automatically reapplies that sidecar. To use it in the game,
reference it from the document head:

```xml
<link type="text/rcss" href="panel.editor-overrides.rcss" />
```

Elements with an ID generate a precise `#id` selector. Without an ID, the
inspector uses the first class, or finally the element tag; class and tag
changes can intentionally affect multiple matching elements.

## Diagnostic boundary

- A defect visible here usually belongs to RML, RCSS, font loading, event
  binding or `DragonBoardRmlRenderer`.
- A defect visible only in Skyrim usually belongs to the Present hook,
  render-target lifecycle, texture bridge, VR input bridge, threading or game
  integration.

The preview target is not built by default and is not copied into
`install_output`.
