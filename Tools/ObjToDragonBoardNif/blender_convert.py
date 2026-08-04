from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

import bmesh
import bpy
from mathutils import Euler, Vector


HAVOK_SCALE = 69.99125
MAX_VISUAL_VERTICES = 60000


def parse_args() -> argparse.Namespace:
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True)
    return parser.parse_args(arguments)


def transformed_bounds(shape) -> tuple[list[float], list[float]]:
    transform = shape.transform
    vertices = [
        tuple(
            transform.translation[axis] + transform.scale * sum(
                transform.rotation[axis][component] * vertex[component]
                for component in range(3))
            for axis in range(3))
        for vertex in shape.verts
    ]
    return (
        [min(vertex[axis] for vertex in vertices) for axis in range(3)],
        [max(vertex[axis] for vertex in vertices) for axis in range(3)],
    )


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)


def import_mesh(path: Path, forward_axis: str, up_axis: str) -> list[bpy.types.Object]:
    clear_scene()
    if path.suffix.lower() == ".obj":
        bpy.ops.wm.obj_import(
            filepath=str(path),
            forward_axis=forward_axis,
            up_axis=up_axis,
        )
    elif path.suffix.lower() == ".fbx":
        bpy.ops.import_scene.fbx(
            filepath=str(path),
            use_image_search=True,
        )
    else:
        raise ValueError(f"Unsupported mesh format: {path.suffix}")
    objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not objects:
        raise ValueError(f"Imported file has no mesh objects: {path}")
    return objects


def triangulated_world_mesh(obj: bpy.types.Object) -> bpy.types.Mesh:
    mesh = obj.data.copy()
    mesh.transform(obj.matrix_world)
    working = bmesh.new()
    working.from_mesh(mesh)
    bmesh.ops.triangulate(working, faces=list(working.faces))
    working.to_mesh(mesh)
    working.free()
    mesh.update(calc_edges=True)
    return mesh


def calculate_normalization(
        meshes: list[bpy.types.Mesh],
        reference_minimum: list[float],
        reference_maximum: list[float],
        fit_mode: str,
        scale_multiplier: float) -> tuple[Vector, Vector, Vector, list[float], list[float]]:
    all_vertices = [Vector(vertex.co) for mesh in meshes for vertex in mesh.vertices]
    minimum = Vector(tuple(min(vertex[axis] for vertex in all_vertices) for axis in range(3)))
    maximum = Vector(tuple(max(vertex[axis] for vertex in all_vertices) for axis in range(3)))
    dimensions = maximum - minimum
    if max(dimensions) <= 1e-8:
        raise ValueError("OBJ mesh has zero size")

    reference_min = Vector(reference_minimum)
    reference_max = Vector(reference_maximum)
    reference_dimensions = reference_max - reference_min
    if fit_mode == "face":
        face_axes = sorted(
            range(3),
            key=lambda axis: reference_dimensions[axis],
            reverse=True)[:2]
        ratios = [
            reference_dimensions[axis] / dimensions[axis]
            for axis in face_axes
            if dimensions[axis] > 1e-8
        ]
        uniform_scale = min(ratios)
        fit_scale = Vector((uniform_scale, uniform_scale, uniform_scale))
    elif fit_mode == "longest":
        uniform_scale = max(reference_dimensions) / max(dimensions)
        fit_scale = Vector((uniform_scale, uniform_scale, uniform_scale))
    elif fit_mode == "envelope":
        ratios = [
            reference_dimensions[axis] / dimensions[axis]
            for axis in range(3)
            if dimensions[axis] > 1e-8
        ]
        uniform_scale = min(ratios)
        fit_scale = Vector((uniform_scale, uniform_scale, uniform_scale))
    else:
        fit_scale = Vector((1.0, 1.0, 1.0))

    source_center = (minimum + maximum) * 0.5
    target_center = (reference_min + reference_max) * 0.5
    total_scale = fit_scale * scale_multiplier
    output_minimum = target_center + (minimum - source_center) * total_scale
    output_maximum = target_center + (maximum - source_center) * total_scale
    return source_center, target_center, total_scale, list(output_minimum), list(output_maximum)


def normalized_position(
        position: Vector,
        source_center: Vector,
        target_center: Vector,
        scale: Vector) -> Vector:
    return target_center + (position - source_center) * scale


def apply_post_transform(position: Vector, rotation, scale: float) -> Vector:
    return rotation @ (position * scale)


def normalized_texture_token(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.casefold())


def texture_for_material(
        material,
        texture_map: dict[str, str],
        kind_tokens: tuple[str, ...],
        default_key: str,
        allow_untyped_image: bool = False) -> str:
    mapped_images: list[tuple[str, str]] = []
    if material and material.node_tree:
        image_nodes = [
            node for node in material.node_tree.nodes
            if node.type == "TEX_IMAGE" and node.image and node.image.filepath
        ]
        linked_nodes = [node for node in image_nodes if node.outputs["Color"].is_linked]
        for node in linked_nodes + image_nodes:
            source = Path(bpy.path.abspath(node.image.filepath)).resolve()
            mapped = texture_map.get(str(source).casefold())
            if mapped:
                mapped_images.append((normalized_texture_token(source.stem), mapped))

    normalized_kinds = tuple(normalized_texture_token(token) for token in kind_tokens)
    for source_name, mapped in mapped_images:
        if any(token in source_name for token in normalized_kinds):
            return mapped
    if allow_untyped_image and mapped_images:
        return mapped_images[0][1]

    if material:
        material_name = normalized_texture_token(material.name)
        candidates: list[tuple[int, str]] = []
        for source, mapped in texture_map.items():
            if source.startswith("__"):
                continue
            source_name = normalized_texture_token(Path(source).stem)
            if material_name not in source_name:
                continue
            if not any(token in source_name for token in normalized_kinds):
                continue
            score = 2 if source_name.endswith(normalized_kinds) else 1
            candidates.append((score, mapped))
        if candidates:
            return max(candidates, key=lambda item: item[0])[1]

    return texture_map[default_key]


def safe_shape_name(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_]+", "_", value).strip("_")
    return (cleaned or "Mesh")[:48]


def visual_chunks(
        mesh: bpy.types.Mesh,
        source_center: Vector,
        target_center: Vector,
        scale: Vector,
        post_rotation,
        post_scale: float):
    uv_layer = mesh.uv_layers.active
    polygons_by_material: dict[int, list] = defaultdict(list)
    for polygon in mesh.polygons:
        polygons_by_material[polygon.material_index].append(polygon)

    for material_index, polygons in sorted(polygons_by_material.items()):
        chunk_index = 0
        vertices: list[tuple[float, float, float]] = []
        normals: list[tuple[float, float, float]] = []
        uvs: list[tuple[float, float]] = []
        triangles: list[tuple[int, int, int]] = []
        lookup: dict[tuple[float, ...], int] = {}

        def flush():
            nonlocal chunk_index, vertices, normals, uvs, triangles, lookup
            if not triangles:
                return None
            result = (material_index, chunk_index, vertices, triangles, uvs, normals)
            chunk_index += 1
            vertices, normals, uvs, triangles, lookup = [], [], [], [], {}
            return result

        for polygon in polygons:
            corner_data = []
            for loop_index in polygon.loop_indices:
                loop = mesh.loops[loop_index]
                position = apply_post_transform(
                    normalized_position(
                        mesh.vertices[loop.vertex_index].co,
                        source_center,
                        target_center,
                        scale),
                    post_rotation,
                    post_scale)
                normal = Vector(tuple(
                    loop.normal[axis] / scale[axis] for axis in range(3))).normalized()
                normal = (post_rotation @ normal).normalized()
                uv = uv_layer.data[loop_index].uv if uv_layer else (0.0, 0.0)
                nif_uv = (float(uv[0]), 1.0 - float(uv[1]))
                key = (
                    round(position.x, 6), round(position.y, 6), round(position.z, 6),
                    round(normal.x, 6), round(normal.y, 6), round(normal.z, 6),
                    round(nif_uv[0], 6), round(nif_uv[1], 6),
                )
                corner_data.append((key, position, normal, nif_uv))

            new_vertices = sum(1 for key, *_ in corner_data if key not in lookup)
            if triangles and len(vertices) + new_vertices > MAX_VISUAL_VERTICES:
                result = flush()
                if result:
                    yield result

            triangle = []
            for key, position, normal, nif_uv in corner_data:
                index = lookup.get(key)
                if index is None:
                    index = len(vertices)
                    lookup[key] = index
                    vertices.append(tuple(position))
                    normals.append(tuple(normal))
                    uvs.append(nif_uv)
                triangle.append(index)
            triangles.append(tuple(triangle))

        result = flush()
        if result:
            yield result


def sample_points(points: list[Vector], maximum: int) -> list[Vector]:
    unique = list({tuple(round(value, 7) for value in point): point for point in points}.values())
    if len(unique) <= maximum:
        return unique
    selected: list[Vector] = []
    remaining = unique[:]
    for axis in range(3):
        for point in (
                min(remaining, key=lambda item: item[axis]),
                max(remaining, key=lambda item: item[axis])):
            if point not in selected:
                selected.append(point)
    remaining = [point for point in remaining if point not in selected]
    while remaining and len(selected) < maximum:
        point = max(
            remaining,
            key=lambda candidate: min(
                (candidate - chosen).length_squared for chosen in selected))
        selected.append(point)
        remaining.remove(point)
    return selected


def convex_hull(
        points: list[Vector],
        minimum_thickness: float,
        maximum_vertices: int) -> tuple[
            list[tuple[float, float, float]],
            list[tuple[float, float, float, float]]]:
    minimum = Vector(tuple(min(point[axis] for point in points) for axis in range(3)))
    maximum = Vector(tuple(max(point[axis] for point in points) for axis in range(3)))
    dimensions = maximum - minimum
    thinnest_axis = min(range(3), key=lambda axis: dimensions[axis])
    if dimensions[thinnest_axis] < minimum_thickness:
        center = (minimum[thinnest_axis] + maximum[thinnest_axis]) * 0.5
        offset = minimum_thickness * 0.5
        expanded = []
        for point in points:
            low = point.copy()
            high = point.copy()
            low[thinnest_axis] = center - offset
            high[thinnest_axis] = center + offset
            expanded.extend((low, high))
        points = expanded

    sampled = sample_points(points, maximum_vertices)
    working = bmesh.new()
    for point in sampled:
        working.verts.new(point)
    working.verts.ensure_lookup_table()
    bmesh.ops.remove_doubles(working, verts=list(working.verts), dist=1e-6)
    bmesh.ops.convex_hull(working, input=list(working.verts), use_existing_faces=False)
    working.normal_update()
    hull_vertices = [tuple(vertex.co / HAVOK_SCALE) for vertex in working.verts]
    normals = []
    for face in working.faces:
        normal = face.normal.normalized()
        distance = -normal.dot(face.verts[0].co) / HAVOK_SCALE
        plane = (normal.x, normal.y, normal.z, distance)
        if not any(
                sum((plane[index] - item[index]) ** 2 for index in range(4)) < 1e-8
                for item in normals):
            normals.append(plane)
    working.free()
    if len(hull_vertices) < 4 or len(normals) < 4:
        raise ValueError("Could not create a three-dimensional convex collision hull")
    return hull_vertices, normals


def copy_shape_style(destination_shape, source_shape, diffuse: str, normal: str) -> None:
    destination_shape.flags = source_shape.flags
    destination_shape.shader.name = source_shape.shader.name
    destination_shape.shader._properties = source_shape.shader.properties.copy()
    destination_shape.save_shader_attributes()
    for slot, texture_path in source_shape.textures.items():
        if texture_path:
            destination_shape.set_texture(slot, texture_path)
    destination_shape.set_texture("Diffuse", diffuse)
    destination_shape.set_texture("Normal", normal)
    if source_shape.has_alpha_property:
        destination_shape.has_alpha_property = True
        destination_shape.alpha_property._properties = source_shape.alpha_property.properties.copy()
        destination_shape.save_alpha_property()


def add_collision(
        destination,
        collision_template,
        hulls,
        output_minimum,
        output_maximum,
        mass,
        margin,
        classes) -> None:
    PynBufferTypes = classes["PynBufferTypes"]
    TransformBuf = classes["TransformBuf"]
    bhkConvexVerticesShapeProps = classes["bhkConvexVerticesShapeProps"]
    bhkListShapeProps = classes["bhkListShapeProps"]

    template_node = next(
        node for node in collision_template.nodes.values() if node.collision_object)
    template_collision = template_node.collision_object
    template_body = template_collision.body
    template_shape = template_body.shape

    collision_node = destination.add_node(
        "DragonBoardCollision", TransformBuf(), parent=destination.rootNode)
    collision_node.flags = template_node.flags
    collision = collision_node.add_collision(
        body=None,
        flags=template_collision.properties.flags,
        collision_type=PynBufferTypes.bhkCollisionObjectBufType)
    body_properties = template_body.properties.copy()
    body_properties.shapeID = 0xFFFFFFFF
    body_properties.mass = mass
    for index in range(4):
        body_properties.translation[index] = 0.0
        body_properties.center[index] = 0.0

    dimensions = [
        max(0.001, (output_maximum[index] - output_minimum[index]) / HAVOK_SCALE)
        for index in range(3)
    ]
    inertia = [
        mass * (dimensions[1] ** 2 + dimensions[2] ** 2) / 12.0,
        mass * (dimensions[0] ** 2 + dimensions[2] ** 2) / 12.0,
        mass * (dimensions[0] ** 2 + dimensions[1] ** 2) / 12.0,
    ]
    for index in range(12):
        body_properties.inertiaMatrix[index] = 0.0
    body_properties.inertiaMatrix[0] = inertia[0]
    body_properties.inertiaMatrix[5] = inertia[1]
    body_properties.inertiaMatrix[10] = inertia[2]
    body = collision.add_body(body_properties)

    def properties():
        result = bhkConvexVerticesShapeProps(game=destination.game)
        result.bhkMaterial = template_shape.properties.bhkMaterial
        result.bhkRadius = min(
            template_shape.properties.bhkRadius,
            max(0.001, margin) / HAVOK_SCALE)
        return result

    if len(hulls) == 1:
        vertices, normals = hulls[0]
        body.add_shape(properties(), vertices=vertices, normals=normals)
        return

    list_properties = bhkListShapeProps(game=destination.game)
    list_properties.bhkMaterial = template_shape.properties.bhkMaterial
    list_shape = body.add_shape(list_properties)
    for vertices, normals in hulls:
        child = destination.add_shape(
            properties(), vertices=vertices, normals=normals)
        list_shape.add_child(child)


def convert_job(manifest: dict, job: dict, classes: dict) -> None:
    NifFile = classes["NifFile"]
    NODEID_NONE = classes["NODEID_NONE"]
    BSInvMarker = classes["BSInvMarker"]
    BSXFlags = classes["BSXFlags"]
    TransformBuf = classes["TransformBuf"]

    source_path = Path(job["source"])
    output_path = Path(job["output"])
    objects = import_mesh(source_path, manifest["forward_axis"], manifest["up_axis"])
    meshes = [triangulated_world_mesh(obj) for obj in objects]

    reference = NifFile(manifest["reference_nif"])
    reference_shape = reference.shapes[0]
    reference_minimum, reference_maximum = transformed_bounds(reference_shape)
    source_center, target_center, scale, output_minimum, output_maximum = (
        calculate_normalization(
            meshes,
            reference_minimum,
            reference_maximum,
            manifest["fit_mode"],
            manifest["scale"]))
    rotation_radians = tuple(
        value * 0.017453292519943295 for value in manifest["rotation_degrees"])
    post_rotation = Euler(rotation_radians, "XYZ").to_matrix()
    post_scale = manifest["post_scale"]
    transformed_points = [
        apply_post_transform(
            normalized_position(vertex.co, source_center, target_center, scale),
            post_rotation,
            post_scale)
        for mesh in meshes for vertex in mesh.vertices
    ]
    output_minimum = [
        min(point[axis] for point in transformed_points) for axis in range(3)]
    output_maximum = [
        max(point[axis] for point in transformed_points) for axis in range(3)]

    destination = NifFile()
    destination.initialize(
        reference.game,
        str(output_path),
        type(reference.rootNode).__name__,
        safe_shape_name(source_path.stem))
    destination.rootNode.flags = reference.rootNode.flags

    template_properties = reference_shape.properties.copy()
    for attribute in (
            "nameID", "controllerID", "collisionID", "skinInstanceID",
            "shaderPropertyID", "alphaPropertyID"):
        setattr(template_properties, attribute, NODEID_NONE)

    texture_map = manifest["texture_map"]
    shape_count = 0
    for obj, mesh in zip(objects, meshes):
        for material_index, chunk_index, vertices, triangles, uvs, normals in (
                visual_chunks(
                    mesh,
                    source_center,
                    target_center,
                    scale,
                    post_rotation,
                    post_scale)):
            material = (
                obj.material_slots[material_index].material
                if material_index < len(obj.material_slots) else None)
            diffuse = texture_for_material(
                material,
                texture_map,
                ("basecolor", "diffuse", "albedo"),
                "__default_diffuse__",
                allow_untyped_image=True)
            normal_texture = texture_for_material(
                material,
                texture_map,
                ("normal", "normalmap"),
                "__default_normal__")
            material_name = material.name if material else "Material"
            shape_name = safe_shape_name(f"{obj.name}_{material_name}_{chunk_index}")
            shape = destination.createShapeFromData(
                shape_name,
                vertices,
                triangles,
                uvs,
                normals,
                props=template_properties.copy(),
                use_type=reference_shape.properties.bufType,
                parent=destination.rootNode)
            shape.transform = TransformBuf()
            copy_shape_style(shape, reference_shape, diffuse, normal_texture)
            shape_count += 1

    if shape_count == 0:
        raise ValueError(f"No triangles were imported from {source_path}")

    reference_bsx = reference.rootNode.get_extra_data(blockname="BSXFlags")
    collision_template = None
    if manifest["collision_template"]:
        collision_template = NifFile(manifest["collision_template"])
        template_bsx = collision_template.rootNode.get_extra_data(blockname="BSXFlags")
        if template_bsx:
            reference_bsx = template_bsx
    if reference_bsx:
        BSXFlags.New(
            destination,
            name=reference_bsx.name,
            flags=int(reference_bsx.flags),
            parent=destination.rootNode)

    marker_source = collision_template.rootNode if collision_template else reference.rootNode
    marker = marker_source.get_extra_data(blockname="BSInvMarker")
    BSInvMarker.New(
        destination,
        name=marker.name if marker else "INV",
        rotation=manifest["inventory_rotation"],
        zoom=manifest["inventory_zoom"],
        parent=destination.rootNode)

    hulls = []
    if collision_template:
        for mesh in meshes:
            points = [
                apply_post_transform(
                    normalized_position(
                        vertex.co, source_center, target_center, scale),
                    post_rotation,
                    post_scale)
                for vertex in mesh.vertices
            ]
            if len(points) >= 3:
                hulls.append(convex_hull(
                    points,
                    manifest["minimum_thickness"],
                    manifest["max_collision_vertices"]))
        if not hulls:
            raise ValueError(f"Could not generate collision for {source_path}")
        add_collision(
            destination,
            collision_template,
            hulls,
            output_minimum,
            output_maximum,
            manifest["mass"],
            manifest["collision_margin"],
            classes)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    destination.save()
    for mesh in meshes:
        bpy.data.meshes.remove(mesh)
    print(
        f"Generated {output_path} | shapes={shape_count} | "
        f"collision_parts={len(hulls)} | "
        f"scale=({scale.x:.6f}, {scale.y:.6f}, {scale.z:.6f}) | "
        f"post_scale={post_scale:.6f} | rotation={manifest['rotation_degrees']}")


def main() -> None:
    args = parse_args()
    manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    sys.path.insert(0, manifest["pynifly_root"])
    from pyn.nifdefs import (
        NODEID_NONE,
        PynBufferTypes,
        bhkConvexVerticesShapeProps,
        bhkListShapeProps,
    )
    from pyn.pynifly import BSInvMarker, BSXFlags, NifFile
    from pyn.structs import TransformBuf

    classes = {
        "NODEID_NONE": NODEID_NONE,
        "PynBufferTypes": PynBufferTypes,
        "bhkConvexVerticesShapeProps": bhkConvexVerticesShapeProps,
        "bhkListShapeProps": bhkListShapeProps,
        "BSInvMarker": BSInvMarker,
        "BSXFlags": BSXFlags,
        "NifFile": NifFile,
        "TransformBuf": TransformBuf,
    }
    for job in manifest["jobs"]:
        convert_job(manifest, job, classes)


if __name__ == "__main__":
    main()
