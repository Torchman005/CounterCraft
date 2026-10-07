# CounterCraft offline modding plan

- Install: `D:\steam\steamapps\common\Counter-Strike Global Offensive` (Steam app 730, build 25738536)
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
2. Fabric 1.20.1 camera lab: receive host poses over localhost, apply render-only camera/FOV overrides, and restore the vanilla view on timeout. Colour/depth export is a subsequent stage.
3. Add the CS2 offline host adapter and render one test cube with depth ordering.
4. Add block/entity/event forwarding, then package a launcher and backup/restore flow.

## Verified checkpoint (2026-10-07)

- Milestone 1: eight Python tests pass, including actual localhost sockets.
- Milestone 2 build: Gradle 8.14/JDK 21 produced the Fabric 1.20.1 jar;
  five Java tests pass under Gradle. The independent development client loaded
  the mod; the new `CounterCraft Lab` world accepted 100 camera requests and
  released control on port 37122.
- Milestone 2 world validation remains pending. A status reply from a menu is
  not evidence of rendered camera movement. Test a new disposable lab world.
- Colour/depth export, a CS2 client compositor and gameplay input/event routing
  remain unimplemented. Server-side Source 2 plugins do not solve composition.
