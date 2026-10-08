import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("package_alpha", Path(__file__).with_name("package-alpha.py"))
package = importlib.util.module_from_spec(spec); spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def test_private_game_files_and_paths_are_not_allowlisted(self):
        for name in (".local/machine.json", "minecraft/run/saves/world.dat", "cs2/private.vpk",
                     "../README.md", "C:/Users/private.json", "cs2/native/build/bin.exe", "cs2\\private.cpp"):
            self.assertFalse(package.source_allowed(name), name)
        for name in ("LICENSE", "game/start.ps1", "cs2/native/src/addon.cpp", "minecraft/src/main/resources/fabric.mod.json"):
            self.assertTrue(package.source_allowed(name), name)

    def fixture(self, root):
        entries = []
        for name in sorted(package.ROOT_FILES | package.BINARY_FILES):
            file = root / name; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(b"fixture")
            entries.append({"path": name, "bytes": 7, "sha256": package.digest(file)})
        manifest = {"format": 1, "scope": "offline-full-client-passthrough", "version": "fixture", "files": entries}
        (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf8")
        return manifest

    def test_hashes_missing_files_and_unmanifested_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); self.fixture(root)
            self.assertTrue(package.verify(root)["verified"])
            (root / "LICENSE").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "changed"): package.verify(root)
            self.fixture(root); (root / "PRIVATE.txt").write_bytes(b"secret")
            with self.assertRaisesRegex(ValueError, "Unmanifested"): package.verify(root)
            (root / "PRIVATE.txt").unlink(); (root / "native/CounterCraftProbe.addon64").unlink()
            with self.assertRaisesRegex(ValueError, "Missing"): package.verify(root)

    def test_manifest_traversal_duplicates_and_missing_notices_are_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for mode in ("traversal", "duplicate", "missing"):
                manifest = self.fixture(root)
                if mode == "traversal": manifest["files"][0]["path"] = "../LICENSE"
                if mode == "duplicate": manifest["files"].append(manifest["files"][0])
                if mode == "missing": manifest["files"] = [e for e in manifest["files"] if e["path"] != "THIRD_PARTY_NOTICES.md"]
                (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf8")
                with self.assertRaises(ValueError): package.verify(root)


if __name__ == "__main__": unittest.main()
