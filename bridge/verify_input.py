"""Drive held input and full-client frames in a disposable Minecraft offline lab."""
import json
import socket
import time
from pathlib import Path

from .minecraft_host import request
from .frame_stream import FrameReader, LatestFrames


def main():
    with socket.create_connection(("127.0.0.1", 37122), timeout=3) as client, client.makefile("rb") as reader:
        ready = request(client, reader, {"type": "hello", "role": "test"})
        status = request(client, reader, {"type": "status"})
        if not ready.get("input") or not status["offline"] or status["player"]["screen"] != "world":
            raise RuntimeError("Unpause the isolated singleplayer world and close menus")
        epoch, initial = status["epoch"], status["position"]
        stream = request(client, reader, {"type": "stream-start", "fps": 20, "fullClient": True})
        with socket.create_connection(("127.0.0.1", stream["port"]), timeout=3) as binary:
            latest = LatestFrames(binary, stream["session"], epoch,
                                  time.perf_counter_ns()-status["serverMonotonicNanos"])
            began = time.monotonic(); sequence = 0; gui = world = None
            try:
                while time.monotonic() - began < 3:
                    elapsed = time.monotonic() - began
                    sequence += 1
                    ack = request(client, reader, {"type": "input", "id": sequence, "epoch": epoch,
                        "yaw": 0, "pitch": 30, "forward": 1 if elapsed < .7 else 0, "sideways": 0,
                        "slot": 0, "inventory": .9 < elapsed < 1.15 or 2 < elapsed < 2.25})
                    if ack.get("type") != "input-ack" or ack.get("id") != sequence:
                        raise RuntimeError("Input acknowledgement mismatch")
                    frame = latest.take()
                    if frame is not None:
                        if not frame.metadata["includesHandHud"]: raise RuntimeError("Missing full client color")
                        if frame.metadata.get("guiOpen"): gui = frame
                        else: world = frame
                    time.sleep(.05)
                request(client, reader, {"type": "release"})
                time.sleep(.4)
                final = request(client, reader, {"type": "status"})
                if gui is None or world is None or final["player"]["screen"] != "world" or final["player"]["inputId"] != -1:
                    raise RuntimeError(f"GUI/full client/watchdog check failed: gui={gui is not None}, world={world is not None}, {final}")
                distance = sum((a-b)**2 for a,b in zip(initial,final["position"]))**.5
                if distance < .05: raise RuntimeError("Held forward input did not move the actual player")
                Path(".local").mkdir(exist_ok=True)
                world.export_png(".local/input-world.png"); gui.export_png(".local/input-inventory.png")
                print(json.dumps({"passed": True, "inputs": sequence, "distance": distance,
                    "world": world.summary(), "inventory": gui.summary(), "released": final["player"]}))
            finally:
                latest.close()
                request(client, reader, {"type": "stream-stop"})


if __name__ == "__main__":
    main()
