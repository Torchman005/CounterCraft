"""Inspect on-demand Minecraft world colour/depth bundles; no CS2 rendering."""
from __future__ import annotations

from array import array
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct
import sys


@dataclass(frozen=True)
class WorldFrame:
    manifest: Path
    metadata: dict
    color: Path
    depth: array

    def summary(self):
        return {
            "manifest": str(self.manifest),
            "color": str(self.color),
            "size": [self.metadata["width"], self.metadata["height"]],
            "requestedFrame": self.metadata["requestedFrame"],
            "camera": self.metadata["camera"],
            "depthMin": min(self.depth),
            "depthMax": max(self.depth),
            "geometryPixels": sum(value < 1.0 for value in self.depth),
            "colorSha256": hashlib.sha256(self.color.read_bytes()).hexdigest(),
        }


def _finite_vector(value, size):
    return (isinstance(value, list) and len(value) == size
            and all(type(n) in (int, float) and math.isfinite(n) for n in value))


def read_frame(manifest: str | Path) -> WorldFrame:
    """Reject partial/invalid bundles before using them for renderer validation."""
    path = Path(manifest).resolve()
    metadata = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(metadata, dict) or type(metadata.get("v")) is not int or metadata.get("v") != 1:
        raise ValueError("Unsupported world-frame manifest")
    expected = {"type": "world-frame", "rowOrder": "top-to-bottom", "depthEncoding": "float32-le",
                "depthSpace": "opengl-window-z", "reversedZ": False, "includesHandHud": False,
                "colorFile": "color.png", "depthFile": "depth.f32"}
    if any(metadata.get(key) != value for key, value in expected.items()):
        raise ValueError("Unexpected world-frame format")
    width, height = metadata.get("width"), metadata.get("height")
    if (type(width) is not int or type(height) is not int or width < 1 or height < 1
            or width * height > 4_194_304):
        raise ValueError("Invalid world-frame dimensions")
    if type(metadata.get("requestedFrame")) is not int or metadata["requestedFrame"] < -1:
        raise ValueError("Invalid world-frame sequence")
    camera = metadata.get("camera")
    if (not isinstance(camera, dict) or not _finite_vector(camera.get("position"), 3)
            or not _finite_vector(camera.get("rotation"), 3)
            or type(camera.get("fov")) not in (int, float) or not 0 < camera["fov"] < 180):
        raise ValueError("Invalid rendered camera")
    for name in ("projectionColumnMajor", "viewRotationColumnMajor"):
        if not _finite_vector(metadata.get(name), 16):
            raise ValueError("Invalid frame matrix")
    near, far = metadata.get("near"), metadata.get("far")
    if type(near) not in (int, float) or type(far) not in (int, float) or not 0 < near < far < math.inf:
        raise ValueError("Invalid depth clipping planes")
    color = path.parent / "color.png"
    with color.open("rb") as stream:
        header = stream.read(33)
    if (len(header) != 33 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR"
            or struct.unpack(">II", header[16:24]) != (width, height)):
        raise ValueError("Colour image dimensions do not match the depth buffer")
    depth_path = path.parent / "depth.f32"
    if depth_path.stat().st_size != width * height * 4:
        raise ValueError("Incomplete depth buffer")
    depth = array("f")
    depth.frombytes(depth_path.read_bytes())
    if sys.byteorder != "little":
        depth.byteswap()
    if any(not math.isfinite(value) or not 0 <= value <= 1 for value in depth):
        raise ValueError("Depth buffer has invalid samples")
    return WorldFrame(path, metadata, color, depth)


def linear_depth(depth: float, near: float, far: float) -> float:
    """Positive eye-space distance for the captured OpenGL non-reversed depth."""
    if (not all(type(n) in (int, float) and math.isfinite(n) for n in (depth, near, far))
            or not 0 <= depth <= 1 or not 0 < near < far):
        raise ValueError("Invalid OpenGL depth parameters")
    return near * far / (far - depth * (far - near))
