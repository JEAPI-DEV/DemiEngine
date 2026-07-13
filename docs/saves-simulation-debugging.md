# Saves, Simulation, And Debugging

Milestone 5 adds a structured game-save boundary alongside the existing simple
settings API. `Save.write_state` requires four explicit tables: `game`,
`selected_entities`, `prefab_instances`, and `lua`. Only Lua data deliberately
placed in the `lua` table is persisted. Options carry `format_version`,
`autosave`, `sequence`, and `reason`; `Save.metadata` reads them back and
`Save.last_error` explains rejected or incompatible documents. Existing
`Save.register_migration` hooks also apply when structured state is read.

Projects can configure reproducible simulation:

```json
"simulation": {
  "fixed_timestep": 0.016666667,
  "random_seed": 2026
}
```

Lua gameplay uses `Random.seed`, `Random.value`, `Random.range`, and
`Random.integer`. `Random.state` and `Random.restore` allow a save or test to
resume the same sequence.

For headless regression tests, pass a versioned `*.replay.json` fixture with
`--input-replay`. Each frame replaces the full input state, so playback does
not depend on input left over from an earlier frame.

Profiling can be enabled with `DEMI_PROFILE=1` or by passing
`--profile-report report.csv`. The report aggregates frame/update, Lua,
rendering, physics, asset, and network scopes across the run. Project debug
overlays are configured under `debug`; a runtime invocation can override them:

```sh
demi run --project game/demi.project.json \
  --debug-overlays colliders,contacts,grid,entity_ids,draw_order,ui_bounds,profiler
```

The same names are accepted as project booleans, with `profiler_hud` used for
the profiler overlay.
