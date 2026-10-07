import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from bridge.frame_capture import linear_depth, read_frame


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


class FrameTests(unittest.TestCase):
    def bundle(self, root):
        path = Path(root)
        png = (b"\x89PNG\r\n\x1a\n"
               + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 6, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(b"\x00\xff\x00\x00\xff\x00\xff\x00\xff"))
               + chunk(b"IEND", b""))
        (path / "color.png").write_bytes(png)
        (path / "depth.f32").write_bytes(struct.pack("<2f", 0.5, 1))
        metadata = {"v": 1, "type": "world-frame", "width": 2, "height": 1,
                    "rowOrder": "top-to-bottom", "depthEncoding": "float32-le",
                    "depthSpace": "opengl-window-z", "reversedZ": False,
                    "includesHandHud": False, "colorFile": "color.png", "depthFile": "depth.f32",
                    "requestedFrame": 5, "camera": {"position": [1, 65, 2], "rotation": [90, 15, 0], "fov": 70},
                    "projectionColumnMajor": [0] * 16, "viewRotationColumnMajor": [0] * 16,
                    "near": 0.05, "far": 128}
        manifest = path / "frame.json"
        manifest.write_text(json.dumps(metadata))
        return manifest, metadata

    def test_reads_float32_depth_and_matching_colour(self):
        with tempfile.TemporaryDirectory() as root:
            manifest, _ = self.bundle(root)
            frame = read_frame(manifest)
            self.assertEqual(list(frame.depth), [0.5, 1])
            self.assertEqual(frame.summary()["geometryPixels"], 1)

    def test_rejects_incomplete_or_invalid_depth(self):
        with tempfile.TemporaryDirectory() as root:
            manifest, _ = self.bundle(root)
            depth = Path(root) / "depth.f32"
            for data in (b"\0", struct.pack("<2f", float("nan"), 1), struct.pack("<2f", -1, 1)):
                depth.write_bytes(data)
                with self.assertRaises(ValueError):
                    read_frame(manifest)

    def test_rejects_mismatched_dimensions_and_formats(self):
        with tempfile.TemporaryDirectory() as root:
            manifest, metadata = self.bundle(root)
            for key, value in (("width", 1), ("colorFile", "../other.png"), ("reversedZ", True),
                               ("requestedFrame", -2), ("projectionColumnMajor", [0] * 15)):
                bad = copy.deepcopy(metadata)
                bad[key] = value
                manifest.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):
                    read_frame(manifest)

    def test_depth_endpoints_and_projection_roundtrip(self):
        near, far = 0.05, 768
        self.assertAlmostEqual(linear_depth(0, near, far), near)
        self.assertAlmostEqual(linear_depth(1, near, far), far)
        for distance in (0.05, 1, 20, 100, 767):
            depth = far / (far - near) * (1 - near / distance)
            self.assertAlmostEqual(linear_depth(depth, near, far), distance, places=8)
        for values in ((-1, near, far), (1, far, near), (0, float("nan"), far)):
            with self.assertRaises(ValueError):
                linear_depth(*values)


if __name__ == "__main__":
    unittest.main()
