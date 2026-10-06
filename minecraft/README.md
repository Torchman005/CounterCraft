# Minecraft adapter

The requested `1.20.1-OptiFine_I6` profile is kept intact. Do not copy a bridge
mod into that profile or into an existing world.

The first implementation target is an isolated Fabric 1.20.1 profile because the
upstream passthrough example uses Fabric mixins for camera/frame hooks. Once the
protocol in `bridge/protocol.py` is stable, the adapter will publish `camera`,
`blocks`, `entities`, and `event` messages to `127.0.0.1:37121`.

This directory currently contains no game code or extracted Minecraft assets.
