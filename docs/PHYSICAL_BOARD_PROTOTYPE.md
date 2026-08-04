# Physical DragonBoard Prototype

This worktree contains the first in-world DragonBoard carrier implementation.

## Included artifacts

- `DragonBoardVR.esp` with `DragonBoardVRPhysicalBoard` (`MISC`, local FormID `0x000800`).
- Belethor sells 3 copies per merchant chest reset; the item's base value is 100 gold.
- `meshes/DragonBoardVR/dragonboard_physical.nif`, using the same visual geometry as `dragonboard.nif` plus rigid-body collision for HIGGS.
- Native HIGGS grab, drop, and shoulder-stash callbacks.
- Dynamic menu-hand routing: the holding hand carries the board and the opposite hand controls pointer/touch input.
- Automatic opening while held and full panel closure when dropped or stashed.

## Install

Install the contents of `install_output` as a mod and enable `DragonBoardVR.esp`.
HIGGS is required for physical grab/release events. VRIK remains optional for the
body and hand skeleton.

## Spawn and test

Open the console and run:

```text
help "DragonBoard"
player.placeatme <runtime FormID shown by help>
```

Then verify both hands separately:

1. Grab the item with HIGGS.
2. Confirm the board opens on the physical item.
3. Confirm the opposite hand controls the pointer and touch interaction.
4. Drop the item and confirm every DragonBoard panel closes.
5. Grab it again and stash it over the shoulder; confirm the UI closes and the item enters inventory.

Physical board size can be tuned under `[PhysicalBoard]` in
`SKSE/Plugins/DragonBoardVR.ini`. The NIF contains the former `1.55` baseline, so
`fScale = 1.0` is the authored physical size. The setting scales the visual,
collision, and attached UI together, independently from `[Background]`. The UI
keeps its authored `1.55` baseline internally, so `fScale = 1.0` preserves the
previous panel size. It uses the physical reference root directly, without
separate offset or rotation keys.

## VRIK holster proxy

The real board remains a non-equippable MISC item. The plugin adds one internal
dagger-type WEAP proxy while the player owns a DragonBoard so VRIK can manage
the visual in its native holster system.

1. Equip `DragonBoard (VRIK Holster)` from the weapons inventory.
2. Place it in any VRIK slot that accepts small weapons.
3. Draw it from that slot with either permitted hand.
4. DragonBoardVR unequips the proxy, hides that slot, spawns one physical MISC,
   and transfers it to HIGGS.
5. Stash the physical board over the shoulder to return it to inventory and
   restore the proxy in the same VRIK slot.

A normal world drop keeps the source slot hidden while that physical board
remains outside the inventory. Opening the map from the board stores it first
and restores the VRIK proxy before `MapMenu` opens.

The proxy uses local FormID `0x000801`; the physical MISC remains `0x000800`.
VRIK controls slot position, hover spheres, hand restrictions, and compatibility.

## Initial VRIK assignment

The first VRIK assignment keeps the weapon-safe proxy equipped and visible. It does not remove or spawn the physical MISC. After VRIK has assigned the proxy to a slot, drawing that slot unequips the proxy and spawns the HIGGS-grabbable physical board.
