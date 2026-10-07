# Minecraft 1.20.1 camera lab

This Fabric client mod accepts camera position, yaw/pitch and vertical FOV over
local TCP, exports an on-demand world colour/depth bundle and streams bounded
binary world frames with asynchronous GPU readback. It does **not**
move the simulated player, forward clicks, share GPU textures or draw
anything in CS2 yet.

**Validation status:** the full Fabric build succeeded and produced the mod jar.
The Java network/state/capture protocol tests pass under Gradle. The development client
loaded CounterCraft; a new single-player lab world accepted 100 consecutive
camera requests and a release over real localhost TCP.
Render-side verification passed in that world: two actual camera poses/FOVs and
projection matrices matched the requests; distinct 1280x720 colour frames and
non-empty terrain depth were exported, then the vanilla camera returned after
release. Both exported images were inspected visually.

## Build

Use JDK 21 to run Gradle 8.14; the mod compiles to Java 17 bytecode. Loom 1.10.5,
Yarn 1.20.1+build.10 and Fabric Loader 0.19.5 are pinned in `build.gradle`.
Fabric API is not required. No Minecraft files are shipped in the mod jar.

From the repository root:

```powershell
./scripts/build-minecraft.ps1 -Gradle 'C:\path\to\gradle-8.14\bin\gradle.bat' -JavaHome 'C:\path\to\jdk21'
```

The script uses `.local/gradle` in this repository for its cache. The output jar is
`minecraft/build/libs/countercraft-minecraft-0.1.0.jar`. The sources jar is not the
playable artifact.

For downloads, the script forwards HTTP(S)_PROXY host/port to Gradle's Java
process. Only HTTP proxy URLs without embedded credentials are supported. It
does not change the user's persistent Java or proxy configuration.

## Isolated lab

Keep the existing `1.20.1-OptiFine_I6` instance intact. Use a **separate Fabric
1.20.1 instance** and a new disposable world, never an existing survival save.
OptiFine/OptiFabric are not supported by this prototype.

The mod is inactive by default. The lab JVM argument is:

```text
-Dcountercraft.enabled=true
```

Loom's `runClient` sets that property and uses the isolated `minecraft/run`
directory. It opens a development client, not PCL's normal profile. Build first,
then launch from the repository root with the same Gradle and JDK versions:

```powershell
./scripts/build-minecraft.ps1 -Gradle 'C:\path\to\gradle-8.14\bin\gradle.bat' -JavaHome 'C:\path\to\jdk21' -Task runClient
```

Do not launch multiple Gradle builds at once. If the window hangs while the log
repeatedly reports an OpenAL device reset failure, add `-NullAudio`. This opts
into OpenAL's null output backend only for that invocation; the lab will have no
audible sound. Normal launches and the user's PCL settings are unaffected.

Add `-World 'CounterCraft Lab'` to open that already-existing save directly. The
helper only accepts saves in `minecraft/run/saves` and rejects absent names.
Optional `-Background` keeps this opted-in single-player lab running when it
loses window focus, without writing `options.txt`. Explicit pause menus still
release control and disable capture; multiplayer keeps normal focus behavior.

## Verify a camera session

Enter and unpause a single-player test world. From the repository root:

```powershell
python -m bridge.minecraft_host         # status only
python -m bridge.minecraft_host --demo  # five-second camera motion
python -m bridge.minecraft_host --capture  # one world colour/depth bundle
python -m bridge.minecraft_host --verify   # two rendered camera/FOV poses + release
```

The game-side port is **37122**, distinct from the Python diagnostic server's
37121. Only one host is accepted at a time. The wire format is JSON Lines v1:
`hello` (`role: test` or `cs2`), `status`, `camera`, `release`, `capture`.

- Camera `position` is in MC blocks; `rotation` is `[yaw,pitch,0]` in degrees.
- Frame numbers must increase within each TCP connection, including after release.
- Roll is rejected. The camera stays within 64 blocks of the player's eye position
  because this slice does not move the chunk-loading player.
- Camera control expires after 500 ms without a fresh pose or world snapshot.
  Pause, disconnect, world changes and leaving single-player release control.
- Two seconds without incoming TCP data closes the connection. Messages are bounded,
  and game objects are only read from Minecraft's client thread.

`ack` means the request was accepted. `--verify` additionally checks the actual
render-side camera, projection, colour change and depth, and confirms release.
Check `run/logs/latest.log` for `Camera lab listening` and any Mixin errors.

## Diagnostic world frames

`capture` schedules a single readback on the render thread just after the world
pass, before vanilla clears depth for the hand/HUD. Only a fresh, unpaused
single-player world is accepted. The network worker waits at most three seconds;
PNG/depth writing runs on a separate worker. Capture is bounded to 4,194,304
pixels and one pending job. GPU readback is synchronous and can stall a frame;
this is a diagnostic path, not a per-frame compositor feed.

Replies return the local `frame.json` path under
`minecraft/run/countercraft/captures/<uuid>/`. The manifest is published last:

- `color.png`: world RGBA, top row first, without hand/HUD. Sky and fog remain.
- `depth.f32`: the same pixel order, little-endian float32 OpenGL window depth
  in [0,1], **not reversed Z**. This is not directly interchangeable with CS2 depth.
- `frame.json`: dimensions, world epoch, requested camera sequence (or -1 for
  vanilla), actual rendered camera/FOV, near/far planes and column-major
  projection/view rotation matrices. The camera position supplies the separate
  world translation. `monotonicNanos` is a Java-process clock, not UTC.

`bridge.frame_capture.read_frame` checks dimensions, encoding, matrices, file
size and finite depth samples. `linear_depth` converts this standard perspective
depth into positive eye-space distance. Data is ignored by Git; keep it local.
The wire `renderer: false` still means **no CS2 renderer**, even when capture
and stream capabilities are available.

## Continuous world frames

The independent receiver validates actual frames and measures transport:

```powershell
python -m bridge.stream_host --verify --seconds 10 --fps 20 --snapshot .local/stream.png
python -m bridge.stream_host --seconds 10 --fps 30 --consumer-ms 200
```

`stream-start`/`stream-stop` extend the control protocol. A stream gets a fresh
UUID and ephemeral loopback TCP port; its lifetime belongs to its control host.
World readback uses three PBOs and zero-timeout fence polling. CPU copy and TCP
publication are bounded by two leased arrays and one replaceable pending frame.
Pause, stale ticks, world change and host disconnect stop the stream; a stalled
receiver times out rather than block rendering. Reconnect creates a new session.

Streaming preserves GL bottom-to-top row order, unlike top-to-bottom diagnostic
PNG bundles. Depth remains non-reversed OpenGL window-z. Camera/matrices are
frozen at issue time. This is a CPU-copy prototype for a future compositor;
there is no CS2 client integration yet. See [binary layout and timing](../docs/frame-stream.md).
