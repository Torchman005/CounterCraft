# Offline Minecraft actions

The Fabric host advertises `actions: true` in `ready`. Requests run on the
Minecraft client tick thread, never on the socket worker. CS2 gameplay mode uses a separate held-input protocol (see gameplay-input.md). After `hello`, read `status.epoch`, then send:

```json
{"v":1,"type":"action","id":1,"epoch":1,"action":"look","yaw":90,"pitch":20}
{"v":1,"type":"action","id":2,"epoch":1,"action":"break","block":[0,63,0],"face":"up"}
{"v":1,"type":"action","id":3,"epoch":1,"action":"place","block":[0,63,0],"face":"up"}
```

IDs increase per connection. The queue permits 32 entries, eight executions per
tick, 50 requests/second, and a 400 ms lifetime. Admission and execution require
a fresh, unpaused singleplayer world with matching epoch. Release, world changes,
pause and detected disconnect clear pending work. Already executing actions
cannot be undone by disconnecting. The socket cancels pending work after 750 ms.
Invalid requests return a descriptive error and close the connection.

`action-ack` returns action, ID, epoch, actual player feet, yaw/pitch, selected
slot, health and on-ground state. World/slot acknowledgements confirm client
execution/submission, **not** integrated-server acceptance: read `inspect` or
`inventory` afterwards to verify the result.

| Action | Fields | Behavior |
|---|---|---|
| `look` | `yaw`, `pitch` | Player orientation, pitch ±90 |
| `move` | `delta: [x,y,z]` | Step of at most two blocks with MC collision resolution |
| `break` | integer `block`, `face` | Vanilla attack/progress; repeat to mine in survival |
| `place` | integer target `block`, `face` | Place into air by clicking the neighbor opposite `face` |
| `inspect` | integer `block` | Read a loaded nearby block |
| `select` | `slot: 0..8` | Select hotbar |
| `inventory` | none | Read player-handler slots 0..45 and cursor stack |
| `click` | `slot: 0..45`, `button: 0 or 1` | Vanilla PICKUP click, including 2×2 crafting |
| `creative` | `slot: 0..8`, `item` registry ID, `count: 1..64` | Select a creative item; refused in survival |
| `target` | none | Read vanilla aim ray block/face |

Faces: `up`, `down`, `north`, `south`, `east`, `west`. Break/place require a raycast
to that visible face within vanilla reach. World actions check loaded chunks and
world/build bounds. Inventory access refuses open containers. Movement does not
override gravity or synchronize host collision. Render-only cameras retain the
64-block bound. All numbers are finite; block coordinates and slots are integers.

The CLI makes one action per connection:

```powershell
python -m bridge.action_host inventory
python -m bridge.action_host look 90 20
python -m bridge.action_host select 0
```

`bridge.action_host.Actions` supports ordered requests within a session and checks
acknowledgements. Do not run it while the native receiver owns the host connection.

Validation: queue/protocol tests cover bounds, replay, rate/capacity, expiration,
cancellation, pause/world changes and execution errors. A real isolated creative
world accepted inventory reads and grass-block removal; a new connection read
air at that position. The real-world verifier subsequently placed/mined planks
at (28,129,-16), crafted one oak log into four planks, and read the result in slot
37 with an empty cursor. A collision-aware step moved the player out of a hole.
These are Minecraft guest results; CS2 input awaits acceptance.

For a repeatable, save-modifying creative lab check (back up the lab first):

```powershell
python -m bridge.verify_actions
```

It requires nearby full ground/air and clear crafting slots/cursor. It writes
first two hotbar slots only when empty or holding its earlier expected planks.
Creative placement and subsequent world/inventory reads verify actual results.
