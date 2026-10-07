"""Measure real Minecraft frame transport and optionally verify two rendered camera poses."""
import argparse
import json
import math
from pathlib import Path
import socket
import time

from .frame_stream import LatestFrames
from .minecraft_host import request
from .protocol import Camera, camera_message


def percentile(values, fraction):
    if not values:
        return None
    return sorted(values)[min(len(values) - 1, int((len(values) - 1) * fraction))]


def calibrated_clock(client, reader):
    samples = []
    for _ in range(5):
        before = time.perf_counter_ns()
        status = request(client, reader, {"type": "ping"})
        after = time.perf_counter_ns()
        samples.append((after - before, (after + before) // 2 - status["serverMonotonicNanos"], status))
    rtt, offset, status = min(samples, key=lambda sample: sample[0])
    return offset, rtt / 2e6, status


def run(args):
    if args.verify and args.seconds < 7:
        raise ValueError("Camera verification needs at least seven seconds")
    with socket.create_connection(("127.0.0.1", args.port), timeout=4) as client, client.makefile("rb") as reader:
        ready = request(client, reader, {"type": "hello", "role": "test"})
        if not ready.get("stream"):
            raise RuntimeError("This Minecraft build has no frame stream")
        offset, uncertainty, status = calibrated_clock(client, reader)
        if not status.get("offline"):
            raise RuntimeError("Open an unpaused single-player lab world")
        start = request(client, reader, {"type": "stream-start", "fps": args.fps})
        if start.get("host") != "127.0.0.1" or start.get("type") != "stream-started":
            raise RuntimeError("Unexpected stream endpoint")
        frames = None
        ages, readbacks, transfers = [], [], []
        seen, snapshots, pending, saved = set(), {}, {}, {}
        pose_sequence = consumed = 0
        began = time.perf_counter()
        next_control = began
        last_frame = None
        stopped = False
        try:
            stream = socket.create_connection(("127.0.0.1", start["port"]), timeout=2)
            frames = LatestFrames(stream, start["session"], start["epoch"], offset)
            while time.perf_counter() - began < args.seconds:
                now = time.perf_counter()
                elapsed = now - began
                phase = "baseline" if elapsed < 1 else "a" if elapsed < 3 else "b" if elapsed < 5 else "released"
                if now >= next_control:
                    next_control = now + (0.05 if args.verify else 0.25)
                    if args.verify and phase in ("a", "b"):
                        rotation, fov = ((0,20,0), 70) if phase == "a" else ((90,30,0), 55)
                        pose_sequence += 1
                        camera = Camera(pose_sequence, tuple(status["position"]), rotation, fov)
                        request(client, reader, camera_message(camera))
                        pending[pose_sequence] = (phase, camera)
                        pending.pop(pose_sequence - 100, None)
                    elif args.verify:
                        request(client, reader, {"type": "release"})
                    else:
                        status = request(client, reader, {"type": "ping"})
                frame = frames.take()
                if frame is not None:
                    consumed += 1
                    last_frame = frame
                    ages.append(frames.age_ms(frame))
                    readbacks.append(frame.metadata["readbackNanos"] / 1e6)
                    transfers.append(frame.transfer_ms)
                    if args.verify:
                        sequence = frame.metadata["requestedFrame"]
                        target = pending.get(sequence)
                        label = target[0] if target else phase if sequence == -1 and phase in ("baseline", "released") else None
                        if target:
                            camera = target[1]
                            actual = frame.metadata["camera"]
                            if (any(abs(a - b) > 1e-4 for a, b in zip(actual["position"], camera.position))
                                    or any(abs(a - b) > 1e-4 for a, b in zip(actual["rotation"], camera.rotation))
                                    or abs(actual["fov"] - camera.fov) > 1e-4):
                                raise RuntimeError("Stream pixels were paired with the wrong camera pose")
                            m5 = frame.metadata["projectionColumnMajor"][5]
                            projection_fov = math.degrees(2 * math.atan(1 / m5))
                            if abs(projection_fov - camera.fov) > 0.01:
                                raise RuntimeError("Stream projection FOV mismatch")
                        if label and label not in seen:
                            # Four bounded diagnostic frames. Full depth scans/PNG encoding must not starve TCP drain.
                            seen.add(label); saved[label] = frame
                if frames.failure:
                    raise RuntimeError("Frame receiver failed") from frames.failure
                time.sleep(args.consumer_ms / 1000)
            elapsed_seconds = time.perf_counter() - began
            status = request(client, reader, {"type": "status"})
            request(client, reader, {"type": "release"})
            request(client, reader, {"type": "stream-stop"})
            frames.close()
            stopped = True
            if not consumed:
                raise RuntimeError("No fresh streamed frames received")
            if args.verify:
                if not {"baseline", "a", "b", "released"}.issubset(seen):
                    raise RuntimeError("Stream did not verify both cameras and release")
                for label, frame in saved.items():
                    summary = frame.summary()
                    if not summary["geometryPixels"]:
                        raise RuntimeError("Stream has no terrain depth")
                    snapshots[label] = summary
                    print(json.dumps({"verificationFrame": label, **summary}), flush=True)
                    if args.snapshot and label in ("a", "b"):
                        frame.export_png(args.snapshot.with_name(args.snapshot.stem + "-" + label + ".png"))
                if snapshots["a"]["colorSha256"] == snapshots["b"]["colorSha256"]:
                    raise RuntimeError("Stream colour did not change with camera")
            if args.snapshot and last_frame is not None:
                last_frame.export_png(args.snapshot)
            result = {"type": "stream-result", "requestedFps": args.fps,
                      "elapsedSeconds": round(elapsed_seconds, 3),
                      "received": frames.received, "consumed": consumed, "replaced": frames.replaced,
                      "stale": frames.stale, "verified": args.verify,
                      "clockUncertaintyMs": uncertainty, "ageP50Ms": percentile(ages, .5),
                      "ageP95Ms": percentile(ages, .95), "ageMaxMs": max(ages),
                      "readbackP95Ms": percentile(readbacks, .95), "transferP95Ms": percentile(transfers, .95),
                      "server": status["stream"]}
            print(json.dumps(result), flush=True)
            return result
        finally:
            try:
                if not stopped:
                    request(client, reader, {"type": "release"})
                    request(client, reader, {"type": "stream-stop"})
            finally:
                if frames is not None:
                    frames.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=37122)
    parser.add_argument("--fps", type=int, choices=range(1, 31), default=20)
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--consumer-ms", type=float, default=5, help="Slow consumer test; background receiver still drains TCP")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--snapshot", type=Path, help="Optional local PNG snapshot; keep generated game data untracked")
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or not 0 < args.seconds <= 120:
        parser.error("seconds must be 0..120")
    if not math.isfinite(args.consumer_ms) or not 0 < args.consumer_ms <= 500:
        parser.error("consumer-ms must be 0..500")
    run(args)


if __name__ == "__main__":
    main()
