# Physical Board Mesh Generator

Builds `dragonboard_physical.nif` from the real DragonBoard visual mesh and adds
a proportional dynamic Havok box collision for HIGGS.

```powershell
python Tools/PhysicalBoardMeshGenerator/generate_physical_board.py `
  --visual Assets/meshes/DragonBoardVR/dragonboard.nif `
  --collision-template Tools/PhysicalBoardMeshGenerator/dragon_tablet_collision_template.nif `
  --output Assets/meshes/DragonBoardVR/dragonboard_physical.nif `
  --collision-half-depth 0.15 `
  --collision-margin 0.05
```

`--collision-template` only supplies proven Skyrim rigid-body/material settings.
The default collision is inset behind the visible face so the HIGGS hand can
reach touch targets without losing the board's rigid-body collision.
Set `PYNIFLY_ROOT` or pass `--pynifly-root` when PyNifly is not installed as a
Blender addon.

## VRIK weapon proxy

Builds `dragonboard_vrik_proxy.nif` from a valid melee weapon NIF. The
weapon collision and metadata remain intact, the template geometry is hidden,
and the DragonBoard geometry is added as the only visible shape.

```powershell
python Tools/PhysicalBoardMeshGenerator/generate_vrik_proxy.py `
  --visual Assets/meshes/DragonBoardVR/dragonboard.nif `
  --weapon-template "I:/Games/Skyrim VR FUS/mods/Cathedral - Armory/meshes/weapons/iron/IronDagger.nif" `
  --output Assets/meshes/DragonBoardVR/dragonboard_vrik_proxy.nif
```
