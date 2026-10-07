# CS2 host adapter

CounterStrikeSharp is a server-side Source 2 framework. It can provide offline
player/map/event hooks, but it does not itself composite a second renderer into a
CS2 client. The client-side frame/depth stage therefore remains a separate task.

Use `game/launch-offline.ps1` for local testing only. Never launch this build into
official matchmaking.

## Native checkpoint

The [native lab](native/README.md) now validates Windows loopback reception and
isolated D3D11 depth composition, with four passing CTest suites. A ReShade
upload/diagnostic-inset candidate has compiled and its actual DLL rejects a
non-CS2 process. `scripts/prepare-cs2-lab.ps1` prepares an installation preview
outside the game directory. It does not install a loader.

Real Minecraft frames were received and uploaded on hardware D3D11 (180/10s,
1280x720, age P95 112.33ms), using known eye-space host geometry. No CS2 camera,
depth, API callback or effect compilation has been verified. Do not describe the
diagnostic inset or independent GPU oracle as CS2 world composition.

## Next client slice

The Minecraft prototype now exposes [bounded binary world frames](../docs/frame-stream.md).
Its independent Python receiver also serves as a native interoperability oracle.
The next client experiment is installing/testing the compiled ReShade candidate
after specific approval and a backup. No ReShade/Metamod/CounterStrikeSharp installation is included in
this checkpoint. The upstream GTA compositor is an architectural reference, not
a CS2-compatible binary or verified Source 2 API.

The host must first prove resource creation/upload and a known cube with depth
ordering in the actual offline CS2 renderer. It must then obtain and verify the
actual rendered view/projection and compatible host depth. Server eye position
or the Python camera demo does not establish client camera integration. Renderer
API, depth resource selection, reversed-Z, resolution and callback timing need
measurement on this installed CS2 build before Minecraft composition is claimed.

Use a network worker for the frame reader and a latest-frame mailbox. Never read
sockets or wait for Minecraft on CS2's render thread. Reject wrong-session,
partial, stale or CRC-invalid payloads before GPU upload. Clear the layer on
disconnect/restart. The guest OpenGL depth, row orientation and sky/fog require
explicit conversion; drawing a flat full-screen guest image is not the target
depth composition test.
