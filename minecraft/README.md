# Minecraft 1.20.1 camera lab

This Fabric client mod accepts camera position, yaw/pitch and vertical FOV over
local TCP. It does **not** export colour/depth, move the simulated player, forward
clicks or draw anything in CS2 yet.

**Validation status:** the Java network/state core compiles and passes five tests.
The full Fabric build is currently blocked downloading Loom's `mercury-0.4.2.jar`.
No installable mod jar has been produced, and Mixin hooks/in-game behavior are
unverified. The steps below describe the intended build and lab workflow.

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
then run `gradle -g ../.local/gradle runClient` from this folder with the same Gradle
and JDK versions. Do not launch multiple Gradle builds at once.

## Verify a camera session

Enter and unpause a single-player test world. From the repository root:

```powershell
python -m bridge.minecraft_host         # status only
python -m bridge.minecraft_host --demo  # five-second camera motion
```

The game-side port is **37122**, distinct from the Python diagnostic server's
37121. Only one host is accepted at a time. The wire format is JSON Lines v1:
`hello` (`role: test` or `cs2`), `status`, `camera`, `release`.

- Camera `position` is in MC blocks; `rotation` is `[yaw,pitch,0]` in degrees.
- Frame numbers must increase within each TCP connection, including after release.
- Roll is rejected. The camera stays within 64 blocks of the player's eye position
  because this slice does not move the chunk-loading player.
- Camera control expires after 500 ms without a fresh pose or world snapshot.
  Pause, disconnect, world changes and leaving single-player release control.
- Two seconds without incoming TCP data closes the connection. Messages are bounded,
  and game objects are only read from Minecraft's client thread.

`ack` means the request was accepted; visual movement still needs in-game verification.
Check `run/logs/latest.log` for `Camera lab listening` and any Mixin errors.
