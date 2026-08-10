# Physical Board Plugin Generator

Generates `Assets/DragonBoardVR.esp` with the physical DragonBoard carrier item.
The generated plugin is ESL-flagged and uses compact-range local FormIDs.

```powershell
dotnet run --project Tools/PhysicalBoardPluginGenerator -- Assets/DragonBoardVR.esp
```

The generator reads `Skyrim.esm` for the VRIK proxy weapon template. Pass the
master as the second argument or set `SKYRIMVR_MASTER_PATH` when it cannot be
found automatically:

```powershell
dotnet run --project Tools/PhysicalBoardPluginGenerator -- Assets/DragonBoardVR.esp "X:\SkyrimVR\Data\Skyrim.esm"
```

The generated records are:

- `DragonBoardVRPhysicalBoard`: `MISC`, local FormID `0x000800`, physical HIGGS model, value 100 gold.
- `DragonBoardVRVrikHolsterProxy`: harmless dagger-type `WEAP`, local FormID `0x000801`, visible while assigning the first VRIK holster. The physical MISC is spawned only when drawing from an already assigned slot.
- No vanilla container or leveled-list records are overridden.
- `Assets/integrations/skypatcher` adds ten weighted DragonBoard entries to
  `LItemMiscVendorMiscItems75` at runtime. Vendor distribution requires
  SkyPatcher with leveled-list patching enabled.

SkyPatcher modifies the winning runtime leveled list, preserving entries from
the plugin that wins the normal load-order conflict.

The physical mesh uses the same DragonBoard visual geometry as `dragonboard.nif` and adds a proportional dynamic Havok box collision for HIGGS. Rebuild it with `Tools/PhysicalBoardMeshGenerator/generate_physical_board.py`.

