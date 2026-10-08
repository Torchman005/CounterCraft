"""Run the actual Windows native receiver against an independent loopback fixture."""
import argparse
import copy
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest
import uuid
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from bridge.test_frame_stream import packet, SESSION
from bridge.frame_stream import HEADER, FrameReader

# Deliberately different clock origin: matching Python/Java/C++ epochs is not assumed.
CLOCK_OFFSET = 9_000_000_000_000
PROBE = None


def make_packet(sequence=1, *, age_ns=0, session=SESSION, depth=None, camera=None):
    base = copy.deepcopy(FrameReader(Bytes(packet()), SESSION, 7).read().metadata)
    base["monotonicNanos"] = time.perf_counter_ns() + CLOCK_OFFSET - age_ns
    if camera is not None:
        base['requestedFrame']=camera['frame']
        base['camera']={key:camera[key] for key in ('position','rotation','fov')}
    data = packet(sequence, base, session)
    if depth is not None:
        fields = list(HEADER.unpack(data[:64]))
        body = data[64:-8] + struct.pack("<ff", *depth)
        fields[10] = zlib.crc32(body)
        data = HEADER.pack(*fields) + body
    return data


class Bytes:
    def __init__(self, data): self.data = memoryview(data)
    def recv_into(self, target):
        n = min(len(target), len(self.data))
        target[:n] = self.data[:n]; self.data = self.data[n:]
        return n


class Fixture:
    def __init__(self, scenario, offline=True):
        self.scenario, self.offline = scenario, offline
        self.closed = threading.Event()
        self.errors = []
        self.cameras = []
        self.releases = 0
        self.camera_arrived = threading.Event()
        self.control = self.listen()
        self.binary = self.listen()
        self.port = self.control.getsockname()[1]
        self.threads = []

    @staticmethod
    def listen():
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0)); sock.listen(1); sock.settimeout(3)
        return sock

    def start(self):
        t = threading.Thread(target=self.serve, daemon=True)
        self.threads.append(t); t.start()

    def serve(self):
        try:
            with self.control.accept()[0] as control, control.makefile("rb") as reader:
                control.settimeout(3)
                pings = 0
                for line in reader:
                    message = json.loads(line)
                    kind = message["type"]
                    if kind == "hello": response = {"type":"ready", "stream":True}
                    elif kind == "ping":
                        pings += 1
                        response = {"type":"status", "serverMonotonicNanos":time.perf_counter_ns()+CLOCK_OFFSET,
                                    "offline":self.offline, "epoch":7, "position":[10,64,20], "stream":{"running":True}}
                        if self.scenario == "epoch" and pings > 6: response["epoch"] = 8
                        if self.scenario == "control-eof" and pings > 6: return
                    elif kind == "stream-start":
                        response = {"type":"stream-started", "host":"127.0.0.1", "port":self.binary.getsockname()[1],
                                    "session":str(SESSION), "epoch":7}
                        t = threading.Thread(target=self.send_frames, daemon=True)
                        self.threads.append(t); t.start()
                    elif kind == 'camera':
                        self.cameras.append(message)
                        self.camera_arrived.set()
                        response = dict(type='ack',frame=message['frame']+(1 if self.scenario=='camera-bad-ack' else 0))
                    elif kind == 'release':
                        self.releases += 1
                        response = dict(type='released')
                    else: raise AssertionError(kind)
                    control.sendall((json.dumps({"v":1, **response})+"\n").encode())
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass  # Expected cancellation.
        except Exception as e: self.errors.append(repr(e))
        finally: self.closed.set()

    def send_frames(self):
        try:
            with self.binary.accept()[0] as binary:
                binary.settimeout(3)
                binary.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                if self.scenario in ('render-camera','wrong-render-camera'):
                    if not self.camera_arrived.wait(2): raise AssertionError('No relayed camera')
                    camera=copy.deepcopy(self.cameras[-1])
                    if self.scenario=='wrong-render-camera': camera['position'][0]+=1
                    binary.sendall(make_packet(camera=camera))
                elif self.scenario == "blocked":
                    binary.sendall(b"CCF")  # Cancellation in the middle of a header.
                else:
                    data = make_packet()
                    if self.scenario == "fragment":
                        for offset in range(0, len(data), 7):
                            binary.sendall(data[offset:offset+7])
                            if offset < 64: time.sleep(.001)
                    elif self.scenario == "crc":
                        data = bytearray(data); data[-1] ^= 1; binary.sendall(data)
                    elif self.scenario == "partial": binary.sendall(data[:-1]); return
                    elif self.scenario == "session": binary.sendall(make_packet(session=uuid.uuid4()))
                    elif self.scenario == "duplicate": binary.sendall(data+data)
                    elif self.scenario == "stale": binary.sendall(make_packet(age_ns=700_000_000))
                    elif self.scenario == "future": binary.sendall(make_packet(age_ns=-700_000_000))
                    elif self.scenario == "depth": binary.sendall(make_packet(depth=[float("nan"), 1.]))
                    else: binary.sendall(data)
                # Leave socket open; control heartbeat must continue despite no new frame.
                self.closed.wait(3)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass
        except Exception as e: self.errors.append(repr(e))

    def close(self):
        self.closed.set()
        self.control.close(); self.binary.close()
        for t in self.threads: t.join(4)
        if any(t.is_alive() for t in self.threads): raise AssertionError("Fixture thread leak")
        if self.errors: raise AssertionError(self.errors)


class NativeSocketTests(unittest.TestCase):
    def test_rendered_camera_is_checked_independently_of_ack(self):
        for scenario in ('render-camera','wrong-render-camera'):
            fixture=Fixture(scenario);fixture.start()
            try:
                result=subprocess.run([str(PROBE),str(fixture.port),'.8','camera'],capture_output=True,text=True,timeout=5,check=True)
                report=json.loads(result.stdout)
                if scenario=='render-camera':
                    self.assertEqual(report['cameraFramesMatched'],1)
                    self.assertEqual(report['failure'],'')
                else:
                    self.assertIn('Rendered MC position',report['failure'])
                    self.assertEqual(report['cameraFramesMatched'],0)
                    self.assertTrue(report['cleared'])
            finally: fixture.close()

    def test_camera_mapping_and_stale_release_while_waiting_for_frames(self):
        fixture = Fixture('blocked'); fixture.start()
        try:
            result = subprocess.run([str(PROBE),str(fixture.port),'.8','camera'],capture_output=True,text=True,timeout=5,check=True)
            report = json.loads(result.stdout)
            self.assertEqual(report['failure'],'')
            self.assertEqual(report['camerasSent'],2)
            self.assertEqual(report['cameraReleases'],1)
            self.assertEqual([m['frame'] for m in fixture.cameras],[1,2])
            self.assertEqual(fixture.cameras[0]['position'],[10,64,20])
            self.assertEqual(fixture.cameras[1]['position'],[11,66,22])
            self.assertEqual(fixture.cameras[0]['rotation'],[-90,10,0])
            self.assertEqual(fixture.cameras[1]['rotation'],[-180,-20,0])
            self.assertEqual(fixture.cameras[1]['fov'],80)
            self.assertEqual(fixture.releases,1)
        finally: fixture.close()

    def test_camera_ack_mismatch_terminates_control(self):
        fixture = Fixture('camera-bad-ack'); fixture.start()
        try:
            result = subprocess.run([str(PROBE),str(fixture.port),'.8','camera'],capture_output=True,text=True,timeout=5,check=True)
            report = json.loads(result.stdout)
            self.assertIn('acknowledgement mismatch',report['failure'])
            self.assertEqual(report['camerasSent'],0)
            self.assertTrue(report['cleared'])
        finally: fixture.close()

    def run_case(self, name, seconds=.8, offline=True):
        fixture = Fixture(name, offline); fixture.start()
        try:
            result = subprocess.run([str(PROBE), str(fixture.port), str(seconds)],
                                    text=True, capture_output=True, timeout=5, check=True)
            data = json.loads(result.stdout)
            self.assertTrue(data["cleared"])
            self.assertLess(data["stopMs"], 200, data)
            return data
        finally: fixture.close()

    def test_fragmented_frame_and_nonshared_clock(self):
        data = self.run_case("fragment", .3)
        self.assertEqual(data["failure"], ""); self.assertEqual(data["received"], 1)
        self.assertTrue(data["payload"]); self.assertTrue(data["latest"])

    def test_crc_and_invalid_depth_rejected_before_mailbox(self):
        for case, reason in (("crc", "CRC"), ("depth", "depth sample")):
            with self.subTest(case=case):
                data = self.run_case(case)
                self.assertIn(reason, data["failure"]); self.assertEqual(data["received"], 0)
                self.assertFalse(data["latest"])

    def test_partial_and_wrong_session(self):
        for case, reason in (("partial", "partial"), ("session", "session")):
            with self.subTest(case=case):
                data = self.run_case(case)
                self.assertIn(reason, data["failure"]); self.assertFalse(data["latest"])

    def test_duplicate_clears_previous_frame(self):
        data = self.run_case("duplicate")
        self.assertEqual(data["received"], 1); self.assertFalse(data["latest"])
        self.assertIn("sequence", data["failure"])

    def test_stale_and_future_never_uploaded(self):
        for case in ("stale", "future"):
            with self.subTest(case=case):
                data = self.run_case(case, .3)
                self.assertEqual(data["stale"], 1); self.assertEqual(data["observed"], 0)

    def test_pause_refused(self):
        data = self.run_case("valid", offline=False)
        self.assertIn("paused", data["failure"]); self.assertEqual(data["received"], 0)

    def test_epoch_change_and_control_death_clear(self):
        for case in ("epoch", "control-eof"):
            with self.subTest(case=case):
                data = self.run_case(case)
                self.assertEqual(data["received"], 1); self.assertFalse(data["latest"])
                self.assertTrue(data["failure"])

    def test_cancellation_while_partial_read(self):
        data = self.run_case("blocked", .3)
        self.assertEqual(data["failure"], ""); self.assertEqual(data["observed"], 0)

    def test_mailbox_expires_without_new_packets(self):
        data = self.run_case("valid")
        self.assertEqual(data["failure"], ""); self.assertEqual(data["received"], 1)
        self.assertGreater(data["observed"], 0); self.assertFalse(data["latest"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--probe", type=Path, required=True)
    args, remaining = parser.parse_known_args(); PROBE = args.probe.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
