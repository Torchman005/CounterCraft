# CounterCraft offline modding plan

- Install: `D:\steam1\steamapps\common\Counter-Strike Global Offensive` (Steam app 730, build 25738536; reinstalled 2026-10-08)
- Engine: Source 2; client files contain `game/core/gameinfo.gi` and `game/csgo/gameinfo.gi`
- Minecraft: `D:\pcl2\Release 2.8.3`, requested instance `1.20.1-OptiFine_I6`
- Route: an offline passthrough bridge. Minecraft remains the simulation and CS2 is the host view.
- First slice: local JSON camera handshake and a deterministic coordinate transform. Rendering/depth composition requires a CS2 client renderer integration and is not included in this source-only slice.
- Safety: launch CS2 with `-insecure`; never connect this client to official matchmaking.

## Compatibility decision

OptiFine is a client renderer patch and does not expose the Fabric hooks used by the
upstream passthrough example. The bridge protocol is loader-neutral, but the Minecraft
adapter must be built against a separate isolated 1.20.1 Fabric profile before it can
follow a CS2 camera. Existing OptiFine worlds remain untouched.

## Milestones

1. `bridge/protocol.py` defines camera messages and coordinate transforms; diagnostic TCP handshake and validation have tests. It is not a renderer or relay.
2. Fabric 1.20.1 camera lab: receive host poses over localhost, apply render-only camera/FOV overrides, and restore the vanilla view on timeout. Validate actual render-side poses and export diagnostic world colour/depth.
3. Async world readback/bounded frame transport, verified independently. Then implement the CS2 offline host adapter and render one test cube with depth ordering.
4. Add block/entity/event/input forwarding and player/chunk sync, then package a launcher and backup/restore flow.

## Verified checkpoint (2026-10-07)

- Milestone 1: eight Python tests pass, including actual localhost sockets.
- Milestone 2 build: Gradle 8.14/JDK 21 produced the Fabric 1.20.1 jar;
  Java protocol/state/capture tests pass under Gradle. The independent development client loaded
  the mod; the new `CounterCraft Lab` world accepted 100 camera requests and
  released control on port 37122.
- Milestone 2 world validation passed: render-side camera position/rotation,
  FOV and projection matched two requested poses. World colour/depth exported at
  1280x720 and the original view returned on release. Images were inspected.
- Diagnostic export still uses synchronous readback and worker disk writes.
  The additional stream uses a three-PBO ring, nonblocking fence polls, bounded
  CPU-copy payloads and an independent binary TCP receiver; no GPU sharing yet.
- Milestone 3 transport checkpoint passed: 18 Python and 16 Java tests; real
  1280x720 streaming, camera/projection/depth/release, non-reading receiver
  timeout, fresh-session restart and host-disconnect cleanup. A 20 FPS-limited
  test received 184 frames/10.005s, estimated age P95 42.82ms. Actual pause/GL
  cleanup was later verified in the offline CS2 session; resize/world switch
  lifecycle remains open.
- Native lab checkpoint: isolated D3D11 colour/depth composition and Windows
  loopback receiver verified. Seven CTest suites currently pass, including nine
  native socket tests and actual DLL refusal in a non-CS2 process. Hardware MC test received/
  uploaded 180 frames in 10 seconds, age P95 112.33ms. This uses known eye-space
  cube geometry, not the CS2 camera/depth.
- ReShade 6.8.0 actual offline CS2 check passed: D3D11 callbacks, own-texture
  uploads and FX compilation. Real MC terrain/sky appeared in a diagnostic inset;
  MC pause stopped the stream, released buffers and removed the inset. A session
  received/uploaded 6660/5367 frames, with zero resource creation failures; totals
  include startup and are not an FPS benchmark. Actual camera/depth/world fusion
  remains unverified. GPU shared transport and gameplay routing are unimplemented.
- Approved temporary installation created only dxgi.dll; the official process
  base-path override isolated config/log/effects/cache. Installation and restore
  workflow was verified on 2026-10-07. Ten fixtures currently cover install/
  restore, launch previews and preparation without CS2. After that game's graceful
  exit the loader was removed and the directory matched its pre-install backup.
  A teardown reference
  warning also appeared with only ReShade loaded; attribution/lifetime remains open.
- 2026-10-08 host-depth checkpoint: fixed-capacity, opt-in ReShade
  metadata observation, with no depth/camera readback or resource selection. Pure
  event replay covers lifetime/reuse, effect exclusion and bounded accounting.
  Explicit D3D perspective depth inversion supports normal/reversed Z, left/right
  eye space and finite/infinite far. Hardware GPU oracle passes 48 projection/
  resolution/unit combinations. After reinstall, a Steam-launched offline Dust2
  session produced 143 reports (137 with candidates); missed/deferred/overflow
  ended at 20/0/0, so no complete event coverage or world-depth identity claim.
  The temporary loader and INI bootstrap were restored, matching the new backup.
  Direct cs2.exe launch caused Launcher Error #720; current scripts use Steam.
- Next view-description checkpoint: single/MSAA/array DSVs, ignored D3D11
  fields, API default ranges and typed/typeless formats are normalized. Four
  per-view metadata slots per candidate/interval retain independent draw/clear
  counts. Seven CTest suites pass, including 16 depth-inventory groups. After
  fixing a localized Ninja dependency-cache bug and rebuilding every object,
  Steam-launched offline Dust2 verified 2DMS/single-sample descriptions and
  aggregate/per-view accounting. 107 reports/103 with candidates, final coverage
  misses 159/0/0; complete coverage, performance and world-depth identity remain
  open. Temporary files restored and the game folder matches its fresh backup.

## Remaining acceptance stages

The working estimate is 5-8 further stage acceptances to an **offline playable
prototype**, conditional on the host-camera and depth route working. It is not
a guarantee of a complete all-feature Minecraft port. A stage can require more
than one round when the real-game oracle exposes a new issue.

1. Identify the relevant depth candidate with scene evidence, establish MSAA
   handling and address observer event loss/overhead.
2. Confirm the host camera/projection/viewport and depth convention.
3. Align the actual MC view and host pose/units/timing, then verify real occlusion
   with a known in-world cube.
4. Route gameplay input, player state/collision and MC interactions; test building,
   mining and at least one inventory/crafting interaction end to end.
5. Verify resize/world changes/resource release, regress performance/stability,
   and package a repeatable launch/restore flow.

Camera identity, MSAA depth access, latency and cross-game player/collision
semantics are still open; those findings can change both scope and round count.

## Next client slice

Measure the actual offline CS2 camera matrices/depth and prove an in-world cube
with correct occlusion. Also test resize/world-switch and resource lifetime.
Use the reviewed temporary loader workflow and backup/restore scripts for each
test session after the reinstall is complete and its new state is reviewed.
The independent cube test and ReShade/GTA reference are not a verified CS2 adapter.
GPU sharing, input/event routing and player/chunk/collision sync remain later work.
