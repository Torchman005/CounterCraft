# Experimental offline world fusion

The playable alpha still defaults to the full Minecraft client bridge. World
fusion is a separate, opt-in rendering experiment. It does not couple player
physics, host collision, guns, entities or chunk loading. Moving cameras hide
the guest until an equal rendered pose is available; there is no reprojection.

Use the supervised Steam launcher with your own private calibration:

```powershell
./game/play.ps1 -Mode Play -Cs2Root '<CS2 root>' -BackupSnapshot '<completed backup.zip>' -SessionDirectory '<fresh local session>' -WorldFusion -CameraLayout '<private camera-layout.json>' -FusionPolicy '<private fusion-policy.json>'
```

The configured launcher can now start and supervise both processes:

```powershell
./game/start.ps1 -Launch -WorldFusion -CameraLayout '<private camera-layout.json>' -FusionPolicy '<private fusion-policy.json>' -SessionDirectory '<fresh local session>'
```

Omit `-Launch` to preview. Calibration files are required and checked before
starting MC; arguments without `-WorldFusion` are rejected. For `play.ps1`,
start the isolated, unpaused MC lab first. The regular offline guard, fixed
`-insecure` Steam child verification and hash-guarded loader restoration apply.
`WorldFusion` cannot be combined with gameplay input. Calibration is copied into
the private session candidate, never embedded in a binary or published. The
configured launcher defaults to the accepted full-client mode.

The boundary policy has schema 1, kind `local-world-boundary`, two separately
measured `worldDepthRange` and `foregroundDepthRange` arrays, an observed
`clearDepth`, and `cameraDraw` (a bounded camera sampling milestone). It must be
derived from local render evidence. An optional `additionalBoundaryDepthRanges`
list accepts up to four independently measured first-transition ranges. These
ranges are exact matches with the same resource and viewport dimensions, not
automatic guesses based on their numeric values. A milestone is not a stable world-pass ID.
The viewport boundary is detected from draws; its draw index is never hardcoded.
Do not copy another game's calibration or assume a map/update has the same passes.

An optional `additionalCoverageDepthSizes` accepts up to four exact `[width,height]`
integer pairs, each dimension in 1..4096. Use only locally observed post-world
resources. They must be no larger than the output and may participate only after
the world boundary is latched. Reduced-resource depth writes require one viewport
covering the entire texture at origin zero. Such resources never replace the world
camera or boundary. The GPU expands each texel to its output footprint using
integer proportional coordinates, retaining detected writes through later clears.
Unknown sizes or partial reduced viewports reject the interval. Resize requires
new local evidence; this is conservative coverage, not automatic pass detection.

The adapter requires an output-sized single-layer depth resource, a full observed
clear, a world camera snapshot and the first matching world-to-foreground depth
range change on the same resource in the same effect interval. The camera is
copied before the transition; new-pass constant buffers cannot describe old
depth. Resource changes, unknown/partial clears, missing bindings and incompatible
viewports before the latch reject the interval. An observed full clear after the
latched world snapshot captures accumulated coverage before the clear executes.
Later viewports cannot redefine the owned snapshot. Resource changes still reject
the interval before the latch. After the latch, output-sized or calibrated reduced depth
resources are independently tracked and conservatively compared with the owned
world min/max range. A single-sample resolve inside that range is allowed, with
a raw-depth tolerance of 1e-5; any range extending outside it is protected.
This does not establish the semantic identity of every later pass.

World and final depth are copied into owned GPU textures using the existing
single/MSAA sampler. An owned GPU mask retains changed pixels before every known
clear, before depth-copy overwrites, on resource switches and at the effect
boundary, with a limit of 32 captures per interval and sixteen tracked resources.
Copy destinations are observed even before their first DSV binding. A value-only
write journal flushes every dirty destination, including unbound ones, before
camera/depth acceptance; failures reject the interval. Unsupported post-world
depth writes/copies/clears reject rather than silently escaping coverage.
After the first clear, subsequent comparisons use the observed clear as their
baseline. The mask never resets until the next world snapshot, so a later clear
cannot erase a previously detected weapon or scene write. The effect keeps these
pixels and also compares final depth. Ambiguous MSAA edges remain host
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
or prove scene occlusion. Reports retain up to 128 resource/viewport events in
fixed storage; JSON serialization occurs in the reporting thread. `lastUnsupportedDepth`
records the last unsupported **draw write**, including resource/color shapes and
viewport; it may persist after later clear/copy errors and is not a failure count.
The 2026-10-10 Dust2 live13 final report has **43,331 effect-eligible submissions**,
46,565 boundaries, 45,227 GPU pairs, 3,861 matched guest camera frames, 4,183 received
frames, 2 reconnects and 2,091 invalid intervals. Its trace is truncated and its
last error is an unsupported host pose. Both sampled connection failures report
the existing 64-block camera distance restriction; no idle receive deadline
failure was sampled. Session durations and scenes differ, so these are not a
controlled performance comparison with the earlier 145,144-submission session.

The new strict checks initially rejected every interval. Live diagnostics measured
420x262, 840x525 and 420x525 post-world resources at the 1680x1050 output; the final
private policy includes only these three sizes. Live13 recorded no unsupported
draw-write descriptor. This does not prove all clears/copies or poses are supported.
Complete ReShade FX compilation succeeded during live10, and real callbacks ran
through live13. Dust2 screenshots still do not prove visible guest pixels or wall
occlusion in these runs. All four new sessions restored their loaders, closed owned
guests, and the postflight game-folder hash diff is empty. GPU coverage initialization
uses the verified public device identity marker through ReShade's device proxy.
This remains an unfinished experiment. `liveOcclusionVerified` remains false until a recorded
front/behind-wall scene oracle passes. A mathematical pass or inset screenshot
cannot substitute for that acceptance.

An optional private-policy `diagnosticView` integer is disabled by default (0).
Mode 1 displays blue for unavailable fusion, red for GPU camera mismatch and
green for a match; mode 2 displays retained coverage; mode 3 displays guest color.
These diagnostic outputs bypass normal display and cannot count as occlusion
acceptance. See [current status and next acceptance](project-status.md).
