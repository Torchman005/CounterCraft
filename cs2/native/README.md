# Native receiver and D3D11 lab

This is a verified independent renderer plus a **ReShade upload/diagnostic inset
verified in actual offline CS2**. It is not a playable Minecraft port or verified
CS2 scene compositor. The original OptiFine instance is not used. The temporary
game-folder loader was restored after each test. The opt-in host-depth observer
produced real resource/draw/clear metadata in Steam-launched offline Dust2 on
2026-10-08. Camera/depth identity, full event coverage and overhead remain unverified.

## Build

Requires Windows x64, MSVC/CMake/Ninja/Windows SDK and Python 3. From the project
root (set `VcVars` to your VS installation):

```powershell
./scripts/fetch-native-deps.ps1
./scripts/build-native.ps1 -VcVars 'D:\itJinYu_toolkit\vs studio\VC\Auxiliary\Build\vcvars64.bat'
```

Headers are pinned to nlohmann/json 3.12.0 and ReShade v6.8.0 commit
`18deaa52de0c425a78b329e9cb3c497281cd00ec`, with SHA256 checks. The scripts do not
download/install a runtime loader or write game files. Outputs stay under `.local`.

The build script sets process-local VSLANG=1033 and UTF-8 output for reliable
MSVC `/showIncludes` parsing. Old localized dependency metadata triggers a fresh
configure and clean rebuild. It also verifies nonzero header dependencies for
inventory/add-on objects; `-Clean` requests a full rebuild explicitly. A stale
localized Ninja cache previously linked old/new HostProbe layouts and crashed
the first view-probe candidate before presentation; the clean candidate passed
the real-game session below.

CTest runs eight suites:

- Binary protocol: seven groups for layout, bounds, UUID, sequence, CRC, JSON,
  epoch, camera vectors and finite depth.
- Real D3D11 GPU: five groups at two resolutions. A triangle cube uses a real
  reversed-Z depth buffer, tested against a known guest plane. Covers near/far
  ordering, OpenGL row flip, sky preservation and exact no-frame pass-through.
- Offline guard: exact parsed flags/process name, forbidden flags and actual
  DLL load/init refusal/unload in a non-CS2 test process.
- Nine independent Python/native socket tests (13 scenarios): fragmented frames,
  distinct clock origins, CRC/depth rejection, partial packet, wrong session,
  duplicate sequence, stale/future frame, pause refusal, epoch/control loss,
  partial-read cancellation and mailbox expiry.
- Projection math: six groups covering hand-derived finite/infinite endpoints,
  normal/reversed depth, left/right eye space, jitter, FP32 and invalid inputs.
- Bounded depth inventory: 16 groups for resource lifetime/reuse, effect
  exclusion, resize, multiple devices, indirect/subresource counts, capacity and
  counter saturation, single-sample/MSAA/array DSV normalization, API default
  ranges, typed/typeless families, nonzero mip/layer rejection and bounded
  per-view draw/clear accounting. This replays synthetic events without any game.
- Projection GPU: hardware D3D11, eight projection modes at two resolutions and
  unit scales 0.5/1/32 (48 cases). Checks full-image near/far occlusion, invalid
  host depth, host/guest sky and exact no-frame pass-through. These are synthetic
  textures, not CS2 captures.
- Bounded depth observer: five groups for FIFO/ring wrap, lifecycle/effect replay,
  full queue refusal/recovery, simultaneous producers with exact payload/accounting
  checks, and report snapshots overlapping producer activity. All inventory work
  runs on the consumer; callbacks only publish fixed value metadata.

## Real Minecraft oracle

With the isolated Fabric client in an unpaused single-player world:

```powershell
.local/native-build/countercraft_bench.exe --live --seconds 10 --fps 20 --output .local/native-live.bmp
```

The worker receives/validates frames; the foreground thread uploads the latest
frame to a separate D3D11 device. The host cube is in known **eye space**, with
the guest projection lens. This does not verify world coordinates or camera
reprojection. No real CS2 depth or view matrix is used. `hardware` reports whether
the device is hardware or WARP. Generated screenshots contain game imagery and
must remain ignored.

2026-10-07 hardware run: 180 frames received/uploaded in 10 seconds at 1280x720,
zero replacements/stale frames; estimated age P95 112.33ms, upload P95 4.66ms,
clock uncertainty 0.85ms. A later 15-second run received/uploaded 272 frames,
age P95 165.40ms and upload P95 4.10ms. These are individual desktop measurements,
not a performance guarantee. Future optimization must address readback/transport
latency as well as upload. The earlier Python benchmark is a different workload.

## Upload candidate

`CounterCraftProbe.addon64` uses the pinned ReShade API. `AddonInit` checks the
actual executable basename and `CommandLineToArgvW` tokens before any ReShade or
network initialization. Both `-insecure` and `-countercraft-lab` are required;
`-secure`, `-vulkan` and `+connect` are refused. This is a development guard, not
a firewall or an online-compatible renderer. A ReShade loader still loads before
the add-on guard; restore/remove that loader before normal CS2 use.

On D3D11 effect runtimes the candidate uploads RGBA8 and R32_FLOAT to its own
textures and exposes `COUNTERCRAFT_COLOR`/`COUNTERCRAFT_DEPTH` semantics. It leaves
CS2 colour untouched by default. `-countercraft-preview` enables the optional
diagnostic inset and raw guest-depth strip. The inset is only a texture/callback
oracle, **not host-depth composition**. The FX file compiled successfully in
actual offline CS2 with ReShade 6.8.0. Unsupported runtimes and stale/disconnected frames leave
the inset inactive. A failed connection requires a new test session; no automatic
reconnect is implemented.

Render callbacks do not perform socket IO, CRC/depth scans, disk logging or thread
joins. Resources are per runtime and recreated for guest-size changes. Log JSON
comes from a reporting worker. Normal network/report thread cleanup occurs in
`AddonUninit`, which ReShade calls before `FreeLibrary`; `DllMain` does no work.
Forced process termination relies on OS reclamation, avoiding a loader-lock join.

## Actual offline CS2 result

2026-10-07, Steam build 25738536, NVIDIA RTX 4060 Laptop (driver 617.14): the
official ReShade 6.8.0 full-add-on loader registered the add-on and D3D11 runtime
(API enum 45056). The diagnostic effect compiled, and real MC terrain/sky was
visually inspected over local Dust2/bots. The session received 6660 frames and
uploaded 5367, with guest size 1280x720 and `resourceFailures=0`. Startup reception
preceded effect presentation, so this is not a steady-state FPS benchmark.

Pausing actual MC stopped the stream with `World changed, paused or stale`, left
zero leased buffers and removed the inset while CS2 remained responsive. Starting
a new session while MC was paused refused the stream without creating guest
textures. There is no automatic reconnect; resume MC and start a new test session.

Confirmed offline processes exited gracefully, runtimes reached zero and the
add-on unregistered. ReShade logged a D3D11 reference-count warning both with the
add-on and in a loader-only control session. This baseline does not exclude an
add-on leak or identify the warning's cause. Resize/world-switch and detailed
resource-lifetime checks remain open.

## Temporary loader workflow

Run from PowerShell 7.4+ with Python 3 and a completed native build. Close CS2,
review the plan and obtain approval for game-folder installation. This project's
local offline test and restoration were explicitly authorized. The preparation
command writes only outside the game directory. Pass the actual current install
path after a reinstall rather than relying on an older default:

```powershell
./scripts/fetch-reshade-runtime.ps1
$cs2Root = '<absolute current CS2 install directory>'
./scripts/prepare-cs2-lab.ps1 -Cs2Root $cs2Root
Get-Content .local/cs2-lab-candidate/install-plan.json
./scripts/manage-cs2-loader.ps1  # preview only
```

The fetch script downloads the official pinned 6.8.0 installer and reads only
`ReShade64.dll` from its appended ZIP; it never executes setup. Installer/DLL
SHA256, x64 PE and product/version are checked. The installer certificate chain
was untrusted locally and the DLL unsigned. This is HTTPS/content provenance,
not a claim of a Windows trusted signature; no trust/security settings change.

Before installation, create a universal-modder backup of the exact
`game/bin/win64` directory (`um backup create <directory> --name
countercraft-cs2-before-loader`). With the desktop plugin, run `python -m um`
from its checkout if `um` is not on PATH. Set `$snapshot` to the resulting ZIP:

```powershell
$snapshot = '<absolute path to completed backup ZIP>'
$installed = ./scripts/manage-cs2-loader.ps1 -Mode Install -SteamLaunch -Cs2Root $cs2Root -BackupSnapshot $snapshot
$installed.StateFile  # keep this exact path for restore
./scripts/launch-cs2-lab.ps1 -StateFile $installed.StateFile  # preview
./scripts/launch-cs2-lab.ps1 -StateFile $installed.StateFile -Launch
# Close that offline CS2 session normally before restoring:
./scripts/manage-cs2-loader.ps1 -Mode Restore -Cs2Root $cs2Root -StateFile $installed.StateFile
```

For another game location, pass the same `-Cs2Root` to preparation, install and
restore. Start the installed Steam client first; authentication is done by the
user. The launcher calls that running client's executable with `-applaunch 730`
and `-insecure -countercraft-lab -countercraft-preview -console +sv_lan 1 +map
de_dust2`; `-Map` accepts a local map name. It never starts `cs2.exe` directly.
Direct startup after this reinstall produced Launcher Error #720 even with
`-steam`. It records a private receipt and requires the exact game path, Steam
parent PID and fixed arguments; unexpected user launch options cause refusal.
`ProcessIdentityVerified` only confirms startup identity, not map or rendering
success. No Steam settings, authentication, app ID or ownership files are changed.
The exact Steam-appended `-perfectworld` region suffix is also accepted after
the fixed offline arguments; other unexpected options and `+connect` are refused.

With `-SteamLaunch`, only `game/bin/win64/dxgi.dll` and a small `ReShade.ini`
bootstrap are installed. The official ReShade 6.8.0 `[INSTALL] BasePath` redirects
to the isolated candidate; its full config, add-ons, effects, logs and caches stay
there. A running Steam client cannot inherit a newly invoked helper's environment,
so the previous process-local override route is not used. Existing loaders or
game-folder `ReShade.ini` cause refusal. Both files' hashes and target paths are
recorded. Install uses exclusive file creation and records state before writing;
a failed partial install stays `Prepared` for inspection. Restore removes only
the exact new hash-matched loader/bootstrap, retaining evidence. Both are validated
before removing either; a modified file or manipulated path preserves both for
inspection. Old one-file installation states remain restorable. Restore before
normal CS2 use.

The 2026-10-07 restore was verified against the universal-modder snapshot: no
added, removed or changed files. The 2026-10-08 Steam-session restore also matched
its current-state backup exactly. Fixtures cover old/new install/restore,
conflicting or modified files, missing-game preparation and mocked Steam child
verification, including wrong parent, extra arguments and early exit.
Process operations in launch fixtures are replaced; tests never launch a game:

```powershell
python scripts/test-cs2-loader.py --loader .local/reshade-runtime/ReShade64.dll -v
```

Install/restore fixtures also replace process queries, so a running user game
cannot contaminate the isolated file tests. A busy-process fixture verifies the
unchanged production refusal without touching the real game.

## Host depth preparation without a game install

The new observer is enabled only with `launch-cs2-lab.ps1 -HostProbe`; it is
disabled in the normal upload diagnostic session. It records bounded, non-owning
depth-resource metadata and candidate draw/clear counts. It never reads pixels
or camera constant buffers, chooses a resource, or enables world composition.

```powershell
./scripts/prepare-cs2-lab.ps1 -AllowMissingGame -Destination .local/cs2-host-probe-candidate
```

This prepares only local files even when `cs2.exe` is absent. After reinstall,
refresh the plan and make a new backup of that install before the next approved
offline session. Do not reuse the pre-reinstall snapshot as current-state evidence.
The [host depth note](../../docs/host-depth-probe.md) explains the telemetry,
projection contract, explicit unit conversion and remaining actual-game oracles.

## Still to prove in the actual offline game

The actual 2026-10-08 session produced 143 reports (137 with candidates), runtime
count returned to zero and the game remained responsive. Its screen-size D24S8
candidate was four-sample MSAA; single-sample screen-size D24S8 and other-size D16/
D24S8 candidates were also present. Final missed/deferred/overflow counts were
20/0/0, so `knownLossFree=false`. Camera/depth selection remained disabled. MC was
not running; the receiver timeout was expected and independent of host metadata.
The [host depth note](../../docs/host-depth-probe.md) records the old view-description
classification limitation and why these observations cannot identify world depth.

The new observer normalizes D3D11 2D/2DMS/array DSV descriptions, including ignored
fields and UINT32_MAX ranges. Each candidate reports up to four raw/normalized
view descriptions per interval with independent bind/draw/clear counts; extra
descriptions increment overflow while aggregate draws continue. A canonical base
DSV covers mip 0 and all layers with a compatible typed depth format and sampling
shape. It is metadata evidence only; MSAA remains unsuitable for the existing
single-sample compositor. The new view telemetry passes synthetic checks, and its
actual offline CS2 descriptions were also verified after a clean rebuild: 107
reports (103 with candidates), 1680x1050 output, typed 2DMS D24S8 on a typeless
four-sample resource, and ignored layers=0 correctly normalized to one. Direct/
indirect/clear per-view counts matched aggregate counts and nonBaseViewDraws=0.
Single-sample D24S8 and D16 views also matched; array/subresource/default-range
cases remain synthetic. Final missed/deferred/overflow=159/0/0, so no complete
coverage or performance claim. Camera/depth identity stays unverified. The exact
offline process exited normally; runtime destruction/add-on unload were logged,
the reference-count warning remains, and restored game files matched the latest
backup including the retained first-attempt crash evidence.

The observer now uses a 16384-slot multi-producer/single-consumer sequence queue
instead of callback try_lock/report-copy contention. Producers make at most eight
atomic reservation attempts; rejection is explicit, never blocking. The reporter
drains bounded batches every 2ms and emits one metadata report per second, plus
a drained finalReport at unregister. Three actual add-on lifetimes (two short
device probes and the render lifetime) emitted 165 reports/139 with candidates.
The render lifetime processed 21,227,415 events with missed/full/contended/callback-
failure/deferred/overflow all zero and knownLossFree=true. Peak backlog sampled
at drain start was 9291; maximum drain batch time 2388us, active snapshot P95 59us.
Every report's candidate/view counts matched. Final runtime/device/backlog counts
were zero and the restored directory matched its fresh backup. Camera/depth
selection remains false; this does not prove unknown callback coverage or FPS
overhead, and the D3D11 reference-count warning still needs investigation.

Optional `launch-cs2-lab.ps1 -DepthCapture` now exports limited private depth/VS/PS
binding evidence, and implies HostProbe. The raw MSAA min/max sampler passes 32
hardware/debug-layer cases; a three-slot asynchronous oracle verifies original
bytes after host mutation/release and partial constant-buffer bindings. Captures
are explicitly pre-draw, not final world frames. Public D3D11 staging/event queries
never flush/wait in production; a separate bounded writer handles disk output.
The actual offline Dust2 world-sized candidate is readable at four samples and
1680x1050, with viewport depth [0,0.95]. Identity/pose/occlusion remain unverified.
See [capture semantics](../../docs/host-depth-probe.md). Steam's startup deadline
now defaults to 120 seconds for shader/depot checks and can be set within 40-180.

Still open: verified world-depth identity, unknown host callback paths
and rendering overhead, resize/world-switch/resource lifetime,
frame callback timing, the actual host
camera/projection and depth resource/convention,
then an in-world cube with correct occlusion. Gameplay input, collision, chunk and
event routing and GPU shared transport are later stages. No server plugin supplies
these client rendering guarantees.

Dependencies: [ReShade](https://github.com/crosire/reshade/tree/v6.8.0)
(BSD-3-Clause/MIT API headers) and [nlohmann/json](https://github.com/nlohmann/json/tree/v3.12.0)
(MIT). Only CounterCraft's own sources are committed.
