# World frame stream v1

This is a CPU-copy, loopback TCP prototype for an independent receiver and a future
CS2 renderer. It is not GPU shared textures, zero-copy transport, or CS2 integration.
Minecraft continues to own world simulation. Its player and chunk loader do not move
when a host changes the render camera.

## Control and lifetime

Connect to `127.0.0.1:37122`, send JSON Lines v1 `hello` with `role: test` or `cs2`.
`ready.stream` advertises the capability; `ready.renderer` remains false.

- `stream-start` with integer `fps` in 1..30 requires a fresh, unpaused singleplayer
  world. The reply is `stream-started`, with session UUID, world epoch, actual
  ephemeral port, `host: 127.0.0.1` and requested rate limit.
- Connect a binary receiver to that port within two seconds. Only one receiver is
  accepted per stream. There is no second handshake on the binary socket.
- `status`/`ping` include stream counters and `serverMonotonicNanos`. Send control
  at least once per second; two seconds without incoming control closes the host.
- `stream-stop` is idempotent. Reconnect/start creates a fresh UUID and sequence.
  Explicit pause, world/epoch changes, stale world ticks and host disconnect stop
  streaming. GL buffers/fences are released on the render thread, including shutdown.

The binary connection cannot control the game. Both listeners bind only IPv4
loopback. This is a local lab protocol, without authentication or encryption.

The native receiver permits idle delivery while no byte of the next binary header
has arrived and control heartbeats still confirm the world, epoch and stream.
Once the first header bytes arrive, the entire packet has a strict two-second
completion deadline; partial headers and payloads still fail. Old mailbox frames
disappear after 500 ms even during healthy idle. Cancellation and control failures
remain active while waiting. Socket fixtures verify a 2.3-second idle followed by
successful delivery, stale-frame hiding, and partial-header timeout.

## Packet layout

Packets are concatenated on TCP. Every field below is unsigned little-endian;
the 64-byte header is followed by JSON UTF-8, RGBA bytes, then depth bytes.

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 8 | ASCII `CCFRM001` |
| 8 | 4 | version = 1 |
| 12 | 4 | header bytes = 64 |
| 16 | 4 | JSON bytes, 1..4096 |
| 20 | 4 | RGBA bytes = width × height × 4 |
| 24 | 4 | depth bytes = RGBA bytes |
| 28 | 4 | flags = 0 |
| 32 | 8 | UUID most significant 64 bits |
| 40 | 8 | UUID least significant 64 bits |
| 48 | 8 | sequence, increasing from 1 within a session |
| 56 | 4 | IEEE CRC32 of JSON + RGBA + depth, in that order |
| 60 | 4 | reserved = 0 |

Readers must validate lengths/session/sequence before allocating payloads, enforce
4,194,304 pixels maximum, read exact lengths, and validate CRC and metadata before
publishing a frame. A partial, corrupt, wrong-session or out-of-order packet ends
the receiver; never scan arbitrary payload bytes to resynchronize.

JSON `type` is `world-stream-frame`. It carries the same camera, near/far,
column-major projection and view rotation, epoch and `requestedFrame` contract as
diagnostic captures. `requestedFrame = -1` means vanilla view. Metadata is frozen
at GPU readback **issue**, not completion. Translation is the camera position;
the view rotation matrix alone is not a full translated world-view matrix.

The payload deliberately preserves OpenGL **bottom-to-top** rows. `colorEncoding`
is `rgba8`; depth is float32 little-endian window-z in [0,1], **non-reversed Z**.
Both planes share pixel order. No hand/HUD is included; sky/fog remain. A future
CS2 compositor must handle row orientation, projections, sky and host depth space.

## Bounded work and freshness

The render hook issues two `glReadPixels` commands into one PBO with separate
colour/depth regions. Three PBO slots have GL fences. Completion is polled with
zero timeout; only signaled buffers are mapped and copied. Full GPU rings and
unavailable CPU buffers drop work. Resize drops completed incompatible-size
payloads and reallocates free PBOs; the receiver validates dimensions per packet.

At most two CPU payload arrays are leased: one in the socket writer and one pending.
Pending is a latest-frame mailbox, not a queue. A non-reading receiver cannot hold
the render thread; a single packet write exceeding 500 ms closes its stream.
Packets older than 500 ms are discarded before send. At the maximum resolution,
three PBOs plus two CPU arrays account for at most 160 MiB of payload storage,
excluding drivers, socket buffers, metadata and transient resize allocations.

The Python receiver drains TCP on a worker and keeps one latest complete frame.
It validates epoch/session and uses a calibrated clock offset to reject frames
older than 500 ms, both on publication and on consumption. Clock calibration
uses the midpoint of the smallest-RTT control ping; the reported uncertainty is
half that RTT. This estimate is not a guarantee of synchronized clocks.

`monotonicNanos` is Java's issue clock, not UTC. `readbackNanos` is issue-to-CPU-copy
duration on that clock. CLI `ageP95Ms` estimates issue-to-consumption, including
transport; `transferP95Ms` measures metadata/payload receive after its header.
Max hook time includes allocation, driver submission, polling, copy and JSON
encoding. PBOs avoid deliberate GPU waits; driver calls can still stall. This
prototype copies full-resolution planes on CPU and is not a 60 FPS promise.

## Repeatable lab verification

```powershell
python -m bridge.stream_host --verify --seconds 10 --fps 20 --snapshot .local/stream.png
python -m bridge.stream_host --seconds 10 --fps 30 --consumer-ms 200
```

`--verify` requires vanilla before/after release, both requested poses, matching
actual camera/projection, distinct colour hashes and nonempty terrain depth.
It retains at most four diagnostic frames; full depth scans and PNG encoding
run after stream stop, so verification does not starve the realtime TCP drain.
The second command deliberately consumes slowly while the background receiver
drains; its replaced count demonstrates bounded latest-frame delivery. Unit tests
also use a genuinely non-reading TCP receiver to check the sender's timeout.
All snapshots/game data stay local and untracked.

## Measured checkpoint, 2026-10-07

The final Fabric build was tested in the real isolated `CounterCraft Lab` at
1280×720. A 10.005-second, 20 FPS-limited camera verification received 184 frames
(about 18.4 FPS), with zero sender drops/receiver replacements/stale frames.
Estimated issue-to-consumption age was median 34.73 ms, P95 42.82 ms, maximum
60.16 ms; clock uncertainty was 0.25 ms. Readback P95 was 18.56 ms. These are
this machine/world's measurements, including CPU copies, not a rate guarantee.

A 30 FPS-limited, slow-consumption test received 280 frames in 10.103 seconds
(about 27.7 FPS). The foreground consumed 49 frames and the latest mailbox
replaced 230; no sender drops/stale frames, estimated age P95 64.47 ms.

A real non-reading receiver closed in 0.57 seconds with the 500 ms write-timeout
reason and zero remaining CPU leases. A fresh session then delivered sequence 1
with a different UUID. Closing its control connection stopped streaming and
released camera control. Diagnostic capture remains separately regression-tested.

Actual pause/resize/world-switch GL behavior has not passed an in-game test:
window activation failed twice, so those checks are still open. Protocol tests
cover menu/stale-world rejection, bounded resources and restart/partial packets;
they do not substitute for those game-side GL lifecycle checks.
