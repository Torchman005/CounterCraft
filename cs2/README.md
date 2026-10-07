# CS2 host adapter

CounterStrikeSharp is a server-side Source 2 framework. It can provide offline
player/map/event hooks, but it does not itself composite a second renderer into a
CS2 client. The client-side frame/depth stage therefore remains a separate task.

Use `game/launch-offline.ps1` for local testing only. Never launch this build into
official matchmaking.

## Next client slice

The Minecraft prototype now exposes [bounded binary world frames](../docs/frame-stream.md).
Its independent Python receiver is a protocol oracle for a future native receiver.
The next client experiment is a ReShade add-on candidate, built outside the game
directory first. No ReShade/Metamod/CounterStrikeSharp installation is included in
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
