from __future__ import annotations

import argparse
import configparser
import ctypes
import hashlib
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


SCHEMA_VERSION = 1
EXCLUDED_FILE_NAMES = {"meta.ini", "desktop.ini"}
SENSITIVE_KEY_PARTS = ("password", "passwd", "token", "secret", "apikey", "api_key")


@dataclass(frozen=True)
class Mo2Instance:
    root: Path
    mods_directory: Path
    profiles_directory: Path
    overwrite_directory: Path
    selected_profile: str


def _decode_qbytearray(value: str) -> str:
    value = value.strip()
    if value.startswith("@ByteArray(") and value.endswith(")"):
        value = value[len("@ByteArray(") : -1]
    return re.sub(
        r"\\x([0-9a-fA-F]{2})",
        lambda match: bytes.fromhex(match.group(1)).decode("latin-1"),
        value,
    ).replace(r"\(", "(").replace(r"\)", ")").replace(r"\\", "\\")


def _load_mo2_ini(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str
    parser.read(path, encoding="utf-8-sig")
    return parser


def _find_option(parser: configparser.ConfigParser, name: str) -> str | None:
    wanted = name.casefold()
    for key, value in parser.defaults().items():
        if key.casefold() == wanted:
            return value
    for section in parser.sections():
        for key, value in parser.items(section):
            if key.casefold() == wanted:
                return value
    return None


def _resolve_mo2_path(root: Path, value: str | None, fallback: str) -> Path:
    if not value:
        return root / fallback
    decoded = _decode_qbytearray(value)
    expanded = Path(os.path.expandvars(decoded))
    return expanded if expanded.is_absolute() else root / expanded


def load_instance(root: Path, requested_profile: str | None = None) -> Mo2Instance:
    root = root.expanduser().resolve()
    ini_path = root / "ModOrganizer.ini"
    if not ini_path.is_file():
        raise FileNotFoundError(f"ModOrganizer.ini was not found under '{root}'.")

    parser = _load_mo2_ini(ini_path)
    selected_profile = requested_profile or _decode_qbytearray(
        _find_option(parser, "selected_profile") or ""
    )
    if not selected_profile:
        raise RuntimeError("The active MO2 profile could not be determined.")

    instance = Mo2Instance(
        root=root,
        mods_directory=_resolve_mo2_path(
            root, _find_option(parser, "mods_directory"), "mods"
        ).resolve(),
        profiles_directory=_resolve_mo2_path(
            root, _find_option(parser, "profiles_directory"), "profiles"
        ).resolve(),
        overwrite_directory=_resolve_mo2_path(
            root, _find_option(parser, "overwrite_directory"), "overwrite"
        ).resolve(),
        selected_profile=selected_profile,
    )
    modlist = instance.profiles_directory / selected_profile / "modlist.txt"
    if not modlist.is_file():
        raise FileNotFoundError(f"MO2 profile modlist was not found: '{modlist}'.")
    return instance


def _running_mod_organizer_roots() -> Iterable[Path]:
    if os.name != "nt":
        return []

    TH32CS_SNAPPROCESS = 0x00000002
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [
            ("dwSize", ctypes.c_ulong),
            ("cntUsage", ctypes.c_ulong),
            ("th32ProcessID", ctypes.c_ulong),
            ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
            ("th32ModuleID", ctypes.c_ulong),
            ("cntThreads", ctypes.c_ulong),
            ("th32ParentProcessID", ctypes.c_ulong),
            ("pcPriClassBase", ctypes.c_long),
            ("dwFlags", ctypes.c_ulong),
            ("szExeFile", ctypes.c_wchar * 260),
        ]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snapshot == INVALID_HANDLE_VALUE:
        return []

    roots: list[Path] = []
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(entry)
        if not kernel32.Process32FirstW(snapshot, ctypes.byref(entry)):
            return []
        while True:
            if entry.szExeFile.casefold() == "modorganizer.exe":
                process = kernel32.OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION, False, entry.th32ProcessID
                )
                if process:
                    try:
                        capacity = ctypes.c_ulong(32768)
                        buffer = ctypes.create_unicode_buffer(capacity.value)
                        if kernel32.QueryFullProcessImageNameW(
                            process, 0, buffer, ctypes.byref(capacity)
                        ):
                            roots.append(Path(buffer.value).parent)
                    finally:
                        kernel32.CloseHandle(process)
            if not kernel32.Process32NextW(snapshot, ctypes.byref(entry)):
                break
    finally:
        kernel32.CloseHandle(snapshot)
    return roots


def locate_mo2_root(explicit_root: str | None) -> Path:
    candidates: list[Path] = []
    if explicit_root:
        candidates.append(Path(explicit_root))
    candidates.extend(_running_mod_organizer_roots())

    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        instances_root = Path(local_app_data) / "ModOrganizer"
        if instances_root.is_dir():
            candidates.extend(path.parent for path in instances_root.glob("*/ModOrganizer.ini"))

    seen: set[str] = set()
    for candidate in candidates:
        try:
            resolved = candidate.expanduser().resolve()
        except OSError:
            continue
        key = os.path.normcase(str(resolved))
        if key in seen:
            continue
        seen.add(key)
        if (resolved / "ModOrganizer.ini").is_file():
            return resolved
    raise FileNotFoundError(
        "No MO2 instance was found. Pass --mo2-root with the ModOrganizer directory."
    )


def _read_text(path: Path) -> tuple[str, str]:
    data = path.read_bytes()
    if data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        return data.decode("utf-16"), "utf-16"
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig"), "utf-8-sig"
    try:
        return data.decode("utf-8"), "utf-8"
    except UnicodeDecodeError:
        return data.decode("cp1252"), "cp1252"


def _infer_value_type(value: str) -> str:
    lowered = value.strip().casefold()
    if lowered in {"true", "false"}:
        return "boolean"
    if re.fullmatch(r"[+-]?\d+", value.strip()):
        return "integer"
    if re.fullmatch(
        r"[+-]?(?:\d+\.\d*|\d*\.\d+)(?:[eE][+-]?\d+)?", value.strip()
    ):
        return "float"
    return "string"


def _is_sensitive(key: str) -> bool:
    lowered = key.casefold()
    return any(part in lowered for part in SENSITIVE_KEY_PARTS)


def parse_ini(path: Path) -> dict:
    text, encoding = _read_text(path)
    sections: list[dict] = []
    section_by_name: dict[str, dict] = {}
    current_name = ""
    pending_comments: list[str] = []
    duplicate_counts: dict[tuple[str, str], int] = {}

    def get_section(name: str) -> dict:
        if name not in section_by_name:
            section = {"name": name, "settings": []}
            section_by_name[name] = section
            sections.append(section)
        return section_by_name[name]

    for line_number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if not stripped:
            pending_comments.clear()
            continue
        if stripped.startswith((";", "#")):
            pending_comments.append(stripped[1:].strip())
            continue
        section_match = re.fullmatch(r"\[(.*)]", stripped)
        if section_match:
            current_name = section_match.group(1).strip()
            pending_comments.clear()
            get_section(current_name)
            continue
        if "=" not in line:
            pending_comments.clear()
            continue

        key_part, value_part = line.split("=", 1)
        key = key_part.strip()
        if not key:
            pending_comments.clear()
            continue
        value = value_part.strip()
        identity = (current_name.casefold(), key.casefold())
        occurrence = duplicate_counts.get(identity, 0)
        duplicate_counts[identity] = occurrence + 1
        sensitive = _is_sensitive(key)
        setting = {
            "key": key,
            "value": "" if sensitive else value,
            "valueType": _infer_value_type(value),
            "sensitive": sensitive,
            "line": line_number,
            "occurrence": occurrence,
        }
        if pending_comments:
            setting["description"] = " ".join(pending_comments)
        get_section(current_name)["settings"].append(setting)
        pending_comments.clear()

    return {
        "encoding": encoding,
        "lineEnding": "crlf" if "\r\n" in text else "lf",
        "sections": sections,
        "editable": any(section["settings"] for section in sections),
    }


def _file_fingerprint(path: Path) -> dict:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    stat = path.stat()
    return {
        "size": stat.st_size,
        "modifiedNs": stat.st_mtime_ns,
        "sha256": digest.hexdigest(),
    }


def _active_mod_names(modlist_path: Path) -> list[str]:
    text, _ = _read_text(modlist_path)
    result: list[str] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line.startswith("+") and len(line) > 1:
            result.append(line[1:])
    return result


def _scan_ini_files(mod_directory: Path) -> list[Path]:
    if not mod_directory.is_dir():
        return []
    return sorted(
        (
            path
            for path in mod_directory.rglob("*.ini")
            if path.is_file() and path.name.casefold() not in EXCLUDED_FILE_NAMES
        ),
        key=lambda path: str(path.relative_to(mod_directory)).casefold(),
    )


def build_catalog(instance: Mo2Instance) -> dict:
    modlist_path = (
        instance.profiles_directory / instance.selected_profile / "modlist.txt"
    )
    active_names = _active_mod_names(modlist_path)
    mods: list[dict] = []
    providers: dict[str, list[tuple[int, str]]] = {}

    for priority, name in enumerate(active_names):
        mod_root = instance.mods_directory / name
        files: list[dict] = []
        if mod_root.is_dir():
            for ini_path in _scan_ini_files(mod_root):
                relative_path = ini_path.relative_to(mod_root).as_posix()
                parsed = parse_ini(ini_path)
                file_entry = {
                    "id": hashlib.sha1(
                        f"{name.casefold()}|{relative_path.casefold()}".encode("utf-8")
                    ).hexdigest()[:16],
                    "name": ini_path.name,
                    "relativePath": relative_path,
                    "fingerprint": _file_fingerprint(ini_path),
                    **parsed,
                }
                files.append(file_entry)
                providers.setdefault(relative_path.casefold(), []).append((priority, name))
        mods.append(
            {
                "id": hashlib.sha1(name.casefold().encode("utf-8")).hexdigest()[:16],
                "name": name,
                "folder": name,
                "priority": priority,
                "active": True,
                "files": files,
            }
        )

    overwrite_files: list[dict] = []
    if instance.overwrite_directory.is_dir():
        for ini_path in _scan_ini_files(instance.overwrite_directory):
            relative_path = ini_path.relative_to(instance.overwrite_directory).as_posix()
            overwrite_files.append(
                {
                    "id": hashlib.sha1(
                        f"overwrite|{relative_path.casefold()}".encode("utf-8")
                    ).hexdigest()[:16],
                    "name": ini_path.name,
                    "relativePath": relative_path,
                    "fingerprint": _file_fingerprint(ini_path),
                    **parse_ini(ini_path),
                }
            )
            providers.setdefault(relative_path.casefold(), []).insert(0, (-1, "Overwrite"))
    if overwrite_files:
        mods.insert(
            0,
            {
                "id": "overwrite",
                "name": "Overwrite",
                "folder": "",
                "priority": -1,
                "active": True,
                "overwrite": True,
                "files": overwrite_files,
            },
        )

    for mod in mods:
        for file_entry in mod["files"]:
            candidates = providers[file_entry["relativePath"].casefold()]
            candidates.sort(key=lambda item: item[0])
            provider_names = [candidate[1] for candidate in candidates]
            file_entry["providers"] = provider_names
            file_entry["effectiveProvider"] = provider_names[0] == mod["name"]
            file_entry["hasConflict"] = len(provider_names) > 1

    return {
        "schemaVersion": SCHEMA_VERSION,
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "mo2": {
            "root": str(instance.root),
            "profile": instance.selected_profile,
            "modsDirectory": str(instance.mods_directory),
            "profilesDirectory": str(instance.profiles_directory),
            "overwriteDirectory": str(instance.overwrite_directory),
        },
        "summary": {
            "activeMods": len(active_names),
            "modsWithIni": sum(1 for mod in mods if mod["files"]),
            "iniFiles": sum(len(mod["files"]) for mod in mods),
        },
        "mods": mods,
    }


def write_catalog_atomic(catalog: dict, destination: Path, pretty: bool) -> None:
    destination = destination.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary_name = tempfile.mkstemp(
        prefix=f"{destination.name}.", suffix=".tmp", dir=destination.parent
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(
                catalog,
                stream,
                ensure_ascii=False,
                indent=2 if pretty else None,
                separators=None if pretty else (",", ":"),
            )
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, destination)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build the DragonBoardVR catalog of active MO2 INI files."
    )
    parser.add_argument("command", choices=("scan",))
    parser.add_argument("--mo2-root")
    parser.add_argument("--profile")
    parser.add_argument("--output", required=True)
    parser.add_argument("--pretty", action="store_true")
    arguments = parser.parse_args(argv)

    try:
        root = locate_mo2_root(arguments.mo2_root)
        instance = load_instance(root, arguments.profile)
        catalog = build_catalog(instance)
        write_catalog_atomic(catalog, Path(arguments.output), arguments.pretty)
    except Exception as error:
        print(f"DragonBoardIniScanner: {error}", file=sys.stderr)
        return 1

    summary = catalog["summary"]
    print(
        "DragonBoardIniScanner: "
        f"{summary['activeMods']} active mods, "
        f"{summary['modsWithIni']} with INI files, "
        f"{summary['iniFiles']} INI files."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
