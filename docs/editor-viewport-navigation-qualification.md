# Dense-terrain viewport navigation

This qualification covers `examples/terrain_3d` on Linux with the optimized
Release editor and Vulkan. Terrain, scatter density, shadows and render distance
are unchanged. Native measurements must run without another engine process or
compilation, with the desktop awake and the editor visible. Relaunch after every
native rebuild; restarting Play does not replace the editor executable.

## Diagnosed costs and changes

The initial navigation trace spent about 162 ms per active editor frame, including
39 ms in UI and 50 ms extracting renderable objects across views. The initial
hierarchy fix removed quadratic startup work but still rebuilt a large index on
every redraw. The retained projection described in
[the hierarchy qualification](editor-hierarchy-startup-qualification.md) now
invalidates on authored/preview revisions and clips offscreen rows.

Instancing also consumed bgfx's transient vertex arena. The terrain and vegetation
passes exhausted the same arena that ImGui needed, causing missing editor panels
and a failed viewport. Native instance uploads now use a dedicated, reusable pool
of dynamic buffers. Draw chunks occupy disjoint ranges until the actual native
frame advances. Context epochs prevent stale handle reuse after shutdown/restart.
The default pool uses 1 MiB blocks with a 64 MiB budget per command owner; typed
creation options can adjust it. Budget failures leave UI transient memory intact.

The intermediate native trace confirmed zero failed viewport submissions and
reduced hierarchy work from about 36 ms to 0.073 ms. Editor frames still took
about 122 ms, so that intermediate result was insufficient.

The subsequent CPU changes address repeated work in the shared renderer:

- Resolve an indexed world-transform snapshot once per presentation and reuse
  component classification, bounds and matrices across shadow/depth/camera views.
- Reuse resolved appearance for equivalent opaque, static primitive surfaces.
  Material, animation, deformation, transparency and terrain paths retain their
  existing preparation behavior.
- Skip animation preparation when source classification finds no animated mesh.
- Reuse empty preparation vectors between presentations. Source values are still
  rebuilt, so allocation reuse does not require persistent component revisions.
- Compute scaled transform axes from one quaternion and bypass rotation conversion
  for unrotated parents. Geometry remains equivalent for negative/nonuniform scale;
  an unrotated parent preserves the child's authored Euler representation.
- Cache native component type hashes while retaining `type_index` equality and
  checked `any_cast` access. Hash collisions still compare full type identity.

Borrowed source state is immutable for one presentation. Gameplay and editor
mutations occur outside that interval. An owned transform snapshot survives that
boundary, but compares every entity ID, transform presence, parent and local
position/rotation/scale before reuse. It never infers freshness from addresses
or counts. Instance math similarly compares owner/local values and mesh size;
model/material-dependent bounds are recomputed every presentation. Runtime multi-camera rendering brackets
its const-world calls with one presentation; individual editor `renderFrame`
calls own separate presentations. Source edits require a reset before reusing
standalone extraction preparation. No persistent cache infers changes from entity
count or component addresses.

## Repeatable checks

Capture with the rebuilt Release executable:

```sh
SDL_VIDEODRIVER=x11 ./build/linux-release/demi-editor \
  --project examples/terrain_3d --max-seconds 100 \
  --profile-frames /tmp/terrain-navigation.csv
```

X11 is used for desktop input/capture tooling, not by the fixes. Record resolution,
backend, build and focus state. Move the camera while holding the right mouse
button; distinguish those frames from the editor's idle redraw throttle.
`Editor.camera.*` gauges verify actual motion. `Editor.viewport3d.failed` must stay
zero. Instance upload bytes/chunks and hierarchy build/drawn-row gauges make
resource use and cache reuse observable.

The isolated component-storage benchmark and its allocation accounting live under
`tools/performance/`. Its timings describe lookup cost, not end-to-end speed.

## October 18 verification and remaining limit

The full Debug build succeeded. A selected 22-test gate passed across component
storage/schema, transforms, visibility, native commands, 2D/3D rendering,
hierarchy/document/prefab/HUD editing, physics, Lua and terrain. After the final
transform changes, the transform, visibility and 3D renderer tests were rebuilt
and passed again. Tests cover same-count rename/reparent/reorder, missing/cyclic
parents, component removal, replacement worlds, instance edits, changed mesh
size, changed model bounds, context restart and instance-budget failure. This is
not a claim that the entire 448-test suite was run.

`terrain_3d` (26 files), `rocky_valley_3d` (50 files) and `minimal_3d` (9 files)
validated without diagnostics. The Release CLI, editor and runtime were rebuilt.
A bounded native runtime run also exited normally; its debugger samples are for
hotspot identification, not timing comparisons.

The first new full-resolution camera-motion run captured 173 moving-camera
frames: median editor work 83.24 ms, p95 87.33 ms; viewport 79.45 ms, extraction
48.24 ms, GPU median 12.94 ms. It rendered at 3840 x 2022 on the current monitor.
Camera gauges confirm movement and every viewport failure gauge is zero.
This was an intermediate build, before retained transform/instance math.

The final build's no-input redraw probe at 5120 x 2806 rendered the same initial
camera and all 71,690 considered/visible mesh records across its views. Nine
post-initialization samples had these medians:

| Scope | Final probe |
| --- | ---: |
| Editor CPU work | 68.43 ms |
| Viewport | 64.47 ms |
| Extraction, all views | 30.04 ms |
| Source preparation | 9.29 ms |
| Content-checked world transforms | 3.58 ms |
| Instance extraction, all views | 13.15 ms |
| Instance input checking/preparation | 5.65 ms |
| Surface ordering | 9.35 ms |
| Mesh submission | 8.24 ms |

The initial camera gauges were equal, but resolution, selected UI state and
terrain triangle counts differed between probes; these are not a controlled
end-to-end speedup ratio. Startup redraws also do not establish navigation
percentiles or steady-state GPU time. The final screenshot has intact panels
and terrain, and viewport failure gauges remain zero. The final camera-motion
repeat was blocked by GNOME input permission expiring; no input was injected
around that restriction.

**The performance complaint is not fully resolved.** Even the final redraw probe
is above a 60 Hz CPU budget. Remaining measured costs include instance traversal,
surface ordering, source classification and submission. Further work should
measure those paths rather than reducing scene density or blaming the GPU.
The retained source/transform scopes make those costs observable in future runs.

Session evidence: `/tmp/viewport-final-tests.log`,
`/tmp/viewport-retained-tests.log`, `/tmp/terrain-navigation-final.csv`,
`/tmp/terrain-retained-final.csv` and `/tmp/terrain-retained-final.png`.
Temporary evidence files are not packaged assets or permanent archival storage.

## Navigation repeat after permission renewal

Desktop input access was renewed and the committed Release build (`aeda5edb`)
was tested at 5120 x 2806 with one editor, no concurrent runtime or compilation.
The right-button camera movement completed; camera gauges identify 222 moving
frames after initialization. Screenshots show intact terrain and editor panels.

| Scope | Median | p95 |
| --- | ---: | ---: |
| Editor CPU work | 66.60 ms | 68.21 ms |
| Viewport | 63.46 ms | 64.95 ms |
| Extraction, all views | 31.50 ms | 32.53 ms |
| Content-checked world transforms | 3.88 ms | 4.89 ms |
| Instance extraction, all views | 13.02 ms | 14.44 ms |
| Surface ordering | 9.36 ms | 9.71 ms |
| Mesh submission | 8.05 ms | 8.61 ms |
| GPU | 6.45 ms | 10.72 ms |

Every moving frame reported `Editor.viewport3d.failed = 0`. This resolves the
permission-related verification gap above, but not the CPU performance issue.
These scopes overlap; do not add child scopes to their enclosing viewport cost.
The previous intermediate movement run used a different resolution/camera path,
so it remains unsuitable for an exact speedup ratio.

Evidence: `/tmp/terrain-navigation-verified.csv`,
`/tmp/viewport-navigation-verified.log`, and
`/tmp/terrain-navigation-verified-{before,after}.png`.

The later [Game View performance investigation](terrain-game-view-performance.md)
records shared membership queries, revisioned instance input, runtime hierarchy
clipping and removal of empty debug/text transform traversals. Its final embedded
comparison remains pending desktop input access.
