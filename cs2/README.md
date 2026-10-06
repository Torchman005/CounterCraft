# CS2 host adapter

CounterStrikeSharp is a server-side Source 2 framework. It can provide offline
player/map/event hooks, but it does not itself composite a second renderer into a
CS2 client. The client-side frame/depth stage therefore remains a separate task.

Use `game/launch-offline.ps1` for local testing only. Never launch this build into
official matchmaking.
