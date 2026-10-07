"""Drive the Fabric camera lab over loopback; no CS2 adapter is implied."""
import argparse
import json
import math
import socket
import time

from .protocol import MAX_MESSAGE_BYTES, Camera, camera_message, decode, encode
from .frame_capture import read_frame


def request(client, reader, message):
    client.sendall(encode(message))
    response = reader.readline(MAX_MESSAGE_BYTES + 1)
    if not response or not response.endswith(b"\n"):
        raise RuntimeError("Minecraft disconnected or sent an oversized reply")
    reply = decode(response)
    if reply["type"] == "error":
        raise RuntimeError(reply.get("message", "Minecraft rejected the request"))
    return reply


def capture(client, reader):
    reply = request(client, reader, {"type": "capture"})
    if reply.get("type") != "captured" or not isinstance(reply.get("manifest"), str):
        raise RuntimeError("Unexpected capture reply")
    return read_frame(reply["manifest"])


def verify_camera(client, reader, status):
    """Measure actual render-side poses and depth, not just TCP acknowledgements."""
    if not status.get("offline"):
        raise RuntimeError("Open and unpause a single-player test world first")
    baseline = capture(client, reader)
    x, y, z = status["position"]
    sequence = 0
    observed = []
    try:
        for rotation, fov in (((0, 20, 0), 70), ((90, 30, 0), 55)):
            # Give the render thread time to latch the camera without polling game objects.
            for _ in range(4):
                pose = Camera(sequence, (x, y + 1, z), rotation, fov)
                reply = request(client, reader, camera_message(pose))
                if reply.get("type") != "ack" or reply.get("frame") != sequence:
                    raise RuntimeError("Unexpected camera acknowledgement")
                sequence += 1
                time.sleep(0.05)
            frame = capture(client, reader)
            metadata = frame.metadata
            actual = metadata["camera"]
            if (metadata["requestedFrame"] != pose.frame
                    or any(abs(a - b) > 1e-5 for a, b in zip(actual["position"], pose.position))
                    or any(abs(a - b) > 1e-5 for a, b in zip(actual["rotation"], pose.rotation))
                    or abs(actual["fov"] - fov) > 1e-5):
                raise RuntimeError("Rendered camera did not match the requested pose")
            if abs(metadata["projectionColumnMajor"][5] - 1 / math.tan(math.radians(fov) / 2)) > 1e-4:
                raise RuntimeError("Rendered projection did not match vertical FOV")
            if not any(value < 1 for value in frame.depth):
                raise RuntimeError("Captured depth contains no world geometry")
            observed.append(frame.summary())
    finally:
        released = request(client, reader, {"type": "release"})
        if released.get("type") != "released":
            raise RuntimeError("Camera release failed")
    time.sleep(0.15)
    restored = capture(client, reader)
    if restored.metadata["requestedFrame"] != -1:
        raise RuntimeError("Camera override remained after release")
    if observed[0]["colorSha256"] == observed[1]["colorSha256"]:
        raise RuntimeError("Distinct camera poses produced identical colour frames")
    return {"type": "camera-verification", "passed": True, "baseline": baseline.summary(),
            "controlled": observed, "restored": restored.summary(), "renderer": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=37122)
    parser.add_argument("--demo", action="store_true", help="Move the camera for five seconds; default only reads status")
    parser.add_argument("--capture", action="store_true", help="Export and inspect one world colour/depth frame")
    parser.add_argument("--verify", action="store_true", help="Verify two render-side camera/FOV poses and release using exported frames")
    args = parser.parse_args()
    if sum((args.demo, args.capture, args.verify)) > 1:
        parser.error("Choose only one of --demo, --capture and --verify")
    with socket.create_connection(("127.0.0.1", args.port), timeout=5) as client:
        with client.makefile("rb") as reader:
            print(json.dumps(request(client, reader, {"type": "hello", "role": "test"})))
            status = request(client, reader, {"type": "status"})
            print(json.dumps(status))
            if args.verify:
                print(json.dumps(verify_camera(client, reader, status)))
                return
            if args.capture:
                print(json.dumps(capture(client, reader).summary()))
                return
            if not args.demo:
                return
            if not status.get("offline"):
                raise RuntimeError("Open and unpause a single-player test world first")
            x, y, z = status["position"]
            try:
                started = time.monotonic()
                for frame in range(100):
                    angle = frame / 100 * math.tau
                    message = camera_message(Camera(frame, (x + math.sin(angle) * 2, y + 1, z),
                                                    (math.sin(angle) * 45, 15, 0)))
                    reply = request(client, reader, message)
                    if reply.get("type") != "ack" or reply.get("frame") != frame:
                        raise RuntimeError("Unexpected acknowledgement")
                    time.sleep(max(0, started + (frame + 1) / 20 - time.monotonic()))
                print("100 camera requests acknowledged; visually verify the actual camera in Minecraft.")
            finally:
                try:
                    print(json.dumps(request(client, reader, {"type": "release"})))
                except (OSError, ValueError, RuntimeError):
                    pass  # Disconnect and watchdog also release camera control.


if __name__ == "__main__":
    main()
