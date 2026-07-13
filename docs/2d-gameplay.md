# 2D Gameplay Foundation

Milestone 4 provides data-driven sprite animation, cameras, tilemaps, physics
queries, and a reusable Lua character controller.

## Sprite animation

`Sprite` supports `source_position`, `source_size`, world-space `size`, `pivot`, `flip_x`,
`flip_y`, `layer`, and `sorting_order`. Add `SpriteAnimator2D` beside it to
divide the texture into frames:

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

The source contains a texture ID, pixel tile size, map size, and flat,
row-major layers. Tile ID `0` is empty; positive IDs are one-based atlas
indices. Row zero is the top row. Layers may set `parallax`, `opacity`,
`collision`, and `collision_layer`. Runtime rendering skips tiles outside the
view, and adjacent solid cells are merged into static collision runs.

```json
{
  "format_version": 1,
  "texture": "asset://tiles/world",
  "tile_size": [16, 16],
  "map_size": [3, 2],
  "layers": [
    {"name": "ground", "collision": true,
     "collision_layer": "platform", "tiles": [0, 0, 0, 1, 1, 1]}
  ]
}
```

Scene entities reference it with `Tilemap2D.asset` and set
`pixels_per_unit`. Their `Transform2D.position` is the map's lower-left world
origin.

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

`BoxCollider2D` and `CircleCollider2D` support triggers, named layers, and
lower-level `category_bits`/`mask_bits` overrides when no project layer is
declared. `DistanceJoint2D` connects its entity to `other_entity` with local
anchors, length, stiffness, and damping.

Lua queries include `Physics2D.overlap_box`, `overlap_circle`, `raycast`,
`contacts`, and `has_contact`. Trigger contacts emit `physics_trigger` once per
fixed step for each participant, with `entity_id`, `other_entity_id`,
`other_layer`, and contact normal fields.

## Character controller

`require("demi.character_controller_2d")` returns the reusable Lua controller.
It composes named move/jump actions, a ground raycast, rigidbody velocity, and
sprite animation/flip. Genre-specific rules remain in the game script. See
`examples/minimal_2d_networking/scripts/player.lua` for the platformer probe.
