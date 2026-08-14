# Editor

The experimental editor is a native desktop workspace over DemiEngine's
existing project, scene, component, source, and validation contracts.

```sh
cmake --build --preset linux-debug
./build/linux-debug/demi editor --project examples/minimal_voxel
```

When run inside a project directory, `--project` may be omitted. The editor
discovers the nearest parent `demi.project.json` through the same shared
filesystem service used by `demi dev`.

## Current slice

- The hierarchy displays entities from the loaded main scene and follows
  authored 2D/3D transform parents.
- Selection drives a reflected inspector over the authored scene document.
  Boolean, integer, number, string, enum, vector, and color fields are editable
  using shared component descriptors, including their numeric bounds.
- Entity names, explicit enabled state, and explicit layers are editable.
- Inspector changes are reversible commands. Continuous drags collapse into a
  single undo step; Save writes deterministic JSON through same-directory
  atomic replacement and refuses to overwrite an externally changed scene.
- The Assets panel lists authored project files while excluding generated,
  build, package-cache, and Git internals.
- The Console displays diagnostics from the same `validatePath` service used by
  the CLI.
- Play saves pending valid changes and starts the normal `demi-runtime` beside
  the editor. Pause/Resume and Stop control that owned process.
- The central scene view renders authored 3D entities through the engine's
  existing bgfx renderer on the editor graphics device. It does not maintain a
  second editor-only rendering implementation.

Lua-created/runtime-generated geometry appears in the Play window rather than
the authored scene preview. Component add/remove, hierarchy structural
commands, 2D preview, picking/gizmos, embedded frame stepping, and builds remain
disabled until their real engine services exist.

## Boundaries

`EditorSceneDocument` owns authored JSON and command history;
`EditorDocumentStore` owns conflict detection and atomic file replacement.
`EditorWorkspace` coordinates those documents with loaded project state,
selection, source discovery, and diagnostics. `EditorUiHost` owns SDL3, bgfx,
input forwarding, the authored 3D viewport, and the Dear ImGui frame lifecycle.
The viewport reuses `BgfxRenderer3D`, `GpuResources`, and `RenderCommands` on a
separate bgfx view. Runtime and authored-data modules do not depend on ImGui.
