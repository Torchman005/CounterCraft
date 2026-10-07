"""Bounded binary world-frame reader; no game/renderer imports or third-party dependencies."""
from __future__ import annotations

from array import array
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import socket
import struct
import sys
import threading
import time
import uuid
import zlib

from .frame_capture import _finite_vector

HEADER = struct.Struct("<8sIIIIIIQQQII")
MAX_PIXELS = 4_194_304
MAX_METADATA = 4096


def _exact(stream, size):
    result = bytearray(size)
    view = memoryview(result)
    received = 0
    while received < size:
        count = stream.recv_into(view[received:])
        if not count:
            raise EOFError("Incomplete world-stream packet")
        received += count
    return result


@dataclass(frozen=True)
class StreamFrame:
    session: uuid.UUID
    sequence: int
    metadata: dict
    pixels: bytearray
    received_ns: int
    transfer_ms: float

    @property
    def rgba(self):
        return memoryview(self.pixels)[:len(self.pixels) // 2]

    def depth(self):
        depth = array("f")
        depth.frombytes(memoryview(self.pixels)[len(self.pixels) // 2:])
        if sys.byteorder != "little":
            depth.byteswap()
        return depth

    def summary(self):
        depth = self.depth()
        if any(not math.isfinite(n) or not 0 <= n <= 1 for n in depth):
            raise ValueError("Invalid streamed depth samples")
        return {"session": str(self.session), "sequence": self.sequence,
                "size": [self.metadata["width"], self.metadata["height"]],
                "requestedFrame": self.metadata["requestedFrame"], "camera": self.metadata["camera"],
                "geometryPixels": sum(n < 1 for n in depth), "depthMin": min(depth), "depthMax": max(depth),
                "colorSha256": hashlib.sha256(self.rgba).hexdigest(),
                "readbackMs": self.metadata["readbackNanos"] / 1e6, "transferMs": self.transfer_ms}

    def export_png(self, path):
        """Diagnostic snapshot, reversing GL rows. Not part of the realtime receive loop."""
        width, height = self.metadata["width"], self.metadata["height"]
        stride = width * 4
        raw = b"".join(b"\0" + self.rgba[row * stride:(row + 1) * stride]
                       for row in range(height - 1, -1, -1))

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        Path(path).write_bytes(png + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


class FrameReader:
    def __init__(self, stream, session, epoch):
        self.stream = stream
        self.session = uuid.UUID(str(session))
        self.epoch = epoch
        self.last_sequence = 0

    def read(self):
        header = HEADER.unpack(_exact(self.stream, HEADER.size))
        magic, version, size, meta_size, color_size, depth_size, flags, high, low, seq, crc, reserved = header
        if (magic != b"CCFRM001" or version != 1 or size != HEADER.size or flags or reserved
                or not 0 < meta_size <= MAX_METADATA or not 0 < color_size <= MAX_PIXELS * 4
                or color_size != depth_size or color_size % 4):
            raise ValueError("Invalid or oversized stream header")
        if uuid.UUID(int=(high << 64) | low) != self.session or seq <= self.last_sequence:
            raise ValueError("Wrong stream session or out-of-order sequence")
        started = time.perf_counter_ns()
        meta_bytes = _exact(self.stream, meta_size)
        pixels = _exact(self.stream, color_size + depth_size)
        if zlib.crc32(pixels, zlib.crc32(meta_bytes)) != crc:
            raise ValueError("Corrupted world-stream packet")
        m = json.loads(meta_bytes)
        self._validate(m, color_size)
        self.last_sequence = seq
        received = time.perf_counter_ns()
        return StreamFrame(self.session, seq, m, pixels, received, (received - started) / 1e6)

    def _validate(self, m, color_size):
        expected = {"v": 1, "type": "world-stream-frame", "epoch": self.epoch,
                    "rowOrder": "bottom-to-top", "colorEncoding": "rgba8", "depthEncoding": "float32-le",
                    "depthSpace": "opengl-window-z", "reversedZ": False, "includesHandHud": False,
                    "includesSkyFog": True}
        if not isinstance(m, dict) or any(m.get(k) != v for k, v in expected.items()):
            raise ValueError("Wrong stream metadata or world epoch")
        if any(type(m.get(k)) is not bool for k in ("reversedZ", "includesHandHud", "includesSkyFog")):
            raise ValueError("Invalid stream flags")
        if any(type(m.get(k)) is not int for k in ("v", "epoch", "width", "height", "requestedFrame",
                                                  "monotonicNanos", "readbackNanos")):
            raise ValueError("Invalid stream integer field")
        if (m["width"] < 1 or m["height"] < 1 or m["width"] * m["height"] * 4 != color_size
                or m["requestedFrame"] < -1 or m["readbackNanos"] < 0):
            raise ValueError("Invalid stream dimensions or timing")
        camera = m.get("camera")
        if (not isinstance(camera, dict) or not _finite_vector(camera.get("position"), 3)
                or not _finite_vector(camera.get("rotation"), 3)
                or type(camera.get("fov")) not in (int, float) or not 0 < camera["fov"] < 180):
            raise ValueError("Invalid stream camera")
        if any(not _finite_vector(m.get(k), 16) for k in ("projectionColumnMajor", "viewRotationColumnMajor")):
            raise ValueError("Invalid stream matrix")
        near, far = m.get("near"), m.get("far")
        if type(near) not in (int, float) or type(far) not in (int, float) or not 0 < near < far < math.inf:
            raise ValueError("Invalid stream clipping planes")


class LatestFrames:
    """Drain TCP off the consumer thread; hold only one completed latest frame."""
    def __init__(self, stream, session, epoch, clock_offset_ns, max_age_ms=500):
        self.stream = stream
        self.reader = FrameReader(stream, session, epoch)
        self.offset = clock_offset_ns
        self.max_age_ns = max_age_ms * 1_000_000
        self.lock = threading.Lock()
        self.latest = None
        self.received = self.replaced = self.stale = 0
        self.failure = None
        self.closed = threading.Event()
        self.worker = threading.Thread(target=self._run, name="CounterCraft-FrameReceiver", daemon=True)
        self.worker.start()

    def age_ms(self, frame, now=None):
        if now is None:
            now = time.perf_counter_ns()
        return (now - self.offset - frame.metadata["monotonicNanos"]) / 1e6

    def _run(self):
        try:
            while not self.closed.is_set():
                frame = self.reader.read()
                with self.lock:
                    self.received += 1
                    if abs(self.age_ms(frame, frame.received_ns)) * 1e6 > self.max_age_ns:
                        self.stale += 1
                        continue
                    if self.latest is not None:
                        self.replaced += 1
                    self.latest = frame
        except (OSError, EOFError, ValueError, KeyError, TypeError) as failed:
            if not self.closed.is_set():
                self.failure = failed
        finally:
            self.closed.set()

    def take(self):
        with self.lock:
            result, self.latest = self.latest, None
            if result is not None and abs(self.age_ms(result)) * 1e6 > self.max_age_ns:
                self.stale += 1
                return None
            return result

    def close(self):
        self.closed.set()
        try:
            self.stream.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.stream.close()
        self.worker.join(timeout=2)
