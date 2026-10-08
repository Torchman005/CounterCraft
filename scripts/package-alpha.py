"""Build/verify an allowlisted source-assisted alpha; never copy game installs."""
import argparse
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.1.0-alpha.1"
ROOT_FILES = {"README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "MODLOG.md",
              "MODDING_PLAN.md", "CONTRIBUTING.md", ".gitignore"}
SOURCE_DIRS = {"bridge", "cs2", "minecraft", "game", "docs", "scripts"}
EXTENSIONS = {".md", ".py", ".ps1", ".cpp", ".hpp", ".h", ".java", ".fx", ".gradle", ".json", ".properties", ".txt"}
BINARY_FILES = {"native/CounterCraftProbe.addon64", "mods/countercraft-minecraft-0.1.0.jar"}


def source_allowed(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        return False
    if name in ROOT_FILES:
        return True
    return (len(path.parts) > 1 and path.parts[0] in SOURCE_DIRS
            and not any(part in {"run", "build", ".gradle", ".local", "__pycache__"} for part in path.parts)
            and path.suffix in EXTENSIONS)


def checked_path(root, name):
    path = root / name
    if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"Missing file, symlink or escaped path: {name}")
    return path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(directory):
    root = directory.resolve()
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf8"))
    if manifest.get("format") != 1 or manifest.get("scope") != "offline-full-client-passthrough":
        raise ValueError("Unknown package format/scope")
    seen = set()
    for entry in manifest["files"]:
        name = entry["path"]
        if name in seen or not (source_allowed(name) or name in BINARY_FILES):
            raise ValueError(f"Duplicate or unapproved package entry: {name}")
        seen.add(name)
        path = checked_path(root, name)
        if path.stat().st_size != entry["bytes"] or digest(path) != entry["sha256"]:
            raise ValueError(f"Package file changed: {name}")
    if not (ROOT_FILES | BINARY_FILES).issubset(seen):
        raise ValueError("Package is missing required source, notices or compiled artifacts")
    # Only runtime output may appear after a local launch/build.
    for path in root.rglob("*"):
        name = path.relative_to(root).as_posix()
        runtime = any(part in {".local", ".gradle", "run", "build", "__pycache__"} for part in path.relative_to(root).parts)
        if path.is_file() and name != "manifest.json" and name not in seen and not runtime:
            raise ValueError(f"Unmanifested package file: {name}")
    return {"verified": True, "version": manifest["version"], "files": len(seen), "scope": manifest["scope"]}


def build(destination, addon, jar):
    destination = destination.resolve()
    if destination.exists() or destination.with_suffix(".zip").exists():
        raise ValueError("Use a fresh package destination; existing artifacts are preserved")
    if destination == ROOT or ROOT.is_relative_to(destination):
        raise ValueError("Package destination may not contain the source checkout")
    raw = subprocess.check_output(["git", "ls-files", "-c", "-o", "--exclude-standard", "-z"], cwd=ROOT)
    names = sorted(set(n.decode("utf8") for n in raw.split(b"\0") if n))
    sources = {name: checked_path(ROOT, name) for name in names if source_allowed(name)}
    if not ROOT_FILES.issubset(sources):
        raise ValueError("Required documentation/notices missing")
    if not addon.is_file() or not jar.is_file():
        raise ValueError("Build both adapters before packaging")
    addon_data = addon.read_bytes()
    if addon_data[:2] != b"MZ":
        raise ValueError("Native adapter is not a PE binary")
    with zipfile.ZipFile(jar) as archive:
        metadata = json.loads(archive.read("fabric.mod.json"))
        if metadata.get("id") != "countercraft" or metadata.get("version") != "0.1.0":
            raise ValueError("Wrong Minecraft mod jar")
        if any(name.startswith("net/minecraft/") for name in archive.namelist()):
            raise ValueError("Jar unexpectedly includes Minecraft classes")
    binaries = {"native/CounterCraftProbe.addon64": addon,
                "mods/countercraft-minecraft-0.1.0.jar": jar}
    destination.mkdir(parents=True)
    entries = []
    for name, source in sorted((sources | binaries).items()):
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
        entries.append({"path": name, "bytes": target.stat().st_size, "sha256": digest(target)})
    manifest = {"format": 1, "version": VERSION, "scope": "offline-full-client-passthrough",
                "sourceCommit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "sourceDirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
                "requirements": {"minecraft": "1.20.1", "fabricLoader": "0.19.5", "gradle": "8.14",
                                 "java": ">=21 for source launcher", "powershell": ">=7.4",
                                 "reshade": "6.8.0 full addon support, separate pinned download",
                                 "cs2": "Steam Windows x64 D3D11, local -insecure only"},
                "excluded": ["game assets", "ReShade runtime", "saves", "private captures", "dependency caches"],
                "files": entries}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
    result = verify(destination)
    archive_path = destination.with_suffix(".zip")
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name in sorted([entry["path"] for entry in entries] + ["manifest.json"]):
            archive.write(destination / name, f"CounterCraft-{VERSION}/{name}")
    result.update(directory=str(destination), archive=str(archive_path), sha256=digest(archive_path))
    archive_path.with_suffix(".zip.sha256").write_text(result["sha256"] + "  " + archive_path.name + "\n", encoding="ascii")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("build")
    create.add_argument("--destination", type=Path, required=True)
    create.add_argument("--addon", type=Path, default=ROOT / ".local/native-build/CounterCraftProbe.addon64")
    create.add_argument("--jar", type=Path, default=ROOT / "minecraft/build/libs/countercraft-minecraft-0.1.0.jar")
    check = sub.add_parser("verify"); check.add_argument("directory", type=Path)
    args = parser.parse_args()
    if args.command == "build":
        result = build(args.destination, args.addon, args.jar)
    else:
        result = verify(args.directory)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
