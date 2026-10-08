# Offline camera relay lab

`launch-cs2-lab.ps1 -CameraRelay` opts into a real CS2-to-Minecraft camera relay.
It is disabled by default and requires `camera-layout.json` in the prepared
candidate directory. This is a camera/preview experiment, not playable world
fusion. The regular offline guard and reversible Steam loader workflow apply.

Generate private calibration from a known world-camera capture:

```powershell
python -m bridge.camera_evidence '<capture.json>' --calibration '<candidate>/camera-layout.json' --output .local/calibration-evidence.json
```

Calibration records locations in public VS bindings for view, projection, full
VP and relative VP. Never commit a game's calibration or captured buffers. The
native decoder checks the bound byte ranges, affine orthonormal view, viewport
aspect, canonical projection and both independent matrix products each time.
Bad or missing bindings are rejected. A matching set still requires scene
evidence; there is no stable retail ABI or process-memory scanning.

The opt-in host feed samples at draw64 of output-sized depth candidates, at most
30 times per second per device. Three owned staging/query slots copy only the
calibrated buffers. Polling uses DONOTFLUSH/DO_NOT_WAIT, and a bounded latest-value
mailbox sends CPU bytes to a decoding worker. No network IO runs in GPU callbacks.
The current candidate choice is experimental; draw64 is not a guaranteed main
world pass across maps or game updates. Invalid and busy counts are reported.

The receiver anchors the first validated host eye to the initial Minecraft eye,
using the existing coordinate convention of 32 Source units per block. It sends
position, yaw/pitch and vertical FOV at approximately 30 Hz maximum; nonzero roll
is unsupported. A stale host sample (>250 ms) releases camera control. Stream
disconnect/world change also releases control through the Minecraft server.
The guest player is not moved yet: Minecraft's existing 64-block camera-distance
limit remains in force. There is no collision, gameplay input or chunk sync yet.

`camerasSent` counts acknowledgements. `cameraFramesMatched` independently checks
the actual streamed render camera against a bounded history of sent poses; a
disagreement terminates the session. These checks do not prove equal framebuffer
aspect, final-pass depth, latency correction or world occlusion. Host 1680x1050
and guest 1280x720 currently have different aspect ratios and need aligned
projection before fusion.

Real-game checkpoint: 4,910 acknowledged cameras, 3,308 matching rendered MC
frames, 3,897 received/3,775 uploaded frames, nine stale releases. A later request
was rejected by MC; the original run lacked the detailed reason, so the 64-block
limit is a hypothesis, not a proven diagnosis. That run used uncapped host
sampling (123,391 decoded / 12,402 rejected); the subsequent 30 Hz cap and detailed
error propagation passed tests but await a new real run. The screenshot at the
end showed the stream had stopped, so it does not prove a successful visible
moving preview. Normal exit restored the loader, backup diff was empty, runtime
and pending readbacks returned to zero. ReShade's reference warning 1929 remains
unattributed. MC reported inactive camera, stopped stream and zero leased buffers.

For boundary-depth analysis, `bridge.depth_evidence --source-camera ...
--target-camera ...` accepts earlier camera captures only when session, frame,
request, candidate resource, texture shape and previous viewport all agree.
It requires the first viewport transition and never substitutes the new pass's
bindings for the previous pass's projection. Real repeated boundary captures
compared 74,847 samples with 99.884% within 1%; this is static consistency, not
live depth pairing or a complete world-pass guarantee.
