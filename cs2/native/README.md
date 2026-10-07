# Native receiver and D3D11 lab

This is a verified independent renderer plus a **compiled, uninstalled ReShade
upload candidate**. It is not a playable Minecraft port or verified CS2 scene
compositor. The original OptiFine instance is not used.

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
oracle, **not host-depth composition**. The FX file has not yet been compiled by
ReShade or tested in CS2. Unsupported runtimes and stale/disconnected frames leave
the inset inactive. A failed connection requires a new test session; no automatic
reconnect is implemented.

Render callbacks do not perform socket IO, CRC/depth scans, disk logging or thread
joins. Resources are per runtime and recreated for guest-size changes. Log JSON
comes from a reporting worker. Normal network/report thread cleanup occurs in
`AddonUninit`, which ReShade calls before `FreeLibrary`; `DllMain` does no work.
Forced process termination relies on OS reclamation, avoiding a loader-lock join.

```powershell
./scripts/prepare-cs2-lab.ps1
```

This prepares the compiled candidate, effect, config/preset and `install-plan.json`
outside the game folder. It does not install anything. The plan names the only
proposed game targets (`game/bin/win64/dxgi.dll`, `ReShade.ini`), their existing
state/hashes, exact launch arguments and backup/restore requirements. An official
x64 full-add-on ReShade loader must be obtained/verified separately before install;
this build contains API headers only. Installing the loader requires specific
user approval under the universal-modder workflow.

## Still to prove in the actual offline game

Loader/API compatibility on this CS2 build, FX compilation, upload/resize/teardown,
callback timing, the actual host camera/projection and depth resource/convention,
then an in-world cube with correct occlusion. Gameplay input, collision, chunk and
event routing and GPU shared transport are later stages. No server plugin supplies
these client rendering guarantees.

Dependencies: [ReShade](https://github.com/crosire/reshade/tree/v6.8.0)
(BSD-3-Clause/MIT API headers) and [nlohmann/json](https://github.com/nlohmann/json/tree/v3.12.0)
(MIT). Only CounterCraft's own sources are committed.
