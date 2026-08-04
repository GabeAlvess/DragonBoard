from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from PIL import Image


IMAGE_EXTENSIONS = {".bmp", ".dds", ".jpeg", ".jpg", ".png", ".tga"}
MESH_EXTENSIONS = {".fbx", ".obj"}
DEFAULT_BLENDER_PATHS = (
    Path(r"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"),
    Path(r"C:\Program Files\Blender Foundation\Blender 4.5\blender.exe"),
    Path(r"C:\Program Files\Blender Foundation\Blender 4.4\blender.exe"),
)


def safe_name(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._")
    return cleaned or "converted"

def inventory_rotation_milliradians(angles_degrees: tuple[float, float, float]) -> list[int]:
    full_turn = round(2.0 * math.pi * 1000.0)
    return [
        round(math.radians(angle) * 1000.0) % full_turn
        for angle in angles_degrees
    ]


def find_blender(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if configured := os.environ.get("BLENDER_EXE"):
        candidates.append(Path(configured))
    if discovered := shutil.which("blender"):
        candidates.append(Path(discovered))
    candidates.extend(DEFAULT_BLENDER_PATHS)
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError("Blender was not found. Pass --blender or set BLENDER_EXE.")


def find_pynifly_root(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if configured := os.environ.get("PYNIFLY_ROOT"):
        candidates.append(Path(configured))
    blender_root = Path(os.environ.get("APPDATA", "")) / "Blender Foundation" / "Blender"
    if blender_root.exists():
        candidates.extend(sorted(
            blender_root.glob("*/scripts/addons/io_scene_nifly"), reverse=True))
    for candidate in candidates:
        if (candidate / "pyn" / "pynifly.py").is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "PyNifly was not found. Pass --pynifly-root or set PYNIFLY_ROOT.")


def extract_zip_safely(archive: Path, destination: Path) -> None:
    destination_root = destination.resolve()
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            target = (destination / entry.filename).resolve()
            if target != destination_root and destination_root not in target.parents:
                raise ValueError(f"Unsafe ZIP entry: {entry.filename}")
        package.extractall(destination)


def collect_mesh_files(source_root: Path, single_file: Path | None) -> list[Path]:
    if single_file:
        return [single_file.resolve()]
    return sorted(
        path.resolve()
        for path in source_root.rglob("*")
        if path.is_file() and path.suffix.lower() in MESH_EXTENSIONS)


def nif_texture_path(job_name: str, relative_path: Path) -> str:
    relative_dds = relative_path.with_suffix(".dds")
    return "\\".join((
        "textures", "DragonBoardVR", "Converted", job_name, *relative_dds.parts))


def convert_texture(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.suffix.lower() == ".dds":
        shutil.copy2(source, destination)
        return
    with Image.open(source) as image:
        image.convert("RGBA").save(destination, format="DDS")


def prepare_textures(source_root: Path, package_root: Path, job_name: str) -> dict[str, str]:
    texture_map: dict[str, str] = {}
    package_root = package_root.resolve()
    texture_root = package_root / "textures" / "DragonBoardVR" / "Converted" / job_name
    for source in sorted(path for path in source_root.rglob("*") if path.is_file()):
        source = source.resolve()
        if source == package_root or package_root in source.parents:
            continue
        if source.suffix.lower() not in IMAGE_EXTENSIONS:
            continue
        relative = source.relative_to(source_root)
        destination = texture_root / relative.with_suffix(".dds")
        convert_texture(source, destination)
        texture_map[str(source.resolve()).casefold()] = nif_texture_path(job_name, relative)

    default_texture = texture_root / "_default_white.dds"
    flat_normal = texture_root / "_flat_normal.dds"
    default_texture.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", (4, 4), (255, 255, 255, 255)).save(default_texture, format="DDS")
    Image.new("RGBA", (4, 4), (128, 128, 255, 255)).save(flat_normal, format="DDS")
    texture_map["__default_diffuse__"] = nif_texture_path(
        job_name, Path("_default_white.dds"))
    texture_map["__default_normal__"] = nif_texture_path(
        job_name, Path("_flat_normal.dds"))
    return texture_map


def build_parser() -> argparse.ArgumentParser:
    project_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(
        description="Convert OBJ/FBX files, folders, or ZIP archives into DragonBoard-sized NIF files.")
    parser.add_argument(
        "input", help="OBJ/FBX file, folder containing meshes, or ZIP archive")
    parser.add_argument("--output-dir", help="Package output directory")
    parser.add_argument(
        "--reference-nif",
        default=str(project_root / "Assets/meshes/DragonBoardVR/dragonboard_physical.nif"))
    parser.add_argument(
        "--collision-template",
        default=str(project_root / "Tools/PhysicalBoardMeshGenerator/dragon_tablet_collision_template.nif"))
    parser.add_argument("--blender")
    parser.add_argument("--pynifly-root")
    parser.add_argument(
        "--fit-mode",
        choices=("face", "longest", "envelope", "none"),
        default="face",
        help="all modes preserve proportions; face fits the two largest DragonBoard axes")
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--post-scale", type=float, default=1.0)
    parser.add_argument("--rotate-x", type=float, default=0.0)
    parser.add_argument("--rotate-y", type=float, default=0.0)
    parser.add_argument("--rotate-z", type=float, default=0.0)
    parser.add_argument("--inventory-rotate-x", type=float, default=0.0)
    parser.add_argument("--inventory-rotate-y", type=float, default=0.0)
    parser.add_argument("--inventory-rotate-z", type=float, default=0.0)
    parser.add_argument(
        "--inventory-zoom",
        type=float,
        default=1.0,
        help="Vanilla inventory preview zoom")
    parser.add_argument("--mass", type=float, default=2.0)
    parser.add_argument("--collision-margin", type=float, default=0.05)
    parser.add_argument("--minimum-thickness", type=float, default=0.10)
    parser.add_argument("--max-collision-vertices", type=int, default=128)
    parser.add_argument(
        "--forward-axis",
        choices=("X", "Y", "Z", "NEGATIVE_X", "NEGATIVE_Y", "NEGATIVE_Z"),
        default="NEGATIVE_Z")
    parser.add_argument(
        "--up-axis",
        choices=("X", "Y", "Z", "NEGATIVE_X", "NEGATIVE_Y", "NEGATIVE_Z"),
        default="Y")
    parser.add_argument("--no-collision", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    input_path = Path(args.input).resolve()
    if not input_path.exists():
        raise FileNotFoundError(input_path)
    if (args.scale <= 0.0 or args.post_scale <= 0.0 or args.mass <= 0.0 or
            args.minimum_thickness <= 0.0 or args.inventory_zoom <= 0.0):
        raise ValueError(
            "Scale, post scale, mass, minimum thickness, and inventory zoom must be greater than zero")
    if not 16 <= args.max_collision_vertices <= 255:
        raise ValueError("--max-collision-vertices must be between 16 and 255")

    blender = find_blender(args.blender)
    pynifly_root = find_pynifly_root(args.pynifly_root)
    reference_nif = Path(args.reference_nif).resolve()
    collision_template = Path(args.collision_template).resolve()
    if not reference_nif.is_file():
        raise FileNotFoundError(reference_nif)
    if not args.no_collision and not collision_template.is_file():
        raise FileNotFoundError(collision_template)

    job_name = safe_name(input_path.stem)
    output_dir = Path(args.output_dir).resolve() if args.output_dir else (
        input_path.parent / f"{job_name}_dragonboard_nif")
    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="dragonboard_obj_") as temporary:
        temporary_root = Path(temporary)
        single_file: Path | None = None
        if input_path.is_file() and input_path.suffix.lower() == ".zip":
            source_root = temporary_root / "input"
            source_root.mkdir()
            extract_zip_safely(input_path, source_root)
        elif input_path.is_file() and input_path.suffix.lower() in MESH_EXTENSIONS:
            source_root = input_path.parent
            single_file = input_path
        elif input_path.is_dir():
            source_root = input_path
        else:
            raise ValueError("Input must be an OBJ/FBX file, folder, or ZIP archive")

        mesh_files = collect_mesh_files(source_root, single_file)
        if not mesh_files:
            raise FileNotFoundError(f"No OBJ or FBX files found in {input_path}")

        texture_map = prepare_textures(source_root, output_dir, job_name)
        mesh_root = output_dir / "meshes" / "DragonBoardVR" / "Converted"
        jobs = []
        for mesh_file in mesh_files:
            relative = mesh_file.relative_to(source_root)
            output_nif = mesh_root / relative.with_suffix(".nif")
            output_nif.parent.mkdir(parents=True, exist_ok=True)
            jobs.append({
                "source": str(mesh_file),
                "relative": str(relative),
                "output": str(output_nif),
            })

        manifest = {
            "reference_nif": str(reference_nif),
            "collision_template": None if args.no_collision else str(collision_template),
            "pynifly_root": str(pynifly_root),
            "fit_mode": args.fit_mode,
            "scale": args.scale,
            "post_scale": args.post_scale,
            "rotation_degrees": [args.rotate_x, args.rotate_y, args.rotate_z],
            "inventory_rotation": inventory_rotation_milliradians((
                args.inventory_rotate_x,
                args.inventory_rotate_y,
                args.inventory_rotate_z,
            )),
            "inventory_zoom": args.inventory_zoom,
            "mass": args.mass,
            "collision_margin": args.collision_margin,
            "minimum_thickness": args.minimum_thickness,
            "max_collision_vertices": args.max_collision_vertices,
            "forward_axis": args.forward_axis,
            "up_axis": args.up_axis,
            "texture_map": texture_map,
            "jobs": jobs,
        }
        manifest_path = temporary_root / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        command = [
            str(blender), "--background", "--factory-startup", "--python",
            str(Path(__file__).with_name("blender_convert.py")), "--",
            "--manifest", str(manifest_path),
        ]
        completed = subprocess.run(command, check=False)
        if completed.returncode != 0:
            return completed.returncode

    report = {
        "input": str(input_path),
        "output": str(output_dir),
        "reference_nif": str(reference_nif),
        "fit_mode": args.fit_mode,
        "scale": args.scale,
        "post_scale": args.post_scale,
        "rotation_degrees": [args.rotate_x, args.rotate_y, args.rotate_z],
        "inventory_rotation_degrees": [
            args.inventory_rotate_x,
            args.inventory_rotate_y,
            args.inventory_rotate_z,
        ],
        "inventory_zoom": args.inventory_zoom,
        "collision": not args.no_collision,
        "generated": [job["output"] for job in jobs],
    }
    report_path = output_dir / "conversion_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Converted {len(jobs)} mesh file(s)")
    print(f"Package: {output_dir}")
    print(f"Report: {report_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
