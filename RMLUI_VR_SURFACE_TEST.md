# DragonBoardVR RmlUI surface test

## Runtime requirement

- Install the current `DragonBoardVR.dll`, RML/RCSS documents, generated screen
  NIFs and `RmlUI0.dds` through `RmlUI3.dds` from `install_output`.
- Restart Skyrim VR after changing a NIF or DDS path so cached shader resources
  cannot retain the previous surface binding.

## In-game test

1. Open the DragonBoard normally.
2. Open Settings and confirm the RmlUI page is composited on the board surface.
3. Point at the page and confirm the pointer, hover state and click target agree.
4. Open the status widget and confirm it uses an independent surface.
5. Open the shared keyboard and confirm its surface does not display the main
   panel or status-widget texture.
6. Run the welcome tutorial and confirm its surface does not reuse another
   panel's last rendered frame.
7. Move and scale the board, then confirm the page and pointer remain aligned.
8. Save a setting, restart the game and confirm the value and language persist.

## Expected log

`DragonBoardVR.log` should contain successful RmlUi initialization and surface
attachment messages for the requested panels. It must not report a missing
`RmlUIScreen.nif` or an unusable dedicated diffuse texture.

## Isolation contract

Each independently rendered surface requires its own NIF texture path:

- `RmlUI0.dds`: main panel surface;
- `RmlUI1.dds`: status widget;
- `RmlUI2.dds`: shared keyboard;
- `RmlUI3.dds`: welcome tutorial.

The DDS pixels may be identical because runtime rendering replaces the backing
renderer texture. The paths must remain unique because Skyrim caches texture
and shader resources by path.
