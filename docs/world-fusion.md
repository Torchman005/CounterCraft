# Experimental offline world fusion

The playable alpha still defaults to the full Minecraft client bridge. World
fusion is a separate, opt-in rendering experiment. It does not couple player
physics, host collision, guns, entities or chunk loading. Moving cameras hide
the guest until an equal rendered pose is available; there is no reprojection.

Use the supervised Steam launcher with your own private calibration:

```powershell
./game/play.ps1 -Mode Play -Cs2Root '<CS2 root>' -BackupSnapshot '<completed backup.zip>' -SessionDirectory '<fresh local session>' -WorldFusion -CameraLayout '<private camera-layout.json>' -FusionPolicy '<private fusion-policy.json>'
```

Start the isolated, unpaused MC lab first. The regular offline guard, fixed
`-insecure` Steam child verification and hash-guarded loader restoration apply.
`WorldFusion` cannot be combined with gameplay input. Calibration is copied into
the private session candidate, never embedded in a binary or published. The
regular configured launcher continues to use the accepted full-client mode.

The boundary policy has schema 1, kind `local-world-boundary`, two separately
measured `worldDepthRange` and `foregroundDepthRange` arrays, an observed
`clearDepth`, and `cameraDraw` (a bounded camera sampling milestone). It must be
derived from local render evidence. A milestone is not a stable world-pass ID.
The viewport boundary is detected from draws; its draw index is never hardcoded.
Do not copy another game's calibration or assume a map/update has the same passes.

The adapter requires an output-sized single-layer depth resource, a full observed
clear, a world camera snapshot and the first matching world-to-foreground depth
range change on the same resource in the same effect interval. The camera is
copied before the transition; new-pass constant buffers cannot describe old
depth. Resource changes, clears, missing bindings and incompatible viewports
reject the interval. An observed full clear after the latched world snapshot
is recorded separately, allowing cleared final pixels to use owned world depth;
partial/unknown clears still reject it. Later writes never replace the snapshot.

World and final depth are copied into owned GPU textures using the existing
single/MSAA sampler. The effect keeps host pixels where post-world depth differs,
including weapons and later scene writes, except explicitly observed full-clear
background pixels. Ambiguous MSAA edges also remain host
pixels. Guest sky is ignored. Raw host depth is normalized by its world viewport
and linearized with the calibrated projection; distances use 32 Source units
per MC block. Guest projection scales resample matching rays across aspect ratios.

CPU camera readback is asynchronous and cannot be assumed to complete during the
same frame. It validates bound ranges, the view/projection and both independent
matrix products, then relays the pose to MC. The receiver independently checks
the actual guest render against that request. Separately, current-frame matrices
are copied into a tiny owned GPU texture. A one-pixel ReShade pass compares all
four current matrices with the validated camera that produced the guest frame.
Only a matching GPU result enables composition. It never waits for a CPU query,
and checking the matrix texture once avoids repeating 16 samples per screen pixel.

The raw buffer extractor restores CS shader/class instances, four SRVs, UAV0 and
CB0. The depth sampler restores its compute bindings. All host drawing is left
to ReShade's protected effects; socket IO, logging and frame CRC scans stay off
the render callbacks. Owned resources are unbound before resize/unload, with no
host COM reference retained between callbacks.

Reports distinguish camera snapshots, observed boundaries, GPU frame pairs and
effect-eligible submissions. These counters do **not** count visible MC pixels
or prove scene occlusion. The current real Dust2 run has **zero effect-eligible
frames**: later noncalibrated viewport changes reject the interval. This is an
unfinished experiment, not usable world fusion. `liveOcclusionVerified` remains false until a recorded
front/behind-wall scene oracle passes. A mathematical pass or inset screenshot
cannot substitute for that acceptance.
