from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


SCANNER_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCANNER_ROOT))

import dragonboard_ini_scanner as scanner


class IniScannerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        (self.root / "mods").mkdir()
        (self.root / "overwrite").mkdir()
        profile = self.root / "profiles" / "Current"
        profile.mkdir(parents=True)
        (self.root / "ModOrganizer.ini").write_text(
            "[General]\nselected_profile=@ByteArray(Current)\n",
            encoding="utf-8",
        )
        (profile / "modlist.txt").write_text(
            "+Config Override\n+Base Mod\n-Disabled Mod\n", encoding="utf-8"
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def _write_ini(self, mod: str, relative_path: str, contents: str) -> None:
        destination = self.root / "mods" / mod / Path(relative_path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(contents, encoding="utf-8", newline="")

    def test_catalog_only_contains_active_mods_and_marks_effective_provider(self) -> None:
        relative = "SKSE/Plugins/Example.ini"
        self._write_ini("Base Mod", relative, "[General]\nEnabled=false\n")
        self._write_ini("Config Override", relative, "[General]\nEnabled=true\n")
        self._write_ini("Disabled Mod", relative, "[General]\nEnabled=true\n")

        catalog = scanner.build_catalog(scanner.load_instance(self.root))

        self.assertEqual(catalog["summary"]["activeMods"], 2)
        self.assertEqual([mod["name"] for mod in catalog["mods"]], [
            "Config Override",
            "Base Mod",
        ])
        override_file = catalog["mods"][0]["files"][0]
        base_file = catalog["mods"][1]["files"][0]
        self.assertTrue(override_file["effectiveProvider"])
        self.assertFalse(base_file["effectiveProvider"])
        self.assertEqual(
            override_file["providers"], ["Config Override", "Base Mod"]
        )

    def test_only_literal_true_and_false_are_boolean_values(self) -> None:
        self._write_ini(
            "Base Mod",
            "Settings.ini",
            "[General]\nEnabled=true\nCount=1\nChoice=yes\nScale=1.5\n",
        )

        parsed = scanner.parse_ini(self.root / "mods" / "Base Mod" / "Settings.ini")
        settings = {
            item["key"]: item
            for section in parsed["sections"]
            for item in section["settings"]
        }

        self.assertEqual(settings["Enabled"]["valueType"], "boolean")
        self.assertEqual(settings["Count"]["valueType"], "integer")
        self.assertEqual(settings["Choice"]["valueType"], "string")
        self.assertEqual(settings["Scale"]["valueType"], "float")

    def test_sensitive_values_are_not_copied_to_catalog(self) -> None:
        self._write_ini("Base Mod", "Private.ini", "[Auth]\nApiToken=secret-value\n")

        parsed = scanner.parse_ini(self.root / "mods" / "Base Mod" / "Private.ini")
        setting = parsed["sections"][0]["settings"][0]

        self.assertTrue(setting["sensitive"])
        self.assertEqual(setting["value"], "")

    def test_atomic_writer_creates_valid_json(self) -> None:
        destination = self.root / "output" / "IniCatalog.json"
        scanner.write_catalog_atomic({"schemaVersion": 1}, destination, pretty=True)
        self.assertEqual(json.loads(destination.read_text(encoding="utf-8")), {
            "schemaVersion": 1
        })
        self.assertEqual(list(destination.parent.glob("*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
