"""Repeatable creative-world placement/mining/crafting acceptance. Modifies lab save."""
import argparse
import json
import math
import socket
import time

from .action_host import Actions


def verify(actions):
    before = actions.act("inventory")
    allowed = {36: ("minecraft:oak_planks", 8), 37: ("minecraft:oak_planks", 4)}
    if any(s["count"] and (s["slot"] not in allowed or (s["item"], s["count"]) != allowed[s["slot"]])
           for s in before["slots"] if s["slot"] in (0, 1, 2, 3, 4, 36, 37)) or before["cursorCount"]:
        raise RuntimeError("Clear crafting/cursor/first two hotbar slots, or retain only this verifier's previous planks")
    feet = before["feet"]
    x, y, z = map(math.floor, feet)
    # Choose a solid ground block next to the player, with air above it.
    target = None
    for dx, dy, dz in ((1, 0, 0), (-1, 0, 0), (0, 0, 1), (0, 0, -1),
                       (1, -1, 0), (-1, -1, 0), (0, -1, 1), (0, -1, -1), (0, -2, 0)):
        ground = [x+dx, y+dy, z+dz]
        block = [x+dx, y+dy+1, z+dz]
        if actions.act("inspect", block=ground)["block"] in ("minecraft:grass_block", "minecraft:dirt", "minecraft:stone") and actions.act("inspect", block=block)["air"]:
            target = (ground, block)
            break
    if target is None:
        raise RuntimeError("Stand on flat ground with an empty adjacent block for verification")
    ground, block = target
    # Aim the vanilla ray at the center of the selected block face before use.
    def aim(point):
        ex, ey, ez = feet[0], feet[1] + 1.62, feet[2]
        dx, dy, dz = point[0] + .5 - ex, point[1] + .5 - ey, point[2] + .5 - ez
        horizontal = math.hypot(dx, dz)
        return math.degrees(math.atan2(-dx, dz)), math.degrees(-math.atan2(dy, horizontal))
    yaw, pitch = aim(ground)
    actions.act("look", yaw=yaw, pitch=pitch)
    actions.act("creative", slot=0, item="minecraft:oak_planks", count=8)
    actions.act("select", slot=0)
    placed = actions.act("place", block=block, face="up")
    time.sleep(0.2)
    actual = actions.act("inspect", block=block)
    if actual["block"] != "minecraft:oak_planks":
        raise RuntimeError(f"Placement not confirmed: {placed}, {actual}")
    yaw, pitch = aim(block)
    actions.act("look", yaw=yaw, pitch=pitch)
    actions.act("break", block=block, face="up")
    time.sleep(0.2)
    if not actions.act("inspect", block=block)["air"]:
        raise RuntimeError("Mining not confirmed by world readback")
    # One log in 2x2 grid -> four planks, through normal handler clicks.
    actions.act("creative", slot=1, item="minecraft:oak_log", count=1)
    time.sleep(0.15)
    actions.act("click", slot=37, button=0)
    actions.act("click", slot=1, button=0)
    time.sleep(0.15)
    recipe = actions.act("inventory")
    output = next(s for s in recipe["slots"] if s["slot"] == 0)
    if output["item"] != "minecraft:oak_planks" or output["count"] != 4:
        raise RuntimeError(f"Recipe output incorrect: {output}")
    actions.act("click", slot=0, button=0)
    actions.act("click", slot=37, button=0)
    time.sleep(0.2)
    result = actions.act("inventory")
    crafted = next(s for s in result["slots"] if s["slot"] == 37)
    if crafted["item"] != "minecraft:oak_planks" or crafted["count"] != 4 or result["cursorCount"]:
        raise RuntimeError("Crafted item not confirmed in inventory")
    return {"passed": True, "epoch": actions.epoch, "placedAndMined": block,
            "crafted": crafted, "feet": result["feet"], "scope": "Minecraft guest only"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=37122)
    args = parser.parse_args()
    with socket.create_connection(("127.0.0.1", args.port), timeout=3) as client, client.makefile("rb") as reader:
        print(json.dumps(verify(Actions(client, reader))))


if __name__ == "__main__":
    main()
