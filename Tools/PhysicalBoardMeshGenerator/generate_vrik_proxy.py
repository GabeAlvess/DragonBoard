from __future__ import annotations

import argparse
import math
import os
import struct
import sys
from pathlib import Path
from shutil import copy2


TEMPLATE_GEOMETRY_SCALE = 0.0001


def find_pynifly_root(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if configured := os.environ.get("PYNIFLY_ROOT"):
        candidates.append(Path(configured))
    appdata = Path(os.environ.get("APPDATA", ""))
    blender_root = appdata / "Blender Foundation" / "Blender"
    if blender_root.exists():
        candidates.extend(sorted(
            blender_root.glob("*/scripts/addons/io_scene_nifly"),
            reverse=True))
    for candidate in candidates:
        if (candidate / "pyn" / "pynifly.py").is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "PyNifly was not found. Pass --pynifly-root or set PYNIFLY_ROOT.")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--visual", required=True)
    parser.add_argument("--weapon-template", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--hidden-output")
    parser.add_argument("--inventory-rotate-x", type=float, default=0.0)
    parser.add_argument("--inventory-rotate-y", type=float, default=0.0)
    parser.add_argument("--inventory-rotate-z", type=float, default=0.0)
    parser.add_argument("--inventory-zoom", type=float, default=1.0)
    parser.add_argument("--pynifly-root")
    args = parser.parse_args()

    pynifly_root = find_pynifly_root(args.pynifly_root)
    sys.path.insert(0, str(pynifly_root))

    from pyn.nifdefs import NODEID_NONE
    from pyn.pynifly import NifFile

    if args.inventory_zoom <= 0.0:
        raise ValueError("--inventory-zoom must be greater than zero")

    full_turn = round(2.0 * math.pi * 1000.0)
    inventory_rotation = tuple(
        round(math.radians(angle) * 1000.0) % full_turn
        for angle in (
            args.inventory_rotate_x,
            args.inventory_rotate_y,
            args.inventory_rotate_z,
        )
    )

    def patch_inventory_marker(path: Path) -> None:
        nif = NifFile(str(path))
        marker = nif.rootNode.get_extra_data(blockname="BSInvMarker")
        if marker is None:
            raise ValueError(f"Missing BSInvMarker in {path}")
        old_bytes = struct.pack("<HHHf", *marker.rotation, marker.zoom)
        new_bytes = struct.pack(
            "<HHHf", *inventory_rotation, args.inventory_zoom)
        data = bytearray(path.read_bytes())
        offset = data.find(old_bytes)
        if offset < 0 or data.find(old_bytes, offset + 1) >= 0:
            raise ValueError(f"Could not uniquely locate BSInvMarker in {path}")
        data[offset:offset + len(old_bytes)] = new_bytes
        path.write_bytes(data)

    def suppress_template_geometry(nif: NifFile) -> None:
        for shape in nif.shapes:
            shape.flags = int(shape.flags) | 1
            transform = shape.transform
            transform.scale = TEMPLATE_GEOMETRY_SCALE
            shape.transform = transform

    visual_path = Path(args.visual).resolve()
    weapon_template_path = Path(args.weapon_template).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    copy2(weapon_template_path, output_path)

    if args.hidden_output:
        hidden_output_path = Path(args.hidden_output).resolve()
        hidden_output_path.parent.mkdir(parents=True, exist_ok=True)
        copy2(weapon_template_path, hidden_output_path)
        hidden = NifFile(str(hidden_output_path))
        hidden.rootNode.name = "DragonBoardVrikProxyHidden"
        suppress_template_geometry(hidden)
        hidden.save()
        patch_inventory_marker(hidden_output_path)
        print(f"Generated hidden first-person proxy {hidden_output_path}")

    visual = NifFile(str(visual_path))
    destination = NifFile(str(output_path))
    destination.rootNode.name = "DragonBoardVrikProxy"

    suppress_template_geometry(destination)

    source = visual.shapes[0]
    properties = source.properties.copy()
    for attribute in (
        "nameID",
        "controllerID",
        "collisionID",
        "skinInstanceID",
        "shaderPropertyID",
        "alphaPropertyID",
    ):
        setattr(properties, attribute, NODEID_NONE)

    board = destination.createShapeFromData(
        "DragonBoard",
        source.verts,
        source.tris,
        source.uvs,
        source.normals,
        props=properties,
        use_type=source.properties.bufType,
        parent=destination.rootNode)
    board.flags = source.flags
    board.shader.name = source.shader.name
    board.shader._properties = source.shader.properties.copy()
    board.save_shader_attributes()
    for slot, texture_path in source.textures.items():
        if texture_path:
            board.set_texture(slot, texture_path)

    if source.has_alpha_property:
        board.has_alpha_property = True
        board.alpha_property._properties = source.alpha_property.properties.copy()
        board.save_alpha_property()

    destination.save()
    patch_inventory_marker(output_path)

    print(f"Generated {output_path}")
    print(f"Weapon collision template: {weapon_template_path}")
    print(f"Visible board geometry: {len(board.verts)} vertices, {len(board.tris)} triangles")


if __name__ == "__main__":
    main()
