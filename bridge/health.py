"""Non-mutating readiness check for the isolated singleplayer guest."""
import argparse
import json
import socket
from .minecraft_host import request


def check(port=37122):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as client, client.makefile("rb") as reader:
        ready = request(client, reader, {"type": "hello", "role": "test"})
        status = request(client, reader, {"type": "status"})
        if not all(ready.get(key) is True for key in ("stream", "input", "actions")):
            raise RuntimeError("Guest lacks CounterCraft gameplay capabilities")
        if status.get("offline") is not True or status.get("player", {}).get("screen") != "world":
            raise RuntimeError("Enter an unpaused singleplayer world and close all screens")
        if status.get("stream", {}).get("running"):
            raise RuntimeError("Another stream is active")
        return {"ready": True, "epoch": status["epoch"], "player": status["player"], "port": port}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=37122)
    print(json.dumps(check(parser.parse_args().port)))


if __name__ == "__main__":
    main()
