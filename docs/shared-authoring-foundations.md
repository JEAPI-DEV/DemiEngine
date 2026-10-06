# Shared authoring foundations

These C++ facilities are shared engine/editor infrastructure. They do not add
a new project file format, Lua service or visual-scripting runtime. Existing
document adapters own their schemas, durable IDs, validation and serialization.

## Dependency graphs

`demi::graph::DependencyGraph` lives below runtime and asset integrations. Both
terrain execution and asset cooking use it for deterministic dependency order
and reachability. Edges point from a node to its dependency. IDs are strings;
storage accepts cycles, while `topologicalOrder()` reports a blocked node when
an acyclic schedule is impossible. State-machine users can traverse cyclic
graphs without requesting a topological order.

```cpp
demi::graph::DependencyGraph graph;
graph.addNode("source");
graph.addNode("result");
graph.addDependency("result", "source");
const auto order = graph.topologicalOrder();
```

Callers check registration results and translate graph failures into their own
diagnostics. Terrain owns pin types and required-input rules. Asset cooking
owns import hashes and publishes keys only after complete successful ordering.
Neither consumer imports the other subsystem. Traversal is iterative, with no
fixed content budget or recursive-chain stack limit.

## Value history

`EditorValueHistory<Value>` records reversible before/after values independently
of JSON, terrain, persistence and ImGui. Terrain graph drafts and versioned JSON
documents use the same implementation. A value needs equality, copying and a
nonthrowing swap so history application can finish allocations before mutation.

- `record(before, after)` ignores no-ops and preserves their Redo branch.
- `undo(value)` and `redo(value)` return Applied, Empty or Conflict.
- Conflict leaves the value and both history stacks untouched. The owning
  document decides whether to clear, retain or resolve obsolete history.
- Document identity, native validation, formatting-preserving Save, recovery
  and source-conflict checks remain outside the container.

## Palette cards

`drawEditorPaletteCard` consumes display metadata, an icon and optional typed
payload bytes. It does not depend on terrain, a project, a module catalog or
workspace selection. A card without a payload remains clickable and keyboard
activatable. The HUD/terrain module adapter supplies stable module IDs and the
existing payload type; another catalog can supply its own asset/reference type.

Payload interpretation, allowed destinations and authored mutations belong to
the destination. The widget never guesses an asset kind or writes a document.

## Color controls

`drawEditorColorControl` edits float RGBA values and supports precise channels
through explicit options. Terrain, HUD, reflected/script component properties
and materials use it. Source encoding and allowed numeric domains belong to
their field adapters, not the shared widget. Rendering a control must not
materialize defaults, round values, or change byte/hex/float source encodings.

## Existing shared owners

Graph canvas zoom/coordinate transforms, docking visibility/focus, document
sessions, clipboard envelopes, source-preserving JSON writes and window-only
capture already have independent owners. They remain reusable without moving
terrain evaluation, biome rules, erosion or water generation into UI utilities.

## Qualification

The Release editor and affected targets built successfully. Twelve selected
CTest checks passed in 2.12 seconds: dependency graph, value history, palette
card, color control, asset streaming, terrain graph, graph draft document,
specialized document, graph UI/settings, drag authoring and Shell docking.
This is a scoped gate, not a full repository run.

Coverage includes iterative long dependency chains, cycle detection, active-output
reachability, failed cook publication, history conflicts and Redo preservation,
non-terrain material-card payload bytes, independent color widgets, HUD hex
decoding and native float color bindings. Existing terrain and document
transactions remain qualified through their adapter tests.

`terrain_graph_3d` and `minimal_3d` validate without diagnostics. The example
probe follows the authored resolution and active branches rather than freezing
the user's editable scene at a particular sample grid. `git diff --check` is
clean. A twelve-frame native SDL3/Vulkan editor startup smoke passed on
`minimal_3d` with isolated per-user state. Website templates were not rendered
or deployed; no new Lua API,
visual-scripting runtime or platform capability is claimed.
