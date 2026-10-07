# CS2 host adapter

CounterStrikeSharp is a server-side Source 2 framework. It can provide offline
player/map/event hooks, but it does not itself composite a second renderer into a
CS2 client. The client-side frame/depth stage therefore remains a separate task.

Use `game/launch-offline.ps1` for local testing only. Never launch this build into
official matchmaking.

## Native checkpoint

The [native lab](native/README.md) validates Windows loopback reception and
isolated D3D11 depth composition, with four passing CTest suites. The ReShade
upload/diagnostic-inset add-on was also verified in actual offline CS2 on
2026-10-07: D3D11 callbacks, own-texture uploads and FX compilation succeeded.
Live Minecraft terrain/sky appeared in an inset over local Dust2. Pausing the
Minecraft world stopped its stream, released its buffers and removed the inset.

The CS2 session received 6660 frames and uploaded 5367 at guest size 1280x720,
with zero reported resource creation failures. Startup reception preceded effect
presentation, so these totals are not a steady-state FPS measurement. The
independent D3D11 test (180 frames/10s, age P95 112.33ms) uses known eye-space
geometry. Actual CS2 camera/depth and world alignment remain unverified; the
inset and independent GPU oracle do not establish CS2 world composition.

The approved temporary install created only `game/bin/win64/dxgi.dll`. Official
`RESHADE_BASE_PATH_OVERRIDE` kept config/effects/logs/cache outside the game via
the launched process environment. The loader was restored after graceful exits;
the game directory matches its pre-install backup. A D3D11 reference-count
warning also occurred with only ReShade loaded, so leak attribution remains open.
Five install/restore fixture tests pass. See the native README for the reversible
PowerShell 7.4+ workflow.

## Next client slice

The Minecraft prototype now exposes [bounded binary world frames](../docs/frame-stream.md).
Its independent Python receiver also serves as a native interoperability oracle.
The next client experiment is measuring CS2's actual rendered camera/projection
and depth, then proving an in-world cube with correct occlusion. Resize and world
switch lifecycle checks are also open. No Metamod/CounterStrikeSharp installation
is included. The upstream GTA compositor is an architectural reference, not
a CS2-compatible binary or verified Source 2 API.

Own-resource creation/upload is now proven in the offline renderer. Camera and
depth measurements must establish a compatible coordinate space before the
in-world cube/depth test. Server eye position
or the Python camera demo does not establish client camera integration. Renderer
API, depth resource selection, reversed-Z, resolution and callback timing need
measurement on this installed CS2 build before Minecraft composition is claimed.

Use a network worker for the frame reader and a latest-frame mailbox. Never read
sockets or wait for Minecraft on CS2's render thread. Reject wrong-session,
partial, stale or CRC-invalid payloads before GPU upload. Clear the layer on
disconnect/restart. The guest OpenGL depth, row orientation and sky/fog require
explicit conversion; drawing a flat full-screen guest image is not the target
depth composition test.
