"""Small, dependency-free protocol shared by the MC and CS2 adapters.

Messages are JSON objects delimited by newlines. The protocol deliberately has no
game-specific binary data so it can be implemented by a Fabric mod, a Source 2
addon, or a test harness without shipping either game's files.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
import math
from typing import Any, Iterable

PROTOCOL_VERSION = 1
MAX_MESSAGE_BYTES = 65536


@dataclass(frozen=True)
class Camera:
    frame: int
    position: tuple[float, float, float]
    rotation: tuple[float, float, float]
    fov: float = 70.0


@dataclass(frozen=True)
class Block:
    block_id: str
    position: tuple[int, int, int]
    state: str = "default"


def encode(message: dict[str, Any]) -> bytes:
    """Encode one protocol message, validating its envelope."""
    envelope = {"v": PROTOCOL_VERSION, **message}
    payload = (json.dumps(envelope, separators=(",", ":"), ensure_ascii=True, allow_nan=False) + "\n").encode()
    decode(payload)
    return payload


def decode(line: bytes | str) -> dict[str, Any]:
    """Decode and validate one newline-delimited message."""
    if len(line if isinstance(line, bytes) else line.encode("utf-8")) > MAX_MESSAGE_BYTES:
        raise ValueError("bridge message exceeds 64 KiB")
    if isinstance(line, bytes):
        line = line.decode("utf-8")
    message = json.loads(line)
    if not isinstance(message, dict):
        raise ValueError("bridge message must be an object")
    if type(message.get("v")) is not int or message.get("v") != PROTOCOL_VERSION:
        raise ValueError(f"unsupported bridge protocol: {message.get('v')!r}")
    if not isinstance(message.get("type"), str):
        raise ValueError("bridge message type must be a string")
    if message["type"] == "camera":
        if type(message.get("frame")) is not int or message["frame"] < 0:
            raise ValueError("frame must be a nonnegative integer")
        for field in ("position", "rotation"):
            _vector(message.get(field), field)
        fov = message.get("fov")
        if not _finite(fov) or not 0 < fov < 180:
            raise ValueError("fov must be a vertical angle between 0 and 180 degrees")
    return message


def _finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def _vector(value, name):
    if not isinstance(value, (tuple, list)) or len(value) != 3 or not all(_finite(v) for v in value):
        raise ValueError(f"{name} must contain three finite numbers")


def _transform_args(position, origin, scale):
    _vector(position, "position")
    _vector(origin, "origin")
    if not _finite(scale) or scale <= 0:
        raise ValueError("scale must be positive and finite")


def camera_message(camera: Camera) -> dict[str, Any]:
    return {
        "type": "camera",
        "frame": camera.frame,
        "position": list(camera.position),
        "rotation": list(camera.rotation),
        "fov": camera.fov,
    }


def blocks_message(blocks: Iterable[Block]) -> dict[str, Any]:
    return {
        "type": "blocks",
        "blocks": [
            {"id": b.block_id, "position": list(b.position), "state": b.state}
            for b in blocks
        ],
    }


def mc_to_cs2(position: tuple[float, float, float], origin=(0.0, 0.0, 0.0), scale=32.0) -> tuple[float, float, float]:
    """Map MC x/y/z (positive z south, y up) to Source x/y/z (z up).

    One Minecraft block is 32 Source units, matching the upstream Source bridge
    convention. Keep this function shared by both adapters to avoid drift.
    """
    _transform_args(position, origin, scale)
    x, y, z = position
    ox, oy, oz = origin
    return (ox + x * scale, oy - z * scale, oz + y * scale)


def cs2_to_mc(position, origin=(0.0, 0.0, 0.0), scale=32.0):
    _transform_args(position, origin, scale)
    x, y, z = position
    ox, oy, oz = origin
    return ((x - ox) / scale, (z - oz) / scale, -(y - oy) / scale)


def source_rotation_to_mc(pitch, yaw):
    """Source forward (cos yaw, sin yaw) becomes MC (-sin yaw, cos yaw)."""
    if not _finite(pitch) or not _finite(yaw):
        raise ValueError("angles must be finite")
    return ((-90.0 - yaw + 180.0) % 360.0 - 180.0, pitch, 0.0)
