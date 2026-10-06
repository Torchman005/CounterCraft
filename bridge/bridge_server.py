"""Local test server for the CounterCraft bridge.

It only listens on loopback and acknowledges validated protocol messages.
The real CS2 adapter will replace this endpoint once a renderer route is
selected.
"""

from __future__ import annotations

import argparse
import socket
import socketserver
from .protocol import MAX_MESSAGE_BYTES, decode, encode


class Handler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        self.request.settimeout(5)
        greeted = False
        last_frame = -1
        while True:
            try:
                line = self.rfile.readline(MAX_MESSAGE_BYTES + 1)
                if not line:
                    return
                if not line.endswith(b"\n"):
                    raise ValueError("message must end with newline and fit within 64 KiB")
                message = decode(line)
                if not greeted:
                    if message["type"] != "hello" or message.get("role") not in ("mc", "cs2", "test"):
                        raise ValueError("first message must be hello with role mc, cs2 or test")
                    greeted = True
                    reply = {"type": "ready", "mode": "diagnostic", "renderer": False}
                elif message["type"] == "camera":
                    if message["frame"] <= last_frame:
                        raise ValueError("camera frame must increase within a connection")
                    last_frame = message["frame"]
                    reply = {"type": "ack", "frame": last_frame}
                elif message["type"] == "ping":
                    reply = {"type": "pong"}
                else:
                    raise ValueError("unsupported message in diagnostic mode")
                self.wfile.write(encode(reply))
            except (ValueError, UnicodeDecodeError) as exc:
                try:
                    self.wfile.write(encode({"type": "error", "message": str(exc)[:300]}))
                except OSError:
                    pass
                return
            except (OSError, socket.timeout):
                return


class BridgeServer(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, port=37121):
        super().__init__(("127.0.0.1", port), Handler)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=37121)
    args = parser.parse_args()
    with BridgeServer(args.port) as server:
        print(f"CounterCraft bridge listening on 127.0.0.1:{args.port}")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
