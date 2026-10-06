# CounterCraft investigation

Date: 2026-10-06

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

## Sources

- https://github.com/rehan-remade/universal-modder
- https://github.com/rehan-remade/universal-modder/blob/main/skills/mashup-mods/SKILL.md
- https://github.com/rehan-remade/universal-modder/blob/main/skills/mod-any-game/SKILL.md
- https://github.com/rehan-remade/universal-modder/blob/main/knowledge/games/portal-2/portalcraft-minecraft-inside-portal-2.md
