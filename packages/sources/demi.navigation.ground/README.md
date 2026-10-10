# Ground Navigation

Terrain-backed navigation and collision-aware agents for lightweight 3D games.
Each surface owns an independent native A* grid through `demi.navigation`.
Movement uses `CharacterController3D`, including its collision, slope and gravity
rules. Game scripts choose jobs and priorities.

```lua
local Ground = require("demi.navigation.ground")
local Walker = {}
function Walker:on_start()
  self.surface = Ground.create({terrain="terrain", origin={0,0}, columns=64,
    rows=64, cell_size=2, agent_radius=0.4, ignored_entity=self.entity_id})
  self.agent = self.surface:agent(self.entity_id, {speed=3})
end
function Walker:on_fixed_update(dt)
  if self.surface:update(64) and not self.requested then
    self.requested = true
    self.accepted, self.diagnostic = self.agent:move_to({30,0,40}, 3)
  end
  self.status = self.agent:update(dt)
end
function Walker:on_destroy()
  if self.agent then self.agent:stop() end
end
return Walker
```

This single-actor example owns its survey. For several actors in the same area,
share one surface and create an agent for each actor. Issue move_to when a goal
changes, not every frame. It returns success and a diagnostic; unavailable actors
and unreachable routes fail explicitly. Update returns idle, moving, arrived,
paused, surveying, unreachable or blocked. No failure teleports the actor.


The actor needs Transform3D, a supported collider and CharacterController3D.
The grid maps its axes to world X/Z; returned path points are `{x,y,z}` arrays.
One terrain surface per grid is supported; this is heightfield ground navigation,
not a multilevel navmesh, aerial navigation or crowd-avoidance system.

`surface:set_obstacles(boxes)` updates clearance around dynamic footprints;
each box contains min_x, max_x, min_z and max_z. Agents replan after revision
changes. Call `surface:invalidate()` after terrain or static collision changes;
agents stop until the incremental survey completes. `agent:stop()` cancels a route.
Disabled agents pause without advancing their path. Stalled controllers report
blocked instead of pretending to arrive. Reissue move_to to retry.

Survey defaults: max_slope=30 degrees, sample_height=500, sample_depth=1000.
Survey cells include center and four clearance samples. Bounds and sampling
heights should match your world. Physics remains authoritative between samples.

Run isolated package policy tests with `demi package test packages/sources/demi.navigation.ground`. Native A* and character collision are covered separately by engine and runtime tests.

Version 0.2 starts routes at the actor’s exact position, including when it is
leaving a blocked start cell. Set `face_movement=true` in agent options to turn
the actor toward its movement without tilting it; the default preserves rotation.
