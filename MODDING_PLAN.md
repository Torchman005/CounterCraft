# CounterCraft offline modding plan

## Current checkpoint (2026-10-09, alpha.2)
Experimental world fusion now has an opt-in supervised launcher, owned world/
final depth, a current-frame GPU matrix gate and passing hardware oracles. Real
Dust2 still produces zero effect-eligible frames due to later uncalibrated depth
viewport changes. Live occlusion remains the next acceptance; see
[world-fusion experiment](docs/world-fusion.md). The playable default remains the
full-client alpha bridge.


Basic text/editing events are now implemented and native chat + Enter is accepted
in actual CS2. A FIFO/epoch/expiry/ack protocol avoids text loss in held-state
sampling; IME/clipboard/typematic remain absent. Guest-only vanilla acceptance
confirms redstone lit/unlit, live pig damage and survival health, with disposable
fixture cleanup; death UI Tab/Enter also respawned the real player. These do not
prove a CS2-native redstone interaction or host entities/collision coupling.
Sprint now goes through vanilla key handling. Earlier native command testing
exposed R-as-right-click corruption in chat, fixed by restricting R to world view.

The offline bridge alpha can be built, launched, operated, restored and packaged.
The original complete-port objective remains open: host world-depth integration,
cross-game player/collision/chunk semantics, broader control/survival acceptance,
text composition and performance/lifetime work. Do not conflate these scopes.

## Current checkpoint (2026-10-08, alpha packaging)

Full-client passthrough and continuous CS2 input are now implemented and tested.
Right/left mouse, movement/look, creative inventory and vanilla guest crafting
have actual acceptance evidence. Pause/resume and guest resize reconnect with
neutral input and zero observed resource failures. Full owned-client startup,
Steam startup, exit, hash-guarded loader restore and owned MC close passed.
Aspect-fit display and GUI pointer alignment were visually verified in a
1680x1050 CS2 client. An allowlisted source-assisted alpha package contains only
own artifacts/source/docs, with integrity manifests and recovery instructions.

The complete MC-in-CS2 world integration is **not finished**. Remaining work:
host world-depth composition and collision/player coupling, text input, broader
survival/entity/redstone gameplay acceptance and performance/lifetime analysis.
The round estimate and implementation statuses below are historical; they are
not a current completion forecast. See README and MODLOG for current evidence.

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
  loopback receiver verified. Eight CTest suites currently pass, including nine
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
- Background observer checkpoint: a fixed-capacity value-event queue replaces
  callback try_lock/whole-inventory copy contention. Eight CTest suites (including
  five concurrency/observer groups), 18 Python regressions and 26 workflow fixtures
  pass. Steam offline Dust2 emitted 165 reports/139 with candidates across three
  add-on lifetimes; main lifetime drained 21,227,415 events, defined missed/full/
  contended/callback-failure/deferred/overflow counters all zero, knownLossFree=true.
  Peak pending sampled at drain start was 9291/16384, max background drain 2388us,
  active snapshot P50/P95/max 37/59/162us. Candidate/view totals and lifetime-local
  counter monotonicity passed. Final runtime/devices/pending returned to zero;
  restored game folder matched its new backup. This is continuity/background
  measurement, not a complete callback-path or FPS baseline claim. Camera/depth
  identity/MSAA access still open; unexplained teardown reference warning remains.

## Remaining acceptance stages

The working estimate is 5-8 further stage acceptances to an **offline playable
prototype**, conditional on the host-camera and depth route working. It is not
a guarantee of a complete all-feature Minecraft port. A stage can require more
than one round when the real-game oracle exposes a new issue.

1. Identify the relevant depth candidate with scene evidence, establish MSAA
   handling and measure render/total overhead. Observer's defined event gaps were
   zero in the new offline session; unknown callback coverage still needs evidence.
   Raw single/MSAA sampling now passes 32 hardware cases and real four-sample
   Dust2 export (18 captures). A separate writer preserves observer continuity:
   16,976,442 events, defined loss/deferred/overflow zero in the final run.
   Pre-draw depth is partial; complete world-pass selection is not established.
   Actual viewport depth is [0,0.95], so range-aware inversion is required next.
2. Confirm the host camera/projection/viewport and depth convention.
   Range-aware inversion and explicit caller-confirmed background now pass 192
   independent GPU cases. An offset-free, offline binding analyzer finds one
   mathematically consistent view/projection/full-VP/relative-VP set in each of
   36 real captures. Scene pose, units and final-pass timing remain unverified.
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

The 2026-10-09 retained-coverage stage protects observed post-world depth changes
across known full clears and passes 19 native suites. Live world snapshots now
survive viewport changes but still reject a later different depth resource;
camera relay/effect eligibility remain zero. Steam updated CS2 to Build 25815307
before these sessions. Next: create an updated private backup, revalidate local
camera/pass identity and trace the depth-resource transition before accepting
cross-resource coverage. Then run the visible front/behind-wall cube oracle.
