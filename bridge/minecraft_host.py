"""Drive the Fabric camera lab over loopback; no CS2 adapter is implied."""
import argparse
import json
import math
import socket
import time

from .protocol import MAX_MESSAGE_BYTES, Camera, camera_message, decode, encode


def request(client, reader, message):
    client.sendall(encode(message))
    response = reader.readline(MAX_MESSAGE_BYTES + 1)
    if not response or not response.endswith(b"\n"):
        raise RuntimeError("Minecraft disconnected or sent an oversized reply")
    reply = decode(response)
    if reply["type"] == "error":
        raise RuntimeError(reply.get("message", "Minecraft rejected the request"))
    return reply


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=37122)
    parser.add_argument("--demo", action="store_true", help="Move the camera for five seconds; default only reads status")
    args = parser.parse_args()
    with socket.create_connection(("127.0.0.1", args.port), timeout=3) as client:
        with client.makefile("rb") as reader:
            print(json.dumps(request(client, reader, {"type": "hello", "role": "test"})))
            status = request(client, reader, {"type": "status"})
            print(json.dumps(status))
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
