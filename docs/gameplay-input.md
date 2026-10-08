# Offline held input and full-client frames

`ready.input` advertises held input. A connected host requests `stream-start`
with `fullClient: true` and reads the current epoch from status. Each input ID
increases within the connection:

```json
{"v":1,"type":"input","id":1,"epoch":1,"yaw":0,"pitch":30,"forward":1,"sideways":0,"slot":0,"attack":false,"use":false,"inventory":false,"escape":false,"mouseX":0.5,"mouseY":0.5}
```

Optional boolean fields: jump, sneak, sprint, drop, swap, pick. Scroll is a
bounded cumulative signed counter; GUI ticks apply only its change, so dropped
snapshots do not lose scroll distance. Slot is 0–8; forward/sideways are ±1;
pitch is ±90; mouse coordinates are normalized top-left GUI coordinates.
`input-ack` acknowledges admission, not a world mutation. Read player/world
state to confirm gameplay results.

The mailbox holds only the latest immutable snapshot. At most 100 Hz is admitted;
native transmission is capped at 30 Hz. Minecraft consumes it on its client tick
thread and expires it after 250 ms. Release, pause, changed world, detected
connection loss and stale tick state neutralize input. Native reception retries
at one-second intervals and obtains a new session and epoch. One host owns the
Minecraft control connection; do not run diagnostic CLIs concurrently.

## Controls

| CS2 input | MC behavior |
|---|---|
| W/A/S/D, Space, Shift, Ctrl | Vanilla movement, jump, sneak, sprint |
| Mouse movement | Yaw/pitch, 0.12° per reported pixel |
| Left/right mouse | Attack/mine, use/place; GUI pickup/split |
| R | Alternative held use/place or GUI right click |
| E / Esc | Open inventory / close handled GUI |
| 1–9 / wheel | Hotbar selection / GUI scrolling |
| Q / Ctrl+Q / F | Drop item / stack / swap offhand |
| Middle mouse | Vanilla pick block |
| Shift/Ctrl in GUI | Vanilla GUI modifiers; held-button drag forwarded |
| F8 | Toggle MC view/control; watchdog releases MC input |

ReShade exposes cached screen cursor coordinates, not raw movement. The adapter
uses differences, ignores synthetic returns to the window centre, seeds after
focus/reconnect and never repeatedly accumulates a stationary coordinate.
Screen-to-client centre conversion and actual client dimensions avoid mismatched
render-resolution GUI movement. Ordinary mouse settings can affect sensitivity.

## Render semantics and limits

A full-client frame reads world depth before the hand pass, then final color after
hand/HUD/GUI. Metadata explicitly says `includesHandHud: true`,
`layer: client-color-world-depth`, and supplies `guiOpen`. That color/depth pair
**must not be used as a world-depth compositor layer**. The shader currently
replaces the host view; diagnostic mode retains the inset. Host and guest worlds,
collisions, weapons and entities are not fused.

Chat/search text and IME forwarding are not implemented. Escape does not open a
remote pause menu in the world; use F8 to return to CS2, or pause in the MC client.
Guest pause suppresses stream/control until locally resumed. The viewport is
currently stretched to the host backbuffer. Runtime effects/Steam overlays can
intercept input. Very short presses may be missed by latest-snapshot sampling;
this is an experimental build, not an all-controls compatibility guarantee.

## Evidence

Guest verifier (`python -m bridge.verify_input`): actual movement, world and GUI
frames, return to world, release clearing input ID. Native sockets test wrong
acknowledgements and stale releases. Mouse regression tests include 10,000
stationary samples, reversal, warp, focus reset and extreme deltas.

Real Steam offline CS2: forward moves the guest; E opens/closes its inventory;
mouse reversal changes yaw/pitch by ±12° for ±100 pixels and does not drift when
stationary; a creative inventory click picks up oak_log; left click produces air
in the target cell. Guest held use charges a bow for 30 ticks and stops on release.
CS2 R-key use placed oak_planks at (27,127,-9), independently read back after
closing CS2. Mouse-right transport remains unaccepted; use R. Additional controls
require further scene validation. Private screenshots/logs are ignored and not distributed.
