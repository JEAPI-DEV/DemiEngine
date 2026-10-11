# Deferred editor fixes

- In a nested entity prefab, the child Transform Inspector shows Parent = None
  when parenting comes from the authored children array (observed on depot/rack).
  Show the effective inherited parent, or clearly distinguish an omitted explicit
  parent field from no hierarchy parent. Do not materialize parent fields merely
  to make the Inspector display them.

## Unresolved input incident, 2026-10-11

- After starting the optimized editor on `terrain_graph_3d`, the terrain was
  unexpectedly deleted in the unsaved scene. The status bar showed `Delete`;
  the user confirmed they did not delete it. Automation intended only a Play
  button click. Undo restored the terrain and the clean document; no deleted
  state was saved. A subsequent controlled click started Play normally without
  reproducing deletion. Shortcut bindings were defaults and no recovery snapshot
  remained after Undo. Investigate command/input provenance around startup and
  remote input access before attributing this to either recovery or the editor.
  Evidence: `/tmp/terrain-perf-release-editor-full.png` (deleted state),
  `/tmp/terrain-perf-restored-full.png`, `/tmp/terrain-controlled-play-full.png`.
