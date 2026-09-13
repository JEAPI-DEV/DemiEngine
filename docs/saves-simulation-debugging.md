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
  "random_seed": 2026,
  "maximum_fixed_steps_per_frame": 4
}
```

`maximum_fixed_steps_per_frame` bounds catch-up work after a slow rendered
frame. Excess whole steps are reported as `Simulation.dropped_fixed_ms` and
discarded, leaving the interpolation remainder intact. This prevents a slow
physics frame from creating an unbounded catch-up spiral while every executed
simulation step still uses the authored fixed timestep.

Lua gameplay uses `Random.seed`, `Random.value`, `Random.range`, and
`Random.integer`. `Random.state` and `Random.restore` allow a save or test to
resume the same sequence.

For headless regression tests, pass a versioned `*.replay.json` fixture with
`--input-replay`. Each frame replaces the full input state, so playback does
not depend on input left over from an earlier frame.

Profiling can be enabled with `--profiler`, `DEMI_PROFILE=1`, or by passing
`--profile-report report.csv`. `--profiler` prints slow-frame details,
percentiles, and sorted scope totals to the terminal. The report aggregates
frame/update, Lua, rendering, physics, asset, and network scopes across the
run.

CSV exports also include `p50_ms`, `p95_ms`, `p99_ms`, and `samples`. Percentiles
use the most recent 600 calls per scope (nearest rank), not every call in the
session; totals, maxima, and call counts cover the entire session. Scopes called
multiple times per frame report call percentiles, not frame percentiles. Gauge
values are the latest value, not a peak or cumulative total. In particular, a
final zero `Simulation.dropped_fixed_ms` does not prove no time was dropped
earlier. Headless timings exclude GPU/presentation and advance fixed simulation
time independently of wall time; they are not desktop FPS qualification.

For visible measurements, `--profile-frames frames.csv` records
`frame,scope,total_ms,calls,gauge`. Scope names are CSV-escaped. Timed totals are
per application frame, so `Physics3D.step` sums any catch-up steps in that frame;
the aggregate report's percentiles are still per scope call. Frames with no
fixed step have zero physics work. Missing GPU samples mean unavailable/not fresh,
not zero GPU cost. Trace writing has its own observer overhead and is included
in the following start-to-start interval, not the earlier `Frame.total` scope.

Visible diagnostics include:

- `Frame.interval`: unclamped platform poll-to-poll interval, including pacing
  and work between measured update/render scopes; initial startup is omitted.
- `Render.prepare_cpu`: CPU-side render preparation before graphics advancement.
- `Graphics.frame_advance`: wall time inside `bgfx::frame`, including backpressure.
- `Graphics.render_thread`: backend render-thread elapsed time, not pure CPU busy
  time; it can include driver/presentation waits.
- `Graphics.wait_render`: backend-reported waiting for the render thread.
- `Graphics.wait_submit`: backend-reported render-thread waiting for the submitter.
- `Graphics.gpu`: GPU timestamp interval, with `Graphics.gpu_frame` identifying
  its originating backend frame. Duplicate/older samples are not counted again.
- `Graphics.submitted_frame`, `Graphics.backbuffer_width/height`, and
  `Graphics.vendor_id/device_id`: backend frame and device identity.
- `Graphics.gpu_timer_available`: valid backend GPU timestamps have been observed
  in this snapshot. Zero can mean startup, unsupported timing, or a skipped frame.
- `Simulation.scaled_input_ms`, `Simulation.advanced_ms`,
  `Simulation.backlog_before_steps_ms`, `Simulation.backlog_before_drop_ms`, and
  `Simulation.clamped_wall_ms`: input, executed fixed time, and lost/clamped time.
- `Window.capture_interrupted`, `Window.minimized`, `Window.focused`,
  `Window.refresh_hz`, and `Frame.delta_override`: capture-validity context.

Sum per-frame `Simulation.dropped_fixed_ms` and `Simulation.clamped_wall_ms` for
cumulative totals, excluding warmup explicitly when analyzing steady state.
GPU/backend samples can lag the application frame. Do not add their percentiles
to current CPU scopes or treat backend waits as a separately measured compositor
presentation latency. The engine does not insert GPU readback waits or alter
VSync/frame limiting to collect these diagnostics.

Project debug overlays are configured under `debug`; a runtime invocation can
override them:

```sh
demi run --project game/demi.project.json \
  --debug-overlays colliders,contacts,grid,entity_ids,draw_order,ui_bounds,profiler
```

The same names are accepted as project booleans, with `profiler_hud` used for
the profiler overlay.
