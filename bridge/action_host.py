"""Execute a bounded Minecraft action in the isolated, unpaused offline lab."""
import argparse
import json
import socket
import time

from .minecraft_host import request


class Actions:
    def __init__(self, client, reader):
        self.client, self.reader = client, reader
        ready = request(client, reader, {"type": "hello", "role": "test"})
        if not ready.get("actions"):
            raise RuntimeError("Minecraft does not advertise actions; rebuild/restart the lab")
        status = request(client, reader, {"type": "status"})
        if not status.get("offline"):
            raise RuntimeError("Open and unpause the isolated singleplayer world")
        self.epoch = status["epoch"]
        self.sequence = 0
        self.last = 0.0

    def act(self, kind, **values):
        time.sleep(max(0, self.last + 0.025 - time.monotonic()))
        self.sequence += 1
        self.last = time.monotonic()
        result = request(self.client, self.reader, dict(values, type="action", action=kind,
                         id=self.sequence, epoch=self.epoch))
        if (result.get("type") != "action-ack" or result.get("id") != self.sequence
                or result.get("epoch") != self.epoch or result.get("action") != kind):
            raise RuntimeError("Mismatched action acknowledgement")
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=37122)
    sub = parser.add_subparsers(dest="action", required=True)
    look = sub.add_parser("look")
    look.add_argument("yaw", type=float); look.add_argument("pitch", type=float)
    move = sub.add_parser("move")
    move.add_argument("delta", type=float, nargs=3)
    for name in ("break", "place", "inspect"):
        block = sub.add_parser(name); block.add_argument("block", type=int, nargs=3)
        if name != "inspect":
            block.add_argument("face", choices=("up", "down", "north", "south", "east", "west"))
    select = sub.add_parser("select"); select.add_argument("slot", type=int, choices=range(9))
    click = sub.add_parser("click"); click.add_argument("slot", type=int, choices=range(46))
    click.add_argument("button", type=int, choices=(0, 1))
    sub.add_parser("inventory")
    args = vars(parser.parse_args()); port, kind = args.pop("port"), args.pop("action")
    with socket.create_connection(("127.0.0.1", port), timeout=3) as client, client.makefile("rb") as reader:
        print(json.dumps(Actions(client, reader).act(kind, **args)))


if __name__ == "__main__":
    main()
