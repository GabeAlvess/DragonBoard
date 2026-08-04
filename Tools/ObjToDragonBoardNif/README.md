# OBJ/FBX to DragonBoard NIF

Converts OBJ and FBX files from a single file, folder, or ZIP into Skyrim SE/VR
NIF files sized for DragonBoard. Mixed OBJ/FBX batches are supported. The
generated directory is a ready-to-copy `Data` package with meshes, DDS textures,
and a JSON conversion report.

## Quick use

Drag an OBJ, FBX, folder, or ZIP onto `Convert-ToDragonBoardNif.cmd`.

Or run:

```powershell
python Tools/ObjToDragonBoardNif/convert.py "C:\Meshes\my_model.fbx"
```

The default output is created beside the input as `<name>_dragonboard_nif`:

```text
meshes/DragonBoardVR/Converted/<model>.nif
textures/DragonBoardVR/Converted/<input-name>/...
conversion_report.json
```

## Scaling

Every fit mode preserves the original mesh proportions. The converter never
applies different scale values per axis.

- `--fit-mode face` is the default. It uniformly fits the model inside the two
  largest axes of `dragonboard_physical.nif` and ignores board thickness.
- `--fit-mode longest` preserves proportions and matches the largest dimension.
- `--fit-mode envelope` fits all three dimensions inside the board bounds.
- `--fit-mode none` keeps the imported OBJ size and only centers it.
- `--scale 0.5` applies an additional uniform multiplier.
- `--rotate-x`, `--rotate-y`, and `--rotate-z` bake orientation into geometry.
- `--post-scale` uniformly scales around the NIF origin after fitting.
- `--forward-axis` and `--up-axis` control OBJ import orientation.

## Vanilla inventory preview

The generated NIF contains a separate `BSInvMarker` used only by Skyrim's
original inventory menu. It does not alter the mesh, collision, DragonBoard
orientation, buttons, or RmlUi surfaces.

- The default inventory rotation is `0, 0, 0`, which views the board straight
  along its thin Y axis instead of tilting it upward.
- The default `--inventory-zoom 1.0` is shared by the physical board and VRIK
  proxy inventory previews.
- Use `--inventory-rotate-x`, `--inventory-rotate-y`,
  `--inventory-rotate-z`, and `--inventory-zoom` for model-specific tuning.

In NifSkope, the same values can be edited under the root node's `BSInvMarker`.
Rotation is stored in milliradians (`1570` is approximately 90 degrees), while
zoom is a floating-point value. For the DragonBoard baseline use rotation
`0, 0, 0` and zoom `1.0`.

## Collision

Each imported OBJ/FBX mesh object produces a convex Havok collision part. Multiple
parts are stored in a `bhkListShape`, which follows compound meshes more closely
than a single box while remaining suitable for a movable HIGGS object.

Concave detail inside one mesh object is represented by its convex hull. Split a
concave model into multiple named objects/groups for more accurate collision.
Very thin meshes receive `--minimum-thickness 0.10` so Havok gets a valid volume.

Use `--no-collision` only for visual-only NIFs.

## Textures

DDS files are copied. PNG, JPEG, BMP, and TGA files inside the input tree are
converted to DDS. Imported diffuse material images are assigned automatically;
materials without an image receive a generated white texture and flat normal map.

## Requirements

- Blender 5.1, or pass `--blender` / set `BLENDER_EXE`.
- PyNifly Blender addon, or pass `--pynifly-root` / set `PYNIFLY_ROOT`.
- Python with Pillow.

Run `python Tools/ObjToDragonBoardNif/convert.py --help` for every option.

The non-physical DragonBoard overwrites the loaded NIF root transform from
`MainTablet` every frame. Use the rotation and post-scale options instead of
editing the root in NifSkope; these options bake the transform into visual and
collision geometry.

## RmlUi surface transforms

Baked geometry can use identity shape transforms, so DragonBoardVR cannot infer
the model's front-face basis from a shape named `DragonBoard`. Position, rotation,
and scale are configurable independently for the non-physical and physical boards.

Non-physical board in `DragonBoardVR_Layout.ini`:

```ini
[Background]
fRmlSurfaceOffsetX = 0.000000
fRmlSurfaceOffsetY = 0.000000
fRmlSurfaceOffsetZ = 0.000000
fRmlSurfaceRotX = 90.000000
fRmlSurfaceRotY = 0.000000
fRmlSurfaceRotZ = 180.000000
fRmlSurfaceScale = 1.000000
```

Physical board in `DragonBoardVR.ini`:

```ini
[PhysicalBoard]
fRmlSurfaceOffsetX = 0.000000
fRmlSurfaceOffsetY = 0.000000
fRmlSurfaceOffsetZ = 0.000000
fRmlSurfaceRotX = 90.000000
fRmlSurfaceRotY = 0.000000
fRmlSurfaceRotZ = 180.000000
fRmlSurfaceScale = 1.000000
```

These values affect the RmlUi surface only. They do not rotate, move, or scale the
background mesh or collision. Physical `fRmlSurfaceScale` is an additional
multiplier on top of the physical board's authored UI scale.
## Physical mesh-only scale

Use the following physical-board settings when only the NIF mesh and its
collision should change size:

```ini
[PhysicalBoard]
fScale = 1.000000
fMeshScale = 1.200000
```

`fScale` remains the shared scale for the physical mesh, collision, buttons, and
RmlUi attachments. `fMeshScale` is an additional multiplier applied only to the
physical NIF mesh and `DragonBoardCollision`; it does not resize buttons or
RmlUi. The effective mesh/collision scale is `fScale * fMeshScale`.
