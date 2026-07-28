# Production 2D Gameplay Foundation

The 2D runtime provides shared rendering, physics, tilemap, navigation, and
controller APIs. Game scripts choose policy such as health, damage, teams, and
movement tuning.

## Sprite animation

`Sprite` supports pixel or normalized source rectangles, world-space `size`,
`pivot`, flips, color, `layer`, `sorting_order`, and an optional `material`
asset reference. `nine_slice` contains `[[left, top], [right, bottom]]` pixel
borders. `mask_offset` and `mask_size` clip a sprite in world space. Generic
runtime component mutation can change layer, order, and material without
recreating the entity. Add `SpriteAnimator2D` beside it to divide the texture
into frames:

```json
"SpriteAnimator2D": {
  "frame_size": [16, 16],
  "clip": "idle",
  "clips": {
    "idle": {"start_frame": 0, "frame_count": 1, "fps": 1},
    "run": {
      "start_frame": 1,
      "frame_count": 4,
      "fps": 10,
      "events": [{"frame": 2, "name": "footstep"}]
    }
  }
}
```

Lua controls playback with `Sprite2D.play_animation`, `pause_animation`,
`resume_animation`, `current_animation`, and `set_flip`. Runtime presentation
can update the world-space sprite dimensions with `Sprite2D.set_size`. Authored frame events
are emitted through `Events` as `sprite_animation`; its payload contains
`entity_id`, `clip`, `name`, and the clip-local `frame`.

## Shared animation states and timed collision

`AnimationStateMachine` adds named states, deterministic transitions,
parameters, triggers, durations, and timed events without coupling gameplay
rules to a renderer. A state can select a `sprite_clip`, a `model_clip`, or
both. Lua uses `Animation.state`, `Animation.play`, `Animation.set_number`,
`Animation.set_bool`, and `Animation.trigger`. Timed state events are emitted
as `animation_event` with `entity_id`, `state`, and `name`.

`AnimationCollision2D` describes named receiver shapes and state-time overlap
windows. Layers and masks select compatible shapes. The runtime emits one
`animation_collision` event per source window and receiver overlap, containing
`source_id`, `target_id`, `window`, and `receiver`. It intentionally has no
damage, health, teams, hitstun, or knockback; games assign meaning to the
neutral overlap in Lua. The fighting-game example demonstrates that boundary.

Optional Lua helpers `demi.input_buffer` and `demi.command_recognizer` provide
time-window buffering and ordered command matching without imposing a control
scheme on the runtime.

`Camera2D` supports `target`, `follow_speed`, `follow_offset`, `bounds_min`,
and `bounds_max`. A follow speed of zero snaps to the target. Bounds constrain
the camera center.

Renderables are ordered by `sorting_order`, then layer name, then isometric
depth. Use numeric orders for explicit foreground/background placement.

## Tilemaps

A `Tilemap2D` asset manifest points to a versioned tilemap source:

```json
{"format_version": 1, "id": "asset://tilemaps/level",
 "type": "Tilemap2D", "source": "level.tilemap.json"}
```

The source contains one or more tilesets, a pixel tile size, map size, and
flat, row-major layers. Tile ID `0` is empty; positive IDs are global,
one-based tile IDs. Row zero is the top row. Layers may set `parallax`,
`opacity`, `collision`, `collision_layer`, `navigation_blocked`, and
`navigation_cost`. Runtime rendering skips off-screen cells, chooses the
tileset with the nearest `first_tile`, advances authored tile animations, and
merges adjacent collision cells into static runs.

```json
{
  "format_version": 1,
  "tile_size": [16, 16],
  "map_size": [3, 2],
  "tilesets": [
    {"texture": "asset://tiles/world", "first_tile": 1}
  ],
  "animations": {
    "2": [{"tile": 2, "duration": 0.12}, {"tile": 3, "duration": 0.12}]
  },
  "object_layers": [
    {"name": "spawns", "objects": [
      {"id": "player", "type": "spawn", "position": [16, 16],
       "size": [16, 16]}
    ]}
  ],
  "layers": [
    {"name": "ground", "collision": true,
     "collision_layer": "platform", "navigation_blocked": true,
     "tiles": [0, 0, 0, 1, 1, 1]}
  ]
}
```

Scene entities reference it with `Tilemap2D.asset` and set
`pixels_per_unit`. Their `Transform2D.position` is the map's lower-left world
origin. Lua can call `Tilemap2D.get_tile`, `set_tile`, `clear_overrides`,
`objects`, and `bake_navigation`. Once baked, edits refresh rendering,
collision generation, and the shared navigation grid together. Navigation
baking currently requires square world-space cells.

## Physics

Projects can declare a named collision matrix. Names are assigned stable bits
in sorted order, with a maximum of 16 layers:

```json
"physics": {
  "layers": {
    "platform": ["player"],
    "player": ["platform", "pickup"],
    "pickup": ["player"]
  }
}
```

`BoxCollider2D`, `CircleCollider2D`, `CapsuleCollider2D`,
`PolygonCollider2D`, and `EdgeCollider2D` support triggers, named layers,
friction, restitution, density, and lower-level
`category_bits`/`mask_bits`. `EdgeCollider2D.points` represents an open chain;
`loop: true` closes it. Polygon fixtures accept the Box2D convex-vertex limit.

`Rigidbody2D` supports dynamic, kinematic, and static bodies, continuous
collision, linear/angular damping, angular velocity, rotation locking,
sleeping, and runtime enable. Lua exposes velocity, impulse, force, torque,
angular velocity, awake/enable state, and kinematic target movement.
`Rigidbody2D.move_and_slide` applies a motion vector to a kinematic collider,
stops on static colliders, preserves tangent motion, and returns the applied
vector.
`DistanceJoint2D` remains the focused spring-distance component. `Joint2D`
adds revolute, prismatic, weld, rope, and motor configurations.

Lua queries include `Physics2D.overlap_box_all`, `overlap_circle_all`,
`raycast`, `contacts`, and `has_contact`. Rich hits contain `entity_id`,
`layer`, `point`, `normal`, `distance`, and `fraction`.

Each participant receives `physics_collision_enter`, `_stay`, and `_exit`, or
the corresponding `physics_trigger_*` event. `physics_contact` receives both
kinds. Payloads contain participant IDs/layers, phase, contact point, normal,
normal impulse, and `is_trigger`. Exit events preserve the final known contact
geometry.

## Navigation and controllers

`Navigation2D.configure` creates an axis-aligned grid. Scripts can update
blockers and per-cell costs, request four- or eight-direction A* paths, and
convert between cells and world centers. Isometric games project their input
through the isometric adapter before using the same grid.

Reusable Lua helpers cover ordinary movement without embedding game rules:

- `demi.character_controller_2d`: platform movement, grounding, jump, flip,
  and animation selection;
- `demi.top_down_controller_2d`: normalized action movement with optional
  facing/flip;
- `demi.click_move_controller_2d`: navigation requests and waypoint following.
