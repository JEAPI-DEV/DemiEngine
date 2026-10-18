# Editor startup with dense terrain scenery

Qualified on 2026-10-17 using the Release editor, Linux/Wayland and Vulkan,
with `examples/terrain_3d`. No second engine process or compilation ran during
the measurements. The scene contains 23,848 native entities; its terrain and
vegetation density were unchanged.

The reported symptom was a long delay after the bgfx initialization message
before a window appeared. GDB entry timestamps isolated the first UI draw:
it began at 6.628 seconds and reached `endFrame` at 55.543 seconds. Sampling
the next blocked draw showed `entityParent → drawEntityNode → drawEntityNode
→ EditorHierarchyPanel::draw`. Each hierarchy row scanned the full world for
children, causing quadratic work before the first presentation.

The initial hierarchy fix built one UI-free index of IDs, parents, children,
authored ownership and preview visibility per draw. Search propagates descendant matches
through indexed ancestors. Cycles are broken for presentation and missing
parents appear as roots. Selection and painted-cell data are indexed too.
Offscreen rows retain normal tree traversal and interaction behavior but do not
emit decorative icon/background geometry.

After fixing the hierarchy, the Inspector remained expensive: 758 ms per draw.
Its closed Transform parent picker eagerly built every entity-reference choice,
including a linear world lookup for each candidate's preview ownership. Native
and Lua reference controls now request choices only for an open picker or an
accepted drop. Entity choices reuse indexed ownership; asset pickers likewise
avoid loading their registry while closed. Unknown values and selected labels
remain visible, and drops still validate against actual choices.

## Same-workload results

| Measurement | Before | Final |
| --- | ---: | ---: |
| First `endFrame`, from debugger launch | 55.543 s | 7.012 s |
| First complete UI draw | 48.915 s | 46.428 ms |
| Inspector after isolating its cost | 758 ms | 0.667 ms |
| Second-frame Inspector | 755 ms | 0.331 ms |

The final hierarchy takes 41.843 ms on the first frame, including 25.205 ms
building its index. This is linear work, not a claim of constant-time rendering
for arbitrarily large expanded trees. Project loading still takes about 5.6 s,
and graphics initialization about 1.1 s; those precede the first UI draw and
were not changed by this fix. The viewport renderer configured in about 14 ms.

Retained profiler scopes are `Editor.hierarchy`, `Editor.hierarchy.index`,
`Editor.inspector` and `Editor.inspector.reference_options`; the hierarchy also
reports its indexed entity count. Capture normal frame costs with:

```sh
./build/linux-release/demi-editor --project examples/terrain_3d \
  --max-frames 2 --profile-frames /tmp/editor-startup.csv
```

The first-window timestamps came from GDB function-entry markers, since the
frame CSV begins after project loading. Evidence from this session lives in
`/tmp/demi-editor-startup.log`, `/tmp/demi-ui-stages.log`, and
`/tmp/demi-editor-startup-final.{log,csv}`.

Five focused Debug tests pass: entity hierarchy, selection reveal, Inspector
model, reference control and ImGui input. Regressions include 30,000 siblings,
12,000 levels of ancestry, cycles/orphans, preview suppression, painted cells,
large reference lists, unknown references, and zero option-provider calls for
closed pickers. The existing input tests cover reference drag/drop.

## Retained hierarchy during navigation

The follow-up navigation change retains the index across redraws. Its cache key
uses the scene document's explicit content revision and the workspace's explicit
preview publication revision. Continuous/coalesced edits, history replay and
source replacement receive new content tokens. World replacement and terrain
preview publication/restoration invalidate the preview token, including changes
that preserve entity count or storage addresses. Direct external preview mutators
must call `EditorWorkspace::invalidateHierarchyPreview()` after mutation; native
workspace publication paths call it explicitly.

Open entity and painted-cell rows form a second retained projection. Changing
content, filtering, expansion or selection reveal rebuilds that projection;
ordinary camera navigation reuses it. ImGui clipping submits only viewport rows
while reconstructing their original ancestor ID scopes. Revealing a selection
updates expansion storage for clipped ancestors and includes the target row for
scrolling. No object or default density is removed to reduce presentation work.

The `Editor.hierarchy.rows` scope and `Editor.hierarchy.index.builds`,
`Editor.hierarchy.rows.builds`, `Editor.hierarchy.rows.total` and
`Editor.hierarchy.rows.drawn` gauges distinguish occasional rebuild work from
idle navigation. Follow-up measurements and native qualification must use the
rebuilt and relaunched editor.
