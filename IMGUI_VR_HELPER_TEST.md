# DragonBoardVR ImGui settings test

## Runtime requirement

- For the physical-surface test, install both DLLs from
  `install_output/SKSE/Plugins`. The bundled `imgui-vr-helper.dll` is a local
  1.8.0-compatible build with client API revision 006; the upstream 1.8.0 DLL
  will still open the menu, but will fall back to its own pose and raycast.
- In the Helper settings, set **Overlay placement / Attach mode** to
  **Controller only**.
- Set **Attach controller** to the secondary controller. This keeps the settings
  panel following the same hand used by the DragonBoard in the usual layout.

The Helper currently exposes placement as a global setting. DragonBoardVR does
not overwrite it, because doing so would also move the panels belonging to other
mods.

## In-game test

1. Open the DragonBoard normally.
2. Press its Settings button.
3. Confirm that the ImGui panel is composited on the DragonBoard surface and
   follows its full position and rotation.
4. Point at it with the DragonBoard beam. The ImGui cursor must land at the same
   position as the DragonBoard raycast and respond to the opposite controller's
   trigger.
5. In **Position**, set **Rotation Z** to `0`. The DragonBoard front and the
   ImGui text must both face the player; the ImGui panel must not be mirrored.
6. Move with the menu-hand thumbstick while the ImGui panel is open. Locomotion
   must continue, and that thumbstick must not scroll the settings panel.
7. Change a setting and confirm that the DragonBoard updates while the slider is
   moved.
8. Press **Save INI**, close and reopen the board, and confirm that the value was
   persisted.

If ImGui VR Helper is missing or rejects the client registration, the original
3D MCM panel opens instead.

## Expected log

`DragonBoardVR.log` must contain:

- `ImGui VR Helper physical-surface API 006 available`
- `Publishing ImGui surface 'Background_Panel' size ... in world space ...`
- `Migrated menu rotation to zero-based front-face convention ...`

If the API 006 line is absent, the old Helper DLL is winning the mod-manager
conflict. If the publishing line reports a suspiciously small surface,
keep that exact size in the test report; it identifies a DragonBoard layout
extent problem rather than a VR transform problem.

## Scope of this revision

The physical host path is currently used by DragonBoardVR Settings. It adds a
general per-client surface/UV contract to the Helper, but does not yet enumerate
or launch panels registered by other mods. That registry/host layer is the next
step after surface orientation, cursor alignment and input are validated.
