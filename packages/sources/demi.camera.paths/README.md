# Camera paths

`demi.camera.path` plays authored camera positions and aim points over time.
The package depends only on Lua and returns values; the game applies them through
`demi.transform3d`. It owns no camera, input bindings, HUD, or global state.

```lua
local Path = require("demi.camera.path")
local Transform3D = require("demi.transform3d")
local path = Path.new({
  {position = {0, 8, -12}, target = {0, 2, 0}},
  {position = {12, 6, 0}, target = {0, 2, 0}},
  {position = {0, 8, 12}, target = {0, 2, 0}},
}, {duration = 18, loop = true})

local pose = path:update(dt)
Transform3D.set_position("camera", table.unpack(pose.position))
Transform3D.look_at("camera", table.unpack(pose.target))
```

Set the camera's `target_offset` to `[0,0,1]`: `look_at` aims local +Z. Use an
unparented camera when applying these world-space positions directly.

`duration` is the complete path length in seconds and must be positive and
finite. Keyframes contain numeric `position` and `target` triples and are copied
at creation. Each leg has equal duration. Catmull-Rom interpolation passes through
each keyframe with smooth tangents; it can overshoot between points. Author and
inspect clearance around terrain, buildings and sharp turns. This is authored
cinematic motion, not collision avoidance.

Loops include the last-to-first leg without repeating the first keyframe.
`loop=false` traverses only successive points, clamps time to the endpoints,
and stops playback at the final pose. Looping is the default.

- `update(dt)` advances by nonnegative seconds while `playing` and returns a pose.
- `sample(seconds)` returns a pose without changing playback.
- `seek(seconds)` changes elapsed time, preserving pause/play state.
- `pause()` and `play()` control playback; `restart()` seeks to zero and plays.

Poses include fresh `position` and `target` triples, one-based `segment`, elapsed
seconds and normalized `progress`. Loop time wraps, including negative seeks;
nonlooping time clamps. Sampling and seeking reject nonfinite input. Keep path
configuration fixed during playback; create another path to change its route.

See `examples/terrain_3d` in the engine checkout for a looping tour with chapter
labels and HUD controls. Run package tests with `demi package test` from this
source directory.
