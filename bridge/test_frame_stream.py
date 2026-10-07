import copy
import json
import socket
import struct
import time
import unittest
import uuid
import zlib

from bridge.frame_stream import HEADER, FrameReader, LatestFrames

SESSION = uuid.UUID("01234567-89ab-cdef-0123-456789abcdef")


def packet(seq=1, metadata=None, session=SESSION):
    if metadata is None:
        metadata = {"v": 1, "type": "world-stream-frame", "epoch": 7, "width": 1, "height": 2,
                    "rowOrder": "bottom-to-top", "colorEncoding": "rgba8", "depthEncoding": "float32-le",
                    "depthSpace": "opengl-window-z", "reversedZ": False, "includesHandHud": False,
                    "includesSkyFog": True, "requestedFrame": -1, "near": 0.05, "far": 768,
                    "camera": {"position": [0,64,0], "rotation": [90,30,0], "fov": 55},
                    "projectionColumnMajor": [1.] * 16, "viewRotationColumnMajor": [1.] * 16,
                    "monotonicNanos": time.perf_counter_ns(), "readbackNanos": 1234}
    meta = json.dumps(metadata).encode()
    pixels = bytes([255,0,0,255, 0,255,0,255]) + struct.pack("<ff", 0.5, 1.)
    crc = zlib.crc32(pixels, zlib.crc32(meta))
    return HEADER.pack(b"CCFRM001", 1, 64, len(meta), 8, 8, 0, session.int >> 64,
                       session.int & (2**64 - 1), seq, crc, 0) + meta + pixels


class Fragmented:
    def __init__(self, data):
        self.data = bytearray(data)

    def recv_into(self, target):
        size = min(len(target), len(self.data), 7)
        target[:size] = self.data[:size]
        del self.data[:size]
        return size


class StreamTests(unittest.TestCase):
    def test_fragmentation_depth_and_rows(self):
        frame = FrameReader(Fragmented(packet()), SESSION, 7).read()
        self.assertEqual(frame.sequence, 1)
        self.assertEqual(list(frame.depth()), [0.5, 1.])
        self.assertEqual(frame.summary()["geometryPixels"], 1)
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "frame.png"
            frame.export_png(path)
            png = path.read_bytes()
            offset = png.index(b"IDAT")
            size = struct.unpack(">I", png[offset - 4:offset])[0]
            self.assertEqual(zlib.decompress(png[offset + 4:offset + 4 + size]),
                             b"\0\0\xff\0\xff\0\xff\0\0\xff")

    def test_partial_and_corrupt_packet(self):
        data = packet()
        with self.assertRaises(EOFError):
            FrameReader(Fragmented(data[:-1]), SESSION, 7).read()
        corrupted = bytearray(data); corrupted[-1] ^= 1
        with self.assertRaises(ValueError):
            FrameReader(Fragmented(corrupted), SESSION, 7).read()

    def test_header_bounds_before_payload_allocation(self):
        for index, value in ((0, b"BADMAGIC"), (1, 2), (2, 63), (3, 4097), (4, 2**31),
                             (5, 9), (6, 1), (9, 0), (11, 1)):
            header = list(HEADER.unpack(packet()[:64])); header[index] = value
            with self.subTest(index=index), self.assertRaises(ValueError):
                FrameReader(Fragmented(HEADER.pack(*header)), SESSION, 7).read()

    def test_epoch_restart_and_monotonic_sequence(self):
        for session, epoch in ((uuid.uuid4(), 7), (SESSION, 8)):
            with self.assertRaises(ValueError):
                FrameReader(Fragmented(packet()), session, epoch).read()
        reader = FrameReader(Fragmented(packet(4) + packet(4)), SESSION, 7)
        reader.read()
        with self.assertRaises(ValueError):
            reader.read()

    def test_metadata_dimensions_and_nonfinite_camera(self):
        base = FrameReader(Fragmented(packet()), SESSION, 7).read().metadata
        for field, value in (("width", 3), ("readbackNanos", -1), ("reversedZ", True),
                             ("projectionColumnMajor", [float("nan")] * 16), ("camera", {})):
            metadata = copy.deepcopy(base); metadata[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                FrameReader(Fragmented(packet(metadata=metadata)), SESSION, 7).read()

    def test_latest_mailbox_staleness_and_shutdown(self):
        sender, receiver = socket.socketpair()
        frames = LatestFrames(receiver, SESSION, 7, 0)
        try:
            sender.sendall(packet(1) + packet(2) + packet(3))
            deadline = time.monotonic() + 2
            while frames.received < 3 and time.monotonic() < deadline:
                time.sleep(0.005)
            latest = frames.take()
            self.assertIsNotNone(latest)
            self.assertEqual(latest.sequence, 3)
            self.assertEqual(frames.replaced, 2)
            old = copy.deepcopy(latest.metadata)
            old["monotonicNanos"] = time.perf_counter_ns() - 600_000_000
            sender.sendall(packet(4, old))
            while frames.received < 4 and time.monotonic() < deadline:
                time.sleep(0.005)
            self.assertIsNone(frames.take())
            self.assertEqual(frames.stale, 1)
        finally:
            frames.close(); sender.close()
        self.assertFalse(frames.worker.is_alive())


if __name__ == "__main__":
    unittest.main()
