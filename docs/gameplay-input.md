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
| R | Alternative held use/place in world view; ordinary character in GUI |
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

T opens vanilla chat; `/` opens command chat. Printable characters from the
current Windows keyboard layout, Enter, Tab, Backspace, Delete, arrows, Home and
End are relayed by a separate 64-event FIFO, not the held-state mailbox. Events
expire after 250 ms; every event uses an increasing action ID/current epoch and
requires an execution acknowledgement. Focus loss, F8 and reconnect drop pending
events, with counters in native reports. No replay across sessions. There is no
IME composition, clipboard or typematic forwarding. Very short physical key
presses can still be missed before queue admission.

The hello capability `ui: true` is required by the new gameplay host; restart an
older MC client instead of mixing mod versions. A direct UI action example:

```json
{"v":1,"type":"action","action":"ui","id":1,"epoch":1,"text":"hello","modifiers":0}
```

Text is at most 64 UTF-16 units, with no control characters; a message contains
either text or one allowlisted GLFW key, never both. `action-ack.applied` reports
the vanilla screen handler result; inspect game state/chat logs for the effect.
Chat commands remain subject to the singleplayer world's permissions. R is only
a world-use alternative, to avoid right-clicking chat suggestions while typing.

Escape does not open a
remote pause menu in the world; use F8 to return to CS2, or pause in the MC client.
Guest pause suppresses stream/control until locally resumed. The viewport is
aspect-fitted to the host backbuffer, with centered black bars. GUI deltas use
the fitted content dimensions, so pointer and slot coordinates share the same
mapping. Runtime effects/Steam overlays can
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
CS2 right mouse and R-key use both placed oak_planks at (27,127,-9), independently
read back after closing CS2. ReShade 6.8's right/middle button indices disagree
between header and implementation; public VK button codes avoid that ambiguity.
Pause/resume reconnects with a fresh session and neutral input. Actual guest
resize 960x540 -> 1280x720 kept receiving/uploading with resourceFailures=0. Additional controls
require further scene validation. Private screenshots/logs are ignored and not distributed.

2026-10-09: CS2 T + h/e/l/l/o + Enter produced the exact vanilla MC chat log
`hello`, with six confirmed UI events and zero drops. Survival mode and damage
commands also executed from CS2. A later native redstone command test was stopped
when desktop focus changed; it is not accepted as a CS2 redstone interaction.
The independent guest oracle confirms real redstone lit/unlit, pig entity damage
and survival health 18, plus local UI death-screen respawn. These are vanilla
guest checks, not host entity/collision fusion. Sprint is now a held vanilla
key query, preserving MC hunger/collision restrictions instead of forcing state.
