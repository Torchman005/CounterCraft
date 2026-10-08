"""Opt-in disposable-world oracle: real vanilla redstone, entity combat and health."""
import argparse
import json
import math
import socket
import time
import uuid
from pathlib import Path

from .action_host import Actions
from .minecraft_host import request


def verify(actions):
    def status():
        return request(actions.client, actions.reader, {"type": "status"})

    def wait_for(read, predicate, seconds=2):
        deadline = time.monotonic() + seconds
        while True:
            value = read()
            if predicate(value): return value
            if time.monotonic() >= deadline: raise RuntimeError(f"Vanilla readback did not match: {value}")
            time.sleep(.05)

    def command(text):
        opened = actions.act("ui", key=84)
        if opened["screen"] != "ChatScreen": raise RuntimeError("Cannot open vanilla chat")
        for start in range(0, len(text)+1, 32):
            chunk = ("/" + text)[start:start+32]
            if chunk and not actions.act("ui", text=chunk)["applied"]: raise RuntimeError("Chat rejected text")
        actions.act("ui", key=257)
        time.sleep(.1)

    initial = status()["player"]
    if initial["screen"] != "world" or not initial["creative"] or initial["health"] < 20:
        raise RuntimeError("Requires an unpaused creative lab at full health, with cheats enabled")
    feet = actions.act("inventory")["feet"]
    x, y, z = map(math.floor, feet)
    pair = None
    for dx, dz in ((2, 0), (-3, 0), (1, 2), (1, -2)):
        lamp = [x+dx, y+1, z+dz]; power = [lamp[0]+1, lamp[1], lamp[2]]
        try:
            if actions.act("inspect", block=lamp)["air"] and actions.act("inspect", block=power)["air"]:
                pair = lamp, power; break
        except RuntimeError:
            continue
    if pair is None: raise RuntimeError("No adjacent reachable air cells; stand in an open area")
    lamp, power = pair; tag = "cc_" + uuid.uuid4().hex[:10]
    coordinates = lambda p: " ".join(map(str, p))
    report = {"scope": "Minecraft guest only; not host-world fusion", "lamp": lamp, "power": power}
    try:
        command("setblock " + coordinates(lamp) + " minecraft:redstone_lamp keep")
        command("setblock " + coordinates(power) + " minecraft:redstone_block keep")
        report["powered"] = wait_for(lambda: actions.act("inspect", block=lamp), lambda r: r.get("properties", {}).get("lit") == "true")
        command("setblock " + coordinates(power) + " minecraft:air")
        report["unpowered"] = wait_for(lambda: actions.act("inspect", block=lamp), lambda r: r.get("properties", {}).get("lit") == "false")
        command("setblock " + coordinates(lamp) + " minecraft:air")

        before = {e["id"] for e in actions.act("entities")["entities"]}
        at = [lamp[0]+.5, lamp[1], lamp[2]+.5]
        command("summon minecraft:pig " + coordinates(at) + ' {NoAI:1b,NoGravity:1b,Tags:["' + tag + '"]}')
        spawned = wait_for(lambda: actions.act("entities"), lambda r: any(e["type"] == "minecraft:pig" and e["id"] not in before for e in r["entities"]))
        pig = next(e for e in spawned["entities"] if e["type"] == "minecraft:pig" and e["id"] not in before)
        eye = status()["player"]["eye"]
        dx, dy, dz = at[0]-eye[0], at[1]+.5-eye[1], at[2]-eye[2]
        yaw, pitch = math.degrees(math.atan2(-dx, dz)), -math.degrees(math.atan2(dy, math.hypot(dx, dz)))
        for sequence in range(1, 9):
            request(actions.client, actions.reader, {"type": "input", "id": sequence, "epoch": actions.epoch,
                    "yaw": yaw, "pitch": pitch, "forward": 0, "sideways": 0, "slot": initial["slot"], "attack": sequence < 8})
            time.sleep(.04)
        request(actions.client, actions.reader, {"type": "release"})
        damaged = wait_for(lambda: actions.act("entities"), lambda r: any(e["id"] == pig["id"] and e.get("health", 10) < pig["health"] for e in r["entities"]))
        report["entityBefore"] = pig
        report["entityAfter"] = next(e for e in damaged["entities"] if e["id"] == pig["id"])

        command("gamemode survival")
        wait_for(status, lambda r: r["player"]["creative"] is False)
        command("damage @s 2")
        report["survival"] = wait_for(status, lambda r: r["player"]["health"] < 20)["player"]
        report["passed"] = True
        return report
    finally:
        request(actions.client, actions.reader, {"type": "release"})
        command("gamemode creative")
        command("kill @e[type=minecraft:pig,tag=" + tag + "]")
        for position, expected in ((lamp, "minecraft:redstone_lamp"), (power, "minecraft:redstone_block")):
            if actions.act("inspect", block=position)["block"] == expected:
                command("setblock " + coordinates(position) + " minecraft:air")
        actions.act("look", yaw=initial["yaw"], pitch=initial["pitch"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-lab-commands", action="store_true", help="Required: changes disposable lab blocks, mode, health and spawns one tagged pig")
    parser.add_argument("--port", type=int, default=37122)
    parser.add_argument("--output", type=Path, default=Path(".local/vanilla-acceptance.json"))
    args = parser.parse_args()
    if not args.allow_lab_commands: parser.error("Explicit --allow-lab-commands required; never run on important saves")
    with socket.create_connection(("127.0.0.1", args.port), timeout=3) as client, client.makefile("rb") as reader:
        result = verify(Actions(client, reader))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf8")
    print(json.dumps(result))


if __name__ == "__main__": main()
