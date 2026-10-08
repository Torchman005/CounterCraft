# CounterCraft investigation

Started: 2026-10-06

## Bounded background host observer (2026-10-08)

- Continued from pushed `973ecf7` with standing authorization for offline tests
  and milestone commit/push. Re-read the installed universal-modder mod-any-game,
  mashup-mods, game-automation and publish-mod workflows. No project/ancestor
  AGENTS.md was found. CS2 is still build 25738536 and starts only through Steam
  with the exact `-insecure`/lab/`+sv_lan 1 +map de_dust2` arguments. Original
  PCL OptiFine instance/worlds, Steam configuration and graphics settings unchanged.
- Replaced HostProbe's callback try_lock and whole-inventory report copy with a
  16384-slot multi-producer/single-consumer value-metadata queue. Each callback
  copies a 136-byte payload, makes at most eight atomic reservation attempts and
  never allocates, waits for the consumer or holds GPU/COM ownership. Sequence
  publication prevents reading partial payloads and the consumer never skips an
  unpublished head. The background reporter drains bounded batches at a 2ms poll
  interval and exclusively owns inventory/snapshot/JSON work. Queue-full,
  producer-contention and callback failures count separately, with rejected event
  kinds. Normal unregistration drains the tail and emits finalReport before unload.
- Added five test groups: ring wrap/FIFO, lifecycle/handle reuse/effect replay,
  full queue plus bounded drain/recovery, four simultaneous producers with exact
  accepted/rejected accounting, and report snapshots overlapping production.
  Eight CTest suites passed in 9.91s; 18 Python regressions in 0.715s; 26 isolated
  loader/Steam fixtures in 127.858s. Clean final build has no MSVC warnings; key
  header dependencies still verified. Initial test discovery pointed at a missing
  bridge/tests directory; correct explicit module run subsequently passed.
- Fresh universal-modder snapshot countercraft-cs2-depth-queue/20261008-095535.zip
  (105 files/445.6 MB) before game writes. Candidate hash
  a42733eb6ae42b61a58baf0ba122895f36682a33a43eeac494e0e994e22c1536;
  install state f92c8be8b27b40d2b3bc6b5ff52c57ec. Steam PID 18224 launched exact
  CS2 PID 43260 with fixed offline flags plus the permitted -perfectworld suffix.
  universal-modder gfxcapture screenshots inspected loading and actual Dust2/bot
  first-person mid scene. No automated focus, input or menu/setting change sent.
- 165 reports, 139 with candidates. Three add-on lifetimes: two short initial
  device probes each drain two events, then the actual render lifetime drains
  21,227,415. Reset counters at each finalReport boundary when checking monotonicity;
  they are lifetime-local, not process-global. All defined missed/full/contended/
  callback-failure/deferred/inventory-overflow counters stay zero. Final
  knownLossFree=true, pending=0, runtimes=0, devices empty. Every parsed per-view
  draw/element/indirect/clear sum equals its candidate aggregate; queued equals
  processed plus pending and accepted counters/intervals are monotonic per lifetime.
- Peak pending sampled at drain start 9291/16384; maximum batch 9291 events,
  maximum drain 2388us, total drain 1,236,339us over the main lifetime. Active
  snapshot build P50/P95/max=37/59/162us. Active report JSON-tree build (including
  snapshot, excluding dump/disk)=672/912/1230us. These measure background work,
  not render callback cost, FPS or an A/B performance baseline. Zero known gaps
  in this session does not prove unknown callback paths are fully covered.
- D3D11 output 1680x1050; four-sample D24S8 resource/2DMS typed DSV and other
  single-sample D24S8/D16 descriptions remain correctly normalized. Main candidate
  draw range includes clear-only intervals (0..2423), nonBaseViewDraws=0. Still
  read-only metadata: no host depth pixels, native resource ownership, camera
  constants, GPU wait, selection or fusion. cameraDepthVerified/autoSelected=false.
  MC not running: no receives/uploads, zero own-resource failures, expected socket
  deadline. This is not a repeat MC preview test or world-depth proof.
- CloseMainWindow sent only to exact verified offline PID 43260; graceful exit,
  final report, runtime destruction, add-on unregister and Finished exiting logged.
  D3D11 reference warning 1661 remains unexplained. Exact state restore removed
  only hash-matched dxgi.dll/bootstrap; no CS2 processes remain. universal-modder
  diff against the fresh ZIP has no added/removed/changed files. Existing crash
  evidence and steam_appid.txt preserved; private logs/screenshots remain ignored.
- Next: depth-candidate scene evidence/MSAA read route, actual host camera/
  projection/viewport/convention, same-frame MC alignment and real cube occlusion;
  then gameplay input/collision/building/mining/crafting and lifecycle/stability.
  This closes the observer-loss subtask only; the working 5-8-stage estimate to
  an offline playable prototype is retained until the camera/depth route is proven.
- Source-only universal-modder publication check compared 78 files with the full
  actual CS2 install: zero failures, one known absolute-user-path warning in this
  journal. Recovery provenance is intentionally retained. No game binaries/assets,
  raw logs/screenshots, dependencies, backups or crash evidence are staged.

## D3D11 DSV normalization and bounded view telemetry (2026-10-08)

- User requested the next round and a remaining-round estimate; standing user
  authorization includes offline tests and stage commit/push. Starting from
  pushed 84f6ecf. No project/ancestor AGENTS.md was found. Continued the installed
  universal-modder workflow; the plugin's own sources remain outside this repo.
- Read pinned ReShade API resource definitions and v6.8.0 public
  d3d11_impl_type_convert.cpp. DSV conversion sets one mip; 2DMS has no mip
  selector, non-array DSV has no slice selector. Unused layer fields are zero,
  while API UINT32_MAX denotes remaining active levels/layers. The old exact
  texture_2d/1-layer check misclassified both MSAA and ignored layer fields.
- Added independent core normalization plus a ReShade description adapter.
  Requires typed DSV/resource depth-family compatibility, the corresponding
  single/MS sampling shape, mip 0 and all resource layers for canonicalBaseDsv.
  Active zero counts, unknown resource ranges, nonzero/partial subresources,
  multi-mip ranges, wrong format and unsupported types stay noncanonical.
  Canonical MSAA is still not single-sample compositing support.
- Per candidate/completed interval, four fixed slots store distinct raw and
  normalized descriptions with bind/draw/indirect/clear counts. Overflow keeps
  aggregate accounting and reports a known coverage gap. Effect boundaries,
  resource reuse, descriptor changes and device destruction reset metadata.
  No new native resource ownership, GPU copy/wait, camera scan or pixel reads.
- First build: seven CTest suites passed in 8.81s. Depth inventory now has 16
  groups covering shape/default/format/subresource behavior and bounded view
  accounting. Test inventories moved to heap to avoid accumulating fixed arrays
  in the test executable's Windows stack. Eighteen Python tests passed in 0.710s.
- Initially prepared only .local/cs2-view-probe-candidate for the current Steam install;
  GameFilesWritten=false and both loader targets remain absent. On checking the
  live process, user's PID 8884 has -steam -perfectworld (no offline/lab flags).
  Computer Use observed an active Dust2 deathmatch. No app input was sent,
  no process was closed and no loader was installed into that session. Asked
  when to perform the required offline relaunch. The process subsequently exited
  on its own; only then continued the already-authorized offline validation.
- The first 22-workflow-check run failed 17 fixtures because their installer
  queries still saw the real CS2 process. Production correctly refused; no real
  game changes. Isolated install/restore and preview process queries in the test
  harness and added a mocked-busy refusal. The wrapper first used an array splat,
  which passed -Mode positionally; switched to named hashtable splatting. Focused
  install/conflict/restore and busy-refusal checks then passed (2 tests, 9.417s).
  The isolated full rerun passed all 23 checks in 106.168s. Production process
  guards unchanged; launch suffix support was added subsequently below. All ten
  tracked PowerShell scripts parsed; Python fixture syntax/whitespace passed.
- Fresh backup countercraft-cs2-view-probe/20261008-091414.zip (104 files,
  444.3 MB), install state 3d1ba1c284f847c8a6058e97d8850846. Steam PID 18224
  started PID 30356 with all lab flags plus a trailing -perfectworld. Launcher
  correctly rejected the unexpected suffix; the process then crashed before
  presentation. Local minidump parser found 0xc0000005 in CounterCraftProbe at
  offset 0x3e2cb. No native scanner/debugger was attached. Restored both temporary
  files; kept cs2_2026_1008_091549_0_accessviolation.mdmp as private evidence.
- Found old addon.cpp object timestamp 01:27 with Ninja #deps 0. CMake cached
  a mojibake Chinese /showIncludes prefix while compiler output was valid
  Chinese; host_probe.cpp/core rebuilt after header layout changes, but the
  allocating/report-caller addon.cpp object did not. This stale-layout mixture
  accounts for the add-on fault; a clean rebuild fixed startup. Build script
  now sets process-local VSLANG=1033/UTF-8, resets non-English cached metadata
  with --fresh and --clean-first, and requires nonzero header dependencies for
  all three key objects. Explicit -Clean is available. Clean build compiled all
  25 steps; CTest passed again in 8.82s. Actual dependencies: addon 279, host
  probe 318, inventory 89. Header timestamp dry-run schedules all three objects.
- Added only exact optional -perfectworld suffix acceptance to both Steam
  launchers; offline/lab arguments and rejection of arbitrary extra options
  remain. No Steam region/settings/authentication changes. New region +connect
  refusal and lab/vanilla suffix mocks passed with all 26 fixtures in 118.263s.
- Clean candidate hash 13ab480023a662a43b6d9b6e907a0c0f12c63eb91f1172186edf46cebf9f92be.
  Fresh backup countercraft-cs2-view-probe-clean/20261008-092324.zip (105 files,
  445.6 MB) includes the retained crash. Installation state
  51e1351dd4fb459d87349d74dd857ec8. Steam launched exact PID 47100 with required
  flags plus region suffix; verified receipt and Computer Use observed actual
  local Dust2/bot first-person T spawn. No UI input/graphics changes were sent.
- 107 reports, 103 with candidates; D3D11 output 1680x1050. Main 4x D24S8
  resource format 44 / typed view 45 is texture_2d_multisample, raw
  firstLevel/levels/firstLayer/layers=0/1/0/0 -> normalized 0/1/0/1,
  canonicalBaseDsv=true, nonBaseViewDraws=0, positive draw range 10..2111.
  Single-sample screen-size D24S8 and 4352x5248 D16 plus smaller D24S8 also
  normalize correctly. Every parsed per-view draw/element/indirect/clear sum
  matches candidate totals. Arrays/nonzero subresources/default whole ranges
  are synthetic-only cases. Camera identity and selection remain false.
- Final missed/deferred/overflow=159/0/0; knownLossFree=false. No performance
  baseline or attribution of lost events; cannot compare directly to previous
  session's 20. MC not running: zero received/uploads, no resource failures,
  socket timeout expected. This validates metadata, not MC preview or fusion.
- Gracefully closed exact PID 47100. ReShade logged runtime destruction,
  add-on unregister and Finished exiting; last timed report still had runtime=1
  before destruction (no separate zero-count sample). D3D11 reference warning
  1353 remains un-attributed. Restored both files by the exact state; no CS2,
  dxgi.dll or game ReShade.ini remains. universal-modder diff against the clean
  backup: no added/removed/changed files. Crash/private logs remain ignored.
- Final staged-source universal-modder publish check: 74 files, zero failures,
  one known absolute-user-path warning in this journal. The exact recovery paths
  are intentional provenance; no game binaries, imagery, minidumps, raw logs or
  dependency files are staged. Original first backup comparison has only the
  newly created first-attempt crash dump, with no removed/changed files.
- Next: actual Steam -insecure Dust2 report/restore oracle, then verified host
  camera/projection/viewport, same-frame MC alignment, real occlusion, gameplay
  input/collision/interaction, resource lifecycle and performance regression.
  Working estimate is 5-8 further stage acceptances to an offline playable
  prototype, conditional on camera/depth integration; a complete all-feature
  Minecraft port has no defensible fixed-round guarantee yet.

## Steam launch and actual host-depth observation after reinstall (2026-10-08)

- User confirmed CS2 download finished and requested continuation of the approved
  offline bridge work. Running Steam is now D:\steam1\Steam.exe; its library
  manifest resolves CS2 to D:\steam1\steamapps\common\Counter-Strike Global
  Offensive, build 25738536, StateFlags=4, downloaded bytes complete. Old D:\steam
  install is absent. No CS2/Java process, existing dxgi.dll or game ReShade.ini.
- Regenerated the a36b713 host-probe candidate plan for the exact new installation.
  New pre-loader snapshot: countercraft-cs2-reinstall-before-probe/
  20261008-080527.zip (101 files/443.0 MB). Do not reuse the old install snapshot.
  Initial direct launch exited with Steam IPC code 12; Steam replacement children
  had only -steam and were not adopted as lab successes. The physical-Escape
  interruption was respected, and owned processes/files were closed/restored.
- User confirmed normal CS2 startup, requested retry, then provided Launcher
  Error #720: local Steam client connection failed when cs2.exe was launched
  directly. Adding -steam did not solve it. No ownership/security checks were
  changed. User's steam_appid.txt and two prior crash dumps were already present
  before the new test; preserve them. New current-state backup:
  countercraft-cs2-retry-clean/20261008-081854.zip (104 files/444.3 MB).
- Verified ReShade v6.8.0 upstream dll_main.cpp get_base_path and ini_file.cpp
  global_config via the GitHub contents API; raw-file HTTPS failed on this machine.
  Official [INSTALL] BasePath takes precedence and selects the isolated config/log
  directory. Changed the route to Steam -applaunch 730, with a temporary small
  ReShade.ini bootstrap in addition to pinned dxgi.dll. Both exact paths/hashes
  are recorded; exclusive creation, conflict refusal and all-files-before-delete
  validation retain external changes. Old one-file states remain restorable.
  Original PCL/OptiFine and Steam authentication/settings are untouched.
- Steam client PID 18224 created CS2 PID 32048 with exact executable/new path and
  -steam -insecure -countercraft-lab -countercraft-preview -console +sv_lan 1
  +map de_dust2 -countercraft-host-probe. D3D11 API 45056, RTX 4060 Laptop driver
  617.14, 1920x1080; effect compiled. Computer Use inspected local Dust2 team
  selection, bot introduction and first-person T spawn. A team click intended as
  CT resulted in T selection; followed actual state and made no gameplay claim.
- 143 JSON reports, 137 with candidates. Screen-size candidate 1 is four-sample
  D24S8 and has 13..1297 recorded direct/indirect draw commands in observed active
  intervals. Single-sample screen-size D24S8 and 4352x5248 D16, 960x540/480x270
  D24S8 also appear. Last clear 1 is evidence only, not proof of depth convention.
  Final missed/deferred/overflow=20/0/0; knownLossFree=false. No full coverage or
  performance baseline claim. AutoSelected and cameraDepthVerified remain false.
- Existing base-view classification does not normalize D3D11 default level/layer
  counts or multisample view shape. Its nonBaseViewDraws is not proof of actual
  non-base subresource use; document and verify actual descriptions next. No
  host pixels, constant-buffer content or scene-camera data were read. MC was not
  running, so receiver socket timeout is expected; received/uploads=0 and
  resourceFailures=0. This test verifies metadata callbacks, not world fusion.
- Gracefully closed exact owned PID 32048. Runtime count became zero, add-on
  unregistered, ReShade finished exiting. Reference-count warning 1782 recurred;
  previous loader-only baseline still does not identify its cause or exclude a
  leak. Restored both files using state steamtest-afa810002973449891e0f7acc5fc3b69.
  No CS2 process, dxgi.dll or game ReShade.ini remains. Backup comparison against
  the 08:18 current-state ZIP: no added, removed or changed files.
- Launch scripts now use Steam, validate its exact child path/parent/arguments,
  reject unexpected launch options and record private lab receipts. A verified
  process receipt is not map/runtime success. Vanilla offline launcher also uses
  Steam and refuses residual lab files. Defaults still require -Cs2Root after a
  move/reinstall. No raw console identifiers, imagery, game files or binaries ship.
- Seven native CTest suites passed (8.87s). Twenty workflow fixtures passed
  (110.336s), then six mock-launch cases passed after adding the two vanilla
  cases (25.507s): 22 distinct checks. The first fixture run correctly rejected
  candidates nested inside its fake game root; separated the fixture roots, then
  all checks passed. Mocks shadow all process operations and never launch games.
  Java/native source unchanged. Eighteen Python regressions passed (0.761s), all
  ten PowerShell scripts parsed, Python fixture syntax and whitespace checks
  passed. Preparation rerun after listing both targets passed and confirmed
  preview-only BasePath with no game writes. Staged-source publication check is
  on the 73-file source-only index snapshot found zero failures and one known
  absolute-user-path warning in this journal. Recovery provenance is intentional;
  raw logs, SDK/loader binaries, game assets and saves are excluded. Game comparison
  used the actual win64 install. Source tree is ready for the requested checkpoint
  commit/push; next stage is view normalization and verified depth/camera identity.

## Host depth reconnaissance without CS2 (2026-10-08)

- User reported accidentally deleting CS2 and is downloading it again. Continue
  only work that needs no CS2 launch; do not install a loader or change/download
  game files. Starting from pushed checkpoint d1be2d9.
- Route: bounded read-only ReShade depth-resource observation plus independently
  validated D3D projection-depth math. Resource dimensions/draw counts/clears are
  candidate evidence, never automatic camera/depth selection. No host pixel or
  constant-buffer readback, COM ownership, retail offsets or memory scans.
- Keep observation opt-in under the existing exact offline flags. ReShade event
  signatures and native D3D11 context lifetime are read from pinned 6.8.0 headers.
  Test ordinary/reversed finite/infinite projection and known GPU geometry in the
  standalone lab, then verify the new callbacks in actual CS2 after reinstall.
- Implemented 128-resource/64-binding/8-device fixed metadata storage, per-lifetime
  IDs, per-device effect intervals, no-frame/effect exclusion, saturated counters
  and eight-candidate report bounds. New host_probe callbacks retain no COM object,
  skip native deferred contexts and always return false for command interception.
  Render callbacks use try_lock; reporter sorts/encodes the copy off-thread.
  Missed/deferred/overflow evidence is explicit. IDs never authorize resource use.
- Added ProjectionDepth with an explicit D3D [0,1], column-major, independent
  perspective contract. Supports ordinary/reversed finite/infinite Z and both eye
  handedness conventions; canonical-layout checks refuse invalid matrices. This
  does not discover or identify CS2 camera constants. Standalone compositor now
  inverts projection coefficients and applies explicit host-to-guest unit scale.
- Full native build passed without MSVC warnings/errors. Seven CTest suites pass
  (8.14s): existing protocol/cube/guard/socket suites, six projection groups, nine
  inventory replay groups and hardware projection GPU tests (48 cases). GPU tests
  use synthetic host/guest planes at two resolutions and unit scales 0.5/1/32,
  including sky/invalid depth/no-frame restoration; cs2Integrated remains false.
- Ten PowerShell workflow fixtures passed (46.459s): existing exact install/
  restore refusal checks plus host-probe opt-in preview, restored-state refusal,
  map-argument refusal and preparation/missing-game path isolation. A first run
  exposed a GBK decode error in the test subprocess; explicit UTF-8 decoding fixed
  the fixture, and the final run had no background-thread errors. Eighteen Python
  regressions passed (0.780s); Java/effect code was unchanged.
- Prepared the actual new .local/cs2-host-probe-candidate with -AllowMissingGame.
  Plan records GameExecutablePresent=false, GameFilesWritten=false and the
  opt-in argument. No CS2 launch, loader install or download-directory mutation.
  Updated build/preparation docs and docs/host-depth-probe.md separate synthetic
  validation from upcoming actual callback/scene camera/depth verification.
- All ten PowerShell scripts parsed, the fixture passed Python syntax validation
  and staged whitespace checks passed. universal-modder publish check on the
  73-file source-only index snapshot found zero failures and one existing journal
  absolute-user-path warning. No binaries, SDK headers, game imagery, saves or
  raw logs are staged. Game-content comparison was omitted while CS2 is absent/
  reinstalling; the staged source inventory was checked explicitly for generated
  binaries and assets. No CS2 process was running at the final check.

## Offline CS2 upload validation (2026-10-07)

- User specifically approved installing/testing/restoring the offline loader.
  Continued from pushed `a039711`. Downloaded official full-add-on ReShade 6.8.0
  with the upstream example's normal request headers. Read the appended ZIP as
  data; did not execute setup. Product/version and x64 PE verified, content hashes
  pinned in scripts/fetch-reshade-runtime.ps1. Installer certificate chain is not
  trusted locally (UnknownError); DLL is unsigned. Provenance is official HTTPS
  plus exact pinned content, not a Windows certificate trust claim. No certificate,
  registry, anti-cheat or security settings were changed.
- universal-modder backup before writes: 105 files/443 MB, snapshot
  `C:\Users\23182\.universal-modder\backups\countercraft-cs2-before-loader\20261007-163210.zip`.
  Added hash-checked, conflict-refusing install/restore scripts. Actual installation
  creates only game/bin/win64/dxgi.dll. Verified ReShade's official per-process
  RESHADE_BASE_PATH_OVERRIDE in pinned dll_main.cpp; config, log, effects and caches
  stay under ignored .local/cs2-lab-candidate. No game-folder ReShade.ini was created.
- Started the installed Steam client; its normal session was already authenticated.
  Launched exact owned CS2 PID 44468 with -insecure, lab/preview flags, +sv_lan 1,
  +map de_dust2. Actual command line and loaded proxy/system DXGI/render DX11
  modules checked. No third-party-software exception, injection workaround or
  official-server connection was needed.
- ReShade 6.8.0.2155 registered the actual add-on and D3D11 runtime. Probe FX
  compiled successfully. Inspected the real game: MC's live terrain/sky appeared
  inside the diagnostic inset over local Dust2/bots. Own-texture upload, callback
  and shader proof only; hostCameraDepthVerified remains false.
- Main session received 6660 frames and uploaded 5367, resourceFailures=0,
  guest size 1280x720. Startup received frames before effects presented, so these
  totals are not a steady-state FPS benchmark. Paused real MC using its menu;
  stream closed with 'World changed, paused or stale', zero leased buffers. Native
  mailbox disconnected and the inset disappeared while CS2 still responded.
  This closes the actual pause/GL cleanup check; resize/world switch remain open.
- Gracefully closed only the confirmed offline PID. Runtime count became zero;
  add-on unregistered and ReShade finished exiting. A D3D11 reference-count warning
  (1478) appeared during teardown. A loader-only control session (no add-on or
  effects) also exited normally with the same category warning (1213). This does
  not exclude an add-on leak or identify the warning's cause; detailed lifetime
  attribution remains open.
- Updated launch uses PowerShell 7.4+ Start-Process -Environment and private
  stdout/stderr files. Actual third session PID 42564 confirmed exact offline
  arguments, the isolated base path, FX compile and explicit paused-MC refusal
  with zero received/uploaded frames. Graceful close unregistered the add-on and finished
  ReShade exit; that session also had a reference-count warning (2142).
- Restored the exact hash-matched new loader with its recorded state; state is
  Restored. No CS2 processes remain, dxgi.dll and game-folder ReShade.ini absent.
  universal-modder backup diff against the 16:32 snapshot found no added, removed
  or changed files in win64. The original PCL/OptiFine instance remains untouched.
- Five isolated install/restore fixtures passed (20.550s): no-write preview,
  install/conflict/restore, modified-loader refusal, wrong-backup-source refusal
  and manipulated-state refusal. All four native CTest suites and 18 existing
  Python tests passed. Java was unchanged (16 tests at the previous checkpoint).
  Updated docs separate the verified diagnostic preview from future camera/depth
  integration and record the one-file temporary install and restore workflow.
- All ten PowerShell scripts parsed on PowerShell 7.6.5; the new Python fixture
  passed syntax validation and the staged diff passed whitespace checks.
  universal-modder publish check on the 63-file source-only index snapshot found
  zero failures and one absolute-user-path warning in this journal. The recovery
  path is deliberately retained; no loader/SDK binaries, game files, screenshots,
  saves or raw console logs are staged.
- Local evidence (not committed): cs2-preview-first.jpg, cs2-fallback-first.jpg,
  cs2-probe-session.log, cs2-pause-mc-status.jsonl, cs2-install-result.json and loader
  provenance/state. The desktop was unlocked; GUI activation was necessary to
  avoid occluded fullscreen captures. User-input detection was handled by fresh
  observation; no auth dialog, user settings or long gameplay sequence was driven.

## Native receiver and client compositor lab (2026-10-07)

- Continuing from pushed checkpoint `3124e17`. Route: Windows x64 native loopback
  receiver, isolated D3D11 depth-composition oracle, then a ReShade add-on candidate
  for offline CS2. No CS2 loader/game files are written by the build/test tools.
- Verified MSVC 14.50.35717, Windows SDK 10.0.26100.0, CMake/Ninja/fxc are available.
  CS2 install contains rendersystemdx11.dll; this alone is not a runtime API check.
- Queried ReShade tags: v6.8.0 = 18deaa52de0c425a78b329e9cb3c497281cd00ec.
  Downloaded its eight API headers and nlohmann/json 3.12.0 into ignored .local;
  reproducible fetch script pins SHA256. No runtime loader downloaded or installed.
- Implemented native header/metadata/CRC/depth validation, network-worker-only IO,
  heartbeat/clock calibration and a latest-frame mailbox. The isolated D3D11
  shader compares finite guest OpenGL depth with known reversed host perspective
  depth, flips GL rows and preserves host colour for missing guest frames/sky.
- First MSVC build and protocol/GPU oracle passed. The GPU oracle used hardware
  D3D11, with real triangle cube/depth at 80x60 and 128x96. Five groups verified
  near/far occlusion, row flip, guest sky and exact host-only restoration. Inspected
  `.local/native-depth-oracle.bmp`.
- Resumed the separate Minecraft world via fresh Computer Use window selection
  after an initial minimized-window error. Native 10s test received/uploaded 180
  frames at 1280x720, zero replacements/stale; age P95 112.3306ms, upload P95
  4.6551ms, clock uncertainty 0.8508ms. Inspected `.local/native-live.bmp`: real
  terrain/rain and a known red eye-space cube, with sky preserving host colour.
  No CS2 camera, scene depth or world alignment was used.
- Nine independent Python/native socket tests (13 scenarios) found heartbeat
  starvation during a no-frame `select` wait. Moved heartbeat checks into the
  20ms wait loop. Epoch/control-loss cleanup then passed. A subsequent fixture
  failure was Windows' expected ConnectionAbortedError on cancellation; bounded
  header fragmentation plus explicit cancellation handling fixed the fixture.
- Added an offline ReShade upload candidate using verified upstream
  `bool AddonInit(HMODULE,HMODULE)` / `void AddonUninit(HMODULE,HMODULE)` signatures
  from pinned addon_manager.cpp. No heavy DllMain work or owning global destructor.
  The actual DLL rejects non-CS2 processes; exact `-insecure` and lab flags are
  required. D3D11-only resource upload, per-runtime textures, latest-frame fallback,
  worker-side telemetry and an optional diagnostic inset are implemented.
- Final native compile had no MSVC warnings/errors. All four CTest suites passed:
  seven protocol groups, five GPU groups, ten offline guard/DLL refusal groups,
  nine network integration tests. Existing 18 Python regressions passed. Java
  source was unchanged; its 16 tests were verified at the previous checkpoint.
- A later 15s hardware test received/uploaded 272 frames, zero stale/replacements;
  age P95 165.3964ms, upload P95 4.102ms. Final MC status showed stream stopped,
  camera inactive and zero leased buffers. The attempted UI pause did not execute:
  capture became black, activation failed, and fresh recovery showed a locked
  desktop. Stopped UI actions. Real pause/resize/world-switch GL checks stay open.
- Prepared `.local/cs2-lab-candidate/install-plan.json` and compiled add-on/config/
  effect entirely outside CS2. Both proposed game targets (dxgi.dll, ReShade.ini)
  are currently absent. No game files written. The plan records exact arguments,
  target-state backup and hash-checked restore requirements. Loader download and
  install are separate: the official ReShade homepage returned HTTP 403 to the
  read-only retrieval; no bypass or loader download was attempted.
- CS2 callback/resource compatibility, FX compilation, actual camera/depth and
  gameplay remain unverified. The skill requires specific approval before a
  game-folder loader install; prior plugin/dependency and commit/push approvals
  do not constitute that new install approval.
- All PowerShell scripts parsed and staged diff passed whitespace checks.
  universal-modder publish check on the 59-file source-only index snapshot found
  zero failures and one existing absolute-user-path warning in this journal.
  No binaries, headers, screenshots, saves or generated game data are staged.

## Bounded world frame transport (2026-10-07)

- Starting from pushed checkpoint `7577133`, added a three-PBO/fence readback ring
  and a loopback binary TCP feed. This deliberately chooses a portable CPU-copy
  prototype before GPU sharing; it does not install a CS2 renderer/loader.
- The feed has two leased payload buffers and a replaceable pending frame.
  Nonblocking socket writes have a 500 ms deadline. Metadata is frozen at issue;
  session UUID, epoch, sequence, exact lengths and CRC32 protect the receiver.
- Added an independent Python latest-frame receiver, clock calibration and an
  actual-camera stream verifier. The binary layout is in docs/frame-stream.md.
- Final build succeeded; 18 Python and 16 Java tests passed, including fragmented and
  corrupt frames, stale/restarted sessions, bounded buffers and non-reading TCP.
- Gracefully closed the prior lab client and confirmed all dimensions saved before
  building. Restarted the isolated lab using null audio/background flags.
- Found a validation-tool bottleneck: scanning a full depth array/encoding PNG
  during live reception could starve Python TCP drain and trigger the intended
  500ms write deadline. Fixed the verifier to retain four bounded diagnostic
  frames and do deep scans/PNG encoding only after stopping the stream.
- Final real 1280x720 verification: 184 received/consumed frames in 10.005s with
  a 20 FPS cap; zero sender drops, receiver replacements or stale frames. Clock
  uncertainty 0.248ms; estimated age median/P95/max 34.734/42.824/60.157ms;
  readback P95 18.562ms. Both controlled camera/FOV/projection poses, distinct
  colours, real terrain depth and vanilla view after release passed. PNGs inspected.
- Slow-consumer test: 30 FPS cap, 280 frames in 10.103s; foreground consumed 49,
  replaced 230, no sender drops/stale frames, estimated age P95 64.470ms.
- A genuinely non-reading receiver timed out in 0.571s with the explicit 500ms
  write-deadline reason and zero leases. Restart delivered sequence 1 under a
  different UUID. Closing control stopped the stream and released camera control.
- Existing on-demand camera/capture verifier passed again on the final build.
  Final evidence is ignored under .local/: build-stream-final.log,
  stream-verification-final.jsonl, stream-benchmark-final.jsonl,
  stream-slow-final.jsonl, stream-hygiene.jsonl, capture-regression-final.jsonl,
  client-stream-final.log and stream-final*.png. Generated game data is untracked.
- Real pause/resize/world-switch GL validation is still open. Window activation
  failed twice; the lifetime test therefore timed out without those actions.
  Normal close of the confirmed lab PID succeeded and all dimensions saved before
  the final build. Protocol menu/stale-world tests are not a substitute for GL tests.
- Assessed the next CS2 slice in cs2/README.md: native receiver, offline client
  resource upload/cube-depth oracle, then actual host camera/depth integration.
  No CS2 loader/client files installed, no shared GPU texture or playable port.
- universal-modder publish check ran on a 39-file source-only staging copy:
  zero failures, one warning for an already-recorded absolute user/plugin path
  in this journal. No game files, caches, frames, saves or credentials are staged.

## Render-side camera and world frames (2026-10-07)

- Build/setup checkpoint `291655c` was committed and pushed to `origin/main`.
- Added on-demand `capture` to the localhost protocol. Networking schedules a
  future; Minecraft APIs/GL are used only on the render thread. File encoding and
  writes happen on a separate worker. Capture has a three-second response
  timeout, one pending job and a 4M-pixel limit. A cancelled unclaimed job frees
  the slot. PNG/depth are written first; the manifest is published by rename.
- Inspected cached Minecraft 1.20.1 method signatures/bytecode outside tracked
  sources. The new injection is immediately after `WorldRenderer.render` in
  `GameRenderer.renderWorld`, before the vanilla depth clear for the hand/HUD.
  No decompiled source, game binaries or generated images are committed.
- Export metadata explicitly records non-reversed OpenGL depth, dimensions,
  top-to-bottom row order, float32 little-endian samples, near/far, actual camera
  pose/FOV, column-major projection/view rotation and world epoch. Sky/fog remain.
- Added a Python bundle reader, depth conversion and `minecraft_host --verify`.
  It compares actual render-side poses/FOV/projection with two requests, checks
  geometry in depth, different colour frames, and camera release.
- Real verification passed in `CounterCraft Lab`: 1280x720; requested frames 3
  and 7 matched positions/rotations; yaw/pitch/FOV were (0,20,70) and (90,30,55).
  Depth contained 785,434 and 921,112 geometry pixels with near/far 0.05/768.
  Release returned to vanilla requestedFrame -1, yaw 100.614868, pitch 24.711832,
  FOV 70. Exported images were visually inspected: different terrain views,
  correctly oriented and without the hand, crosshair or inventory.
- Evidence: `.local/camera-verification.jsonl` and ignored capture bundles
  `868029d0-ddca-4de5-9343-8873701bd43c` and
  `92f011da-0ff2-40ab-814d-fed197af312d` under `minecraft/run/countercraft/captures`.
- Added `-World` for Minecraft 1.20.1's `--quickPlaySingleplayer` into existing
  isolated lab saves. Optional `-Background` disables only automatic focus-loss
  pausing in opted-in single-player, without persisting user options. Explicit
  pauses still stop the bridge. Null audio successfully initialized `No Output`.
- Final build passed (`.local/build-frame-latest.log`): eight Java tests, zero
  failures/errors, including timeout cancellation/reconnect. Twelve Python tests
  passed, and all repository PowerShell scripts parsed successfully.
- Re-launched the final build with `-NullAudio -World 'CounterCraft Lab'
  -Background`. It loaded normally; the real render-side verification passed
  again while the lab was in the background. Final evidence is
  `.local/camera-verification-final.jsonl` and bundles
  `d750f9c4-93dd-45ed-a954-5b205c86d257` / `74fe6db0-26e5-4a17-92ac-21ff6f79f0ea`.
  Both final images were inspected. A further render-side watchdog check passed:
  after 650 ms without a fresh camera request, capture returned requestedFrame
  -1 and the vanilla view. Evidence is `.local/watchdog-verification.jsonl`.
- The development client remains open in the disposable lab world (null audio).
  All controlled cameras were released or expired. No shared-memory feed or
  CS2 game-folder installation was performed in this milestone.
- This completes a guest rendering diagnostic slice. It is not a CS2 port:
  realtime transport, CS2 camera/render/depth integration, input forwarding,
  simulation/player movement and interactions remain. Do not call these implemented.

## Earlier build/setup checkpoint (2026-10-07)

This checkpoint supersedes the earlier download/installation blockers below.
Those sections are retained as the investigation history.

- Confirmed `minecraft/build/libs/countercraft-minecraft-0.1.0.jar` exists
  (15,511 bytes) and includes the mod manifest, refmap and three Mixins.
  `.local/build-latest.log` reports `BUILD SUCCESSFUL`; the Gradle test XML
  reports five tests, zero failures and zero errors.
- The development client loaded CounterCraft 0.1.0, Minecraft 1.20.1 and Fabric
  Loader 0.19.5. The Python host received `ready` and `status` over real localhost
  TCP on port 37122. The status was `offline: false` while at the menu, so no
  in-world camera claim is made yet.
- Fixed the build helper to forward process HTTP(S)_PROXY host/port to Java,
  and added a `runClient` task selector. Credentials are not accepted in proxy
  URLs; process environment changes are restored in `finally`.
- Codex initially no longer listed the plugin even though its cache existed.
  Re-registered the inspected upstream checkout as a local marketplace and
  reinstalled `universal-modder@universal-modder` 0.2.0. A fresh CLI list confirms
  `installed: true` and `enabled: true`. The installation helper now checks
  existing installation/marketplace state and verifies the requested plugin;
  rerunning it on the installed plugin succeeds without reinstalling.
- Re-ran all eight Python bridge tests: passed, including socket integration.
- The first development client exited nonzero (-805306369). A restarted client
  also became unresponsive during the startup screen. Its thread dump showed
  the render thread inside OpenAL `SOFTHRTF.alcResetDeviceSOFT`, consistent with
  repeated device reset errors. This is evidence of an audio initialization
  stall, not a proven Mixin failure. Logs/thread dump remain in ignored `.local`.
- Stopped only the confirmed stalled lab client PID 38392; no world was open.
  Added the optional process-local `-NullAudio` switch and restarted the same
  isolated development client with OpenAL's null output backend for validation.
- The null-audio client responded normally and the owner opened the new creative
  world `CounterCraft Lab` under `minecraft/run/saves`. Status returned
  `offline: true`, epoch 1. The five-second demo received all 100 consecutive
  camera acknowledgements and a `released` reply. The baseline game screenshot
  showed the lab terrain. During-demo capture was obscured by another desktop
  window, so pixel-level proof of the override remains pending. Next add a
  render-side pose observation and a game framebuffer capture oracle rather
  than equating accepted network requests with rendered output.
- Original PCL/OptiFine profiles and saves were not modified. CS2 has not been
  hooked, launched into official servers, or supplied with a renderer adapter.

## Requested outcome

Run real Minecraft inside Counter-Strike 2, preserving Minecraft systems rather
than making a block-building imitation. On 2026-10-06 the user explicitly accepted
an offline version, replacing the earlier official-matchmaking requirement.

## Verified environment

- This repository initially contains README.md and LICENSE only; no mod implementation.
- Plugin source: https://github.com/rehan-remade/universal-modder
- Retrieved plugin manifest version: 0.2.0.
- Marketplace registration succeeded at
  `C:\Users\23182\.codex\.tmp\marketplaces\universal-modder`.
- Plugin installation did NOT complete. Subsequent installation attempts encountered
  an unavailable Git proxy (`127.0.0.1:7890`) and automatic approval timeouts.
  A final `codex plugin list --json` did not list universal-modder as installed.
- Steam manifest reports CS2 build 25738536 under
  `D:\steam\steamapps\common\Counter-Strike Global Offensive`.
- User specifies Minecraft instance root `D:\pcl2\Release 2.8.3` and
  version `1.20.1-OptiFine_I6`. Compatibility with a Fabric bridge is not verified.

## Research and limitations

The upstream mashup-mods skill describes a passthrough architecture: Minecraft
runs its own simulation, exchanges camera/input/events over local IPC, and exports
colour/depth for composition in the host game. The repository includes a GTA V
example and a Portal 2 field note. These are NOT a completed CS2 adapter.

Portal 2 uses Source 1/D3D9/32-bit interfaces; its hooks are not directly reusable
for CS2's Source 2 runtime. The documented Minecraft examples target 26.3/Fabric,
not the user's 1.20.1/OptiFine instance. No promise of OptiFine compatibility is made.

The upstream mod-any-game skill explicitly restricts this workflow to offline
play or user-controlled servers. Its Source playbook specifies local CS2 testing
with `-insecure`. No official-matchmaking-compatible full Minecraft embedding route
was established. Do not present the offline approach as meeting that requirement.

## Next decisions

1. Complete and verify the Codex plugin installation.
2. Offline scope is now accepted. Select an isolated Minecraft mod-loader profile, inspect current
   CS2 extension APIs, and implement a camera/cube/depth test before expanding.

No game files, launcher profiles, saves, or client DLLs were modified. No game
launch, in-game test, rendering bridge, or complete port has been performed.

## Offline continuation

- Read the upstream game-recon skill and ran its `python -m um scan` against CS2.
  It reported Source 2 and no installed mod loaders. The scan did not detect VAC
  files; that is not evidence of an unprotected client.
- Verified CS2 executable and engine2.dll exist. The requested MC JSON declares
  `net.minecraft.launchwrapper.Launch`; this is not a Fabric profile.
- Implemented a dependency-free diagnostic bridge: JSON Lines handshake,
  monotonically increasing camera frames, loopback binding, bounded reads, timeout,
  error replies, and MC/Source coordinate conversion. This does not connect games.
- Added read-only preflight and a preview-first offline vanilla launch script.
- Plugin install requests again timed out in automatic approval. No successful
  install was confirmed. A reviewable retry script is under scripts/.
- Fabric dependency metadata download was denied by the network sandbox
  (WinError 10013). Gradle cache contains no Fabric Loom/Yarn build dependencies.
- Initial tests: three geometry/validation tests passed. Two loopback integration
  tests were denied by the same sandbox (WinError 10013); elevated run requested.
- User explicitly authorized installation and dependency downloads again. Both
  renewed commands were rejected because automatic approval did not finish before
  its deadline. The elevated integration-test command had the same result.
  These actions did not run; a normal local terminal is needed to unblock them.
- Added stream-based tests for the actual diagnostic handler, including malformed
  JSON, size limits, handshake order, acknowledgements and duplicate frames. These
  do not replace the still-unverified socket integration tests.
- Offline launch was previewed only; preflight confirmed both executables/profile
  and the absence of Metamod/CounterStrikeSharp directories. No game was started.
- Stage validation: six geometry/validation/stream-handler tests passed, Python
  compilation succeeded, and all PowerShell scripts parsed successfully. Socket
  integration tests and actual game integration remain unverified.
- The user requests a commit and push after each completed milestone. This is
  recorded in CONTRIBUTING.md. The first milestone is diagnostic bridge groundwork,
  not a playable Minecraft-to-CS2 port.

## Execution restored and Minecraft camera lab (2026-10-06)

- `scripts/install-plugin.ps1` completed successfully. Codex reported
  `universal-modder@universal-modder` 0.2.0 installed and enabled.
- All eight Python tests passed with loopback access, including the two socket tests.
  PowerShell execution policy alone does not grant the sandbox network access;
  the successful runs used the separately approved execution path.
- Added a Fabric 1.20.1 camera receiver and a Python test host. This is render-only
  camera control, not full game simulation integration or CS2 composition.
- Used pinned Loom 1.10.5, Yarn 1.20.1+build.10, Loader 0.19.5, Java 17 bytecode.
  Existing Gradle 8.12.1 was incomplete (missing Kotlin compiler jar). Switched to
  Gradle 8.14 with JDK 21 and a project-local cache to isolate build state.
- New Java tests cover expiry, world switch, invalid poses, fragmented TCP,
  acknowledgement, release, duplicate frames, reconnect and read bounds.
- All five Java core tests passed, including real loopback sockets, using local
  JUnit 5.11.4 and Gson 2.10.1 with `javac --release 17`. This verifies the network
  and state classes independently of Minecraft; it does not verify Mixin injection.
- Full Gradle builds repeatedly stalled downloading
  `https://maven.fabricmc.net/net/fabricmc/mercury/0.4.2/mercury-0.4.2.jar`.
  The stalled builds were interrupted. Separate bounded HEAD/download diagnostics
  did not execute because automatic approval timed out. Do not infer a server outage
  from these unexecuted diagnostics. No installable jar exists yet.
- No game launch, input automation, loader installation into a game directory,
  profile change, or save modification took place in this stage.

## Sources

- https://github.com/rehan-remade/universal-modder
- https://github.com/rehan-remade/universal-modder/blob/main/skills/mashup-mods/SKILL.md
- https://github.com/rehan-remade/universal-modder/blob/main/skills/mod-any-game/SKILL.md
- https://github.com/rehan-remade/universal-modder/blob/main/knowledge/games/portal-2/portalcraft-minecraft-inside-portal-2.md

## Raw MSAA evidence capture (2026-10-08)

- Added DepthSampler: owned typeless depth copy, raw per-sample min/max RG32F,
  single/MSAA shader paths, CS/class-instance/SRV0/UAV0 restoration. Hardware
  debug-layer oracle passed 32 typed/typeless D16/D24S8/D32/D32S8 x 1/2/4/8
  sample cases without debug warnings. A foreign D3D11 device is rejected.
- Added three-slot asynchronous depth/VS/PS readback. Production uses only
  EVENT/GetData(DONOTFLUSH) and Map(DO_NOT_WAIT), no Flush or GPU wait. GPU oracle
  verifies original depth/constants after host mutation/release, compact row pitch,
  full-slot refusal, and D3D11.1 partial binding ranges. CPU bytes only go to disk.
- Opt-in -DepthCapture implies HostProbe. Per add-on lifetime: at most 18 captures,
  every 240 effect intervals at candidate draw 64/256/512, explicitly BEFORE that
  draw. This is partial channel evidence, not a final world-depth frame or verified
  camera. Larger-than-64KiB constants are explicitly skipped. All private data
  stays in the ignored candidate captures tree.
- First Steam launch waited about 48 seconds for shader/depot checks, beyond the
  old 40-second script deadline. The exact late child and arguments were verified
  and receipt annotated; future launch deadline defaults to 120 seconds (40-180).
- Initial live capture rejected ReShade's original/proxy GetDevice pointer mismatch.
  Public unique GUID device-private-data identity now validates the same underlying
  object without accepting arbitrary same-adapter devices or retaining proxy refs.
  The owned GUID is cleared on sampler destruction. Three failures stop new capture.
- A first successful export using the observer's shared background writer lost
  54,876 metadata events. That run is NOT loss-free. Separated bounded CPU disk
  writing into its own thread and report-error lock from readback queue locking.
- Final revised binary: Steam child PID 9412, exact offline args, local Dust2.
  54 reports across startup/render lifetimes. 18 queued/written, GPU pending 0,
  failures 0, discarded 0, two explicit busy skips. Maximum diagnostic callback
  6509us (includes allocation/CPU copy; not continuous bridge/FPS performance).
  Render lifecycle processed 16,976,442 events, missed/deferred/overflow all 0,
  peak backlog 5044/16384, max background drain 237us. Final runtimes/devices/
  pending all 0. Hardware captures: 1680x1050, typeless R24G8 + D24S8, 4 samples;
  viewport [0,0.95], LESS_EQUAL. Depth previews show road/walls/car geometry.
- Independent writer previous run also exported all 18 with no capture failures
  and zero defined observer gaps during observation. Native teardown still logs
  an unexplained reference count warning (final 1656); no no-leak claim.
- Ten CTest suites, 18 Python regressions, 27 mocked loader/Steam fixtures pass.
  Existing Java code is unchanged; its previous 16 Gradle results are not reruns.
- Backup: C:/Users/23182/.universal-modder/backups/cs2-depth-capture-20261008/
  20261008-124838.zip. Both exact installation states were restored, and the game
  win64 directory diff is added/removed/changed empty. Original PCL remains untouched.
- Next: range-aware projection inversion, mathematical/scene verification of
  projection/view/pose from public binding copies, then known in-world cube.
  cameraDepthVerified/autoSelected remain false. Complete port is not achieved.

## Viewport inversion and mathematical camera evidence (2026-10-08)

- ProjectionDepth now folds min/max viewport depth into inverse coefficients,
  preserves clip lens/planes/handedness, and replaces rather than double-composes
  a changed range. Out-of-viewport values are rejected by CPU/preserve host on GPU.
  Only caller-confirmed with_clear_value maps an exact raw clear to background;
  neither viewport nor matrix guesses a clear value. No CS2 automatic binding yet.
- Independent hardware GPU oracle now passes 192 combinations: eight perspective
  conventions, two resolutions, three unit scales and four viewport ranges
  [0,1]/[0,.95]/[.2,.8]/[.95,1]. Checks known host 2/9 versus guest 5 planes,
  explicit background, invalid depth, guest sky and no-frame original colour.
- Added dependency-free offline bridge.camera_evidence. Reads only bounded,
  actually bound VS constant-buffer ranges from captured files, rejects outside
  paths/size mismatches, explores 16-byte-aligned row/column layouts. No process
  handle, memory scanning, retail offsets, or automatic live camera selection.
- Requires a viewport-aspect-compatible canonical projection and an orthonormal
  affine view plus BOTH full P*V and camera-relative P*V(rotation only) elsewhere
  in the same public binding snapshot. Reports derived pose/lens and locations,
  deduplicates and bounds candidates. Math consistency is not a scene oracle.
- Analyzed 36 captures from the two independent-writer Dust2 sessions. Each has
  one mathematical set, normal-Z/right-handed, near 4, far approximately 10000,
  vertical FOV about 83.58 degrees, aspect 1.6. Static pose derivation suggests
  (-1272.887,-537.879,195.628), yaw 41.63/pitch 0 in candidate Source axes; not yet
  independently confirmed. Private full reports remain ignored; no retail data
  or captured binary files in source distribution. All scene/units/depth-verified
  and auto-selection flags remain false.
- Ten CTest suites pass (CPU projection now eight groups, GPU 192 cases), all
  26 Python tests pass including eight independent camera-evidence fixtures.
  Existing 27 loader/Steam fixtures and 16 Java tests are unchanged prior results.
- This stage changed arithmetic/standalone GPU and file analysis, not runtime
  host selection or gameplay. No additional game launch required for these claims.
  Next: explicitly trigger captures at controlled offline poses, verify angles,
  camera height/lens and depth silhouette, then a world cube and live relay.
