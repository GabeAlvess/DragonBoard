from __future__ import annotations

import argparse
import math
import os
import sys
from pathlib import Path


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


def transformed_bounds(shape) -> tuple[list[float], list[float]]:
    transform = shape.transform
    world_vertices = [
        tuple(
            transform.translation[axis] + transform.scale * sum(
                transform.rotation[axis][component] * vertex[component]
                for component in range(3))
            for axis in range(3))
        for vertex in shape.verts
    ]
    minimum = [min(vertex[axis] for vertex in world_vertices) for axis in range(3)]
    maximum = [max(vertex[axis] for vertex in world_vertices) for axis in range(3)]
    return minimum, maximum


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--visual", required=True)
    parser.add_argument("--collision-template", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument(
        "--collision-half-depth",
        type=float,
        default=0.15,
        help="Collision half-depth in DragonBoard visual units (default: 0.15)")
    parser.add_argument(
        "--collision-margin",
        type=float,
        default=0.05,
        help="Havok convex radius in DragonBoard visual units (default: 0.05)")
    parser.add_argument(
        "--baked-scale",
        type=float,
        default=1.55,
        help="Scale baked into visual vertices and generated collision (default: 1.55)")
    parser.add_argument("--inventory-rotate-x", type=float, default=0.0)
    parser.add_argument("--inventory-rotate-y", type=float, default=0.0)
    parser.add_argument("--inventory-rotate-z", type=float, default=0.0)
    parser.add_argument("--inventory-zoom", type=float, default=1.0)
    parser.add_argument("--pynifly-root")
    args = parser.parse_args()

    pynifly_root = find_pynifly_root(args.pynifly_root)
    sys.path.insert(0, str(pynifly_root))

    from pyn.pynifly import BSInvMarker, BSXFlags, NifFile
    from pyn.nifdefs import NODEID_NONE, PynBufferTypes, bhkBoxShapeProps
    from pyn.structs import TransformBuf

    visual_path = Path(args.visual).resolve()
    collision_template_path = Path(args.collision_template).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if args.baked_scale <= 0.0 or args.inventory_zoom <= 0.0:
        raise ValueError("--baked-scale and --inventory-zoom must be greater than zero")

    visual = NifFile(str(visual_path))
    source = visual.shapes[0]
    scaled_vertices = [
        tuple(component * args.baked_scale for component in vertex)
        for vertex in source.verts
    ]

    destination = NifFile()
    destination.initialize(
        visual.game,
        str(output_path),
        type(visual.rootNode).__name__,
        visual.rootNode.name)
    destination.rootNode.flags = visual.rootNode.flags

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
        scaled_vertices,
        source.tris,
        source.uvs,
        source.normals,
        props=properties,
        use_type=source.properties.bufType,
        parent=destination.rootNode)
    board.flags = source.flags
    board.transform = source.transform
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

    template = NifFile(str(collision_template_path))
    template_node = next(
        node for node in template.nodes.values() if node.collision_object)
    template_collision = template_node.collision_object
    template_body = template_collision.body
    template_shape = template_body.shape

    template_root = template.rootNode
    template_bsx = template_root.get_extra_data(blockname="BSXFlags")
    if template_bsx:
        BSXFlags.New(
            destination,
            name=template_bsx.name,
            flags=int(template_bsx.flags),
            parent=destination.rootNode)

    template_marker = template_root.get_extra_data(blockname="BSInvMarker")
    full_turn = round(2.0 * math.pi * 1000.0)
    inventory_rotation = [
        round(math.radians(angle) * 1000.0) % full_turn
        for angle in (
            args.inventory_rotate_x,
            args.inventory_rotate_y,
            args.inventory_rotate_z,
        )
    ]
    BSInvMarker.New(
        destination,
        name=template_marker.name if template_marker else "INV",
        rotation=inventory_rotation,
        zoom=args.inventory_zoom,
        parent=destination.rootNode)

    minimum, maximum = transformed_bounds(destination.shapes[0])
    center_units = [(minimum[i] + maximum[i]) * 0.5 for i in range(3)]
    half_units = [(maximum[i] - minimum[i]) * 0.5 for i in range(3)]
    half_units[1] = min(
        half_units[1],
        max(
            0.02 * args.baked_scale,
            args.collision_half_depth * args.baked_scale))
    havok_scale = 69.99125
    center_havok = [value / havok_scale for value in center_units]
    half_havok = [max(0.0005, value / havok_scale) for value in half_units]

    collision_node = destination.add_node(
        "DragonBoardCollision",
        TransformBuf(),
        parent=destination.rootNode)
    collision_node.flags = template_node.flags
    collision = collision_node.add_collision(
        body=None,
        flags=template_collision.properties.flags,
        collision_type=PynBufferTypes.bhkCollisionObjectBufType)
    body_properties = template_body.properties.copy()
    body_properties.shapeID = 0xFFFFFFFF
    body_properties.mass = 2.0
    for index in range(3):
        body_properties.translation[index] = center_havok[index]
        body_properties.center[index] = 0.0
    body_properties.translation[3] = 0.0
    body_properties.center[3] = 0.0

    width, depth, height = [2.0 * value for value in half_havok]
    inertia = [
        body_properties.mass * (depth * depth + height * height) / 12.0,
        body_properties.mass * (width * width + height * height) / 12.0,
        body_properties.mass * (width * width + depth * depth) / 12.0,
    ]
    for index in range(12):
        body_properties.inertiaMatrix[index] = 0.0
    body_properties.inertiaMatrix[0] = inertia[0]
    body_properties.inertiaMatrix[5] = inertia[1]
    body_properties.inertiaMatrix[10] = inertia[2]

    body = collision.add_body(body_properties)
    box_properties = bhkBoxShapeProps()
    box_properties.bhkMaterial = template_shape.properties.bhkMaterial
    requested_margin_havok = (
        max(0.01, args.collision_margin) * args.baked_scale / havok_scale)
    box_properties.bhkRadius = min(
        template_shape.properties.bhkRadius,
        requested_margin_havok,
        min(half_havok) * 0.5)
    for index, value in enumerate(half_havok):
        box_properties.bhkDimensions[index] = value
    body.add_shape(box_properties)
    destination.save()

    print(f"Generated {output_path}")
    print(f"Baked visual scale: {args.baked_scale}")
    print(f"Visual bounds: {minimum} to {maximum}")
    print(f"Havok half extents: {half_havok}")
    print(f"Collision margin: {box_properties.bhkRadius} Havok units")


if __name__ == "__main__":
    main()
