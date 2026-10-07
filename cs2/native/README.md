# Native receiver and D3D11 lab

This is a verified independent renderer plus a **ReShade upload/diagnostic inset
verified in actual offline CS2**. It is not a playable Minecraft port or verified
CS2 scene compositor. The original OptiFine instance is not used. The temporary
game-folder loader was restored after testing.

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

CTest runs four suites:

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
local offline test was explicitly authorized; a new installation needs its own
review. The preparation command writes only outside the game directory:

```powershell
./scripts/fetch-reshade-runtime.ps1
./scripts/prepare-cs2-lab.ps1
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
$installed = ./scripts/manage-cs2-loader.ps1 -Mode Install -BackupSnapshot $snapshot
$installed.StateFile  # keep this exact path for restore
./scripts/launch-cs2-lab.ps1 -StateFile $installed.StateFile  # preview
./scripts/launch-cs2-lab.ps1 -StateFile $installed.StateFile -Launch
# Close that offline CS2 session normally before restoring:
./scripts/manage-cs2-loader.ps1 -Mode Restore -StateFile $installed.StateFile
```

For another game location, pass the same `-Cs2Root` to preparation, install and
restore. Start the installed Steam client first; authentication is done by the
user. The launcher always uses `-insecure -countercraft-lab -countercraft-preview
-console +sv_lan 1 +map de_dust2`; `-Map` accepts a local map name. Process-local
`RESHADE_BASE_PATH_OVERRIDE` keeps config, effects, logs and cache under the
candidate directory. Console output also remains there and may contain private
Steam identifiers; do not publish raw logs.

Only `game/bin/win64/dxgi.dll` is installed. Existing loaders or game-folder
`ReShade.ini` cause refusal. The backup source and candidate/loader hashes must
match. Install uses exclusive file creation and records state before writing;
a failed partial install stays `Prepared` for inspection. Restore removes only
the exact new hash-matched loader, retaining evidence. A modified target or
manipulated target path causes refusal. Restore before normal CS2 use.

The actual restore was verified against the universal-modder snapshot: no added,
removed or changed files. The install/restore fixture suite also passed five
tests without launching a game:

```powershell
python scripts/test-cs2-loader.py --loader .local/reshade-runtime/ReShade64.dll -v
```

## Still to prove in the actual offline game

Resize/world-switch/resource lifetime, frame callback timing, the actual host
camera/projection and depth resource/convention,
then an in-world cube with correct occlusion. Gameplay input, collision, chunk and
event routing and GPU shared transport are later stages. No server plugin supplies
these client rendering guarantees.

Dependencies: [ReShade](https://github.com/crosire/reshade/tree/v6.8.0)
(BSD-3-Clause/MIT API headers) and [nlohmann/json](https://github.com/nlohmann/json/tree/v3.12.0)
(MIT). Only CounterCraft's own sources are committed.
