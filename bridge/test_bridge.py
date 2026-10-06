import json
import io
import math
import socket
import threading
import unittest

from bridge.protocol import (Camera, camera_message, cs2_to_mc, decode, encode,
                             mc_to_cs2, source_rotation_to_mc)
from bridge.bridge_server import BridgeServer, Handler


class GeometryTests(unittest.TestCase):
    def test_axes_and_roundtrip_with_origin(self):
        self.assertEqual(mc_to_cs2((1, 2, 3)), (32, -96, 64))
        for point in ((-15.2, 0, 25), (0, 64, 0), (0.001, -64, -500)):
            restored = cs2_to_mc(mc_to_cs2(point, (21, -72, 11)), (21, -72, 11))
            for actual, expected in zip(restored, point):
                self.assertAlmostEqual(actual, expected)

    def test_cardinal_look_matches_coordinate_transform(self):
        for source_yaw in (0, 90, 180, 270, -45):
            yaw, pitch, _ = source_rotation_to_mc(0, source_yaw)
            direction = mc_to_cs2((-math.sin(math.radians(yaw)), 0, math.cos(math.radians(yaw))), scale=1)
            self.assertAlmostEqual(direction[0], math.cos(math.radians(source_yaw)))
            self.assertAlmostEqual(direction[1], math.sin(math.radians(source_yaw)))

    def test_reject_bad_frames(self):
        good = {"v": 1, **camera_message(Camera(1, (1, 2, 3), (0, 0, 0)))}
        for value in ([], {**good, "v": True}, {**good, "position": [float("nan"), 1, 2]},
                      {**good, "frame": -1}, {**good, "fov": 180}):
            with self.assertRaises(ValueError):
                decode(json.dumps(value))
        with self.assertRaises(ValueError):
            mc_to_cs2((0, 0, 0), scale=0)


class HandlerTests(unittest.TestCase):
    def exchange(self, data):
        # Exercise the real handler with an in-memory stream when sandbox rules
        # disallow TCP, without pretending this proves actual network access.
        output = io.BytesIO()

        class Connection:
            def settimeout(self, timeout):
                pass

        handler = object.__new__(Handler)
        handler.request = Connection()
        handler.rfile = io.BytesIO(data)
        handler.wfile = output
        handler.handle()
        return [decode(line) for line in output.getvalue().splitlines()]

    def test_handshake_camera_ack_and_ping(self):
        frames = [
            {"type": "hello", "role": "mc"},
            camera_message(Camera(9, (1, 64, 5), (0, 10, 0))),
            {"type": "ping"},
        ]
        result = self.exchange(b"".join(encode(frame) for frame in frames))
        self.assertEqual([m["type"] for m in result], ["ready", "ack", "pong"])
        self.assertFalse(result[0]["renderer"])
        self.assertEqual(result[1]["frame"], 9)

    def test_errors_close_session(self):
        good = encode({"type": "hello", "role": "test"})
        for invalid in (b'[]\n', b'{broken}\n', b'x' * 65537,
                        b'{"v":1,"type":"ping"}\n', good.rstrip(b'\n')):
            result = self.exchange(invalid + good)
            self.assertEqual(len(result), 1)
            self.assertEqual(result[0]["type"], "error")

    def test_duplicate_frame_is_rejected(self):
        frame = encode(camera_message(Camera(1, (0, 0, 0), (0, 0, 0))))
        result = self.exchange(encode({"type": "hello", "role": "cs2"}) + frame + frame)
        self.assertEqual([m["type"] for m in result], ["ready", "ack", "error"])


class ConnectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = BridgeServer(0)
        cls.worker = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.worker.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.worker.join()

    def test_fragmented_handshake_and_replayed_frame(self):
        with socket.create_connection(self.server.server_address, timeout=2) as client:
            reader = client.makefile("rb")
            with reader:
                hello = encode({"type": "hello", "role": "test"})
                client.sendall(hello[:7])
                client.sendall(hello[7:])
                self.assertFalse(decode(reader.readline())["renderer"])
                frame = encode(camera_message(Camera(20, (0, 64, 0), (0, 0, 0))))
                client.sendall(frame + frame)
                self.assertEqual(decode(reader.readline()), {"v": 1, "type": "ack", "frame": 20})
                self.assertEqual(decode(reader.readline())["type"], "error")
                self.assertEqual(reader.readline(), b"")

    def test_bad_client_does_not_stop_server(self):
        for payload, expected in ((b'[]\n', "error"), (encode({"type": "hello", "role": "mc"}), "ready")):
            with socket.create_connection(self.server.server_address, timeout=2) as client:
                with client.makefile("rb") as reader:
                    client.sendall(payload)
                    self.assertEqual(decode(reader.readline())["type"], expected)


if __name__ == "__main__":
    unittest.main()
