# 2D movement controllers

Package: `demi.gameplay.controllers`.

Provides input-to-intent functions and a separate 2D character helper that
calls engine physics and sprite services. Exports `demi.gameplay.controllers`
and `demi.gameplay.character_controller_2d`, with no package dependencies or
event bus. See [package installation](../../README.md).

The intent module works on plain tables:

```lua
local Controllers = require("demi.gameplay.controllers")

local state = { grounded = true }
local config = { coyote_time = 0.1, jump_buffer = 0.1 }
local intent
intent, state = Controllers.platform({ x = 1, jump = true, dt = 1 / 60 }, state, config)
assert(intent.jump)
local movement = Controllers.top_down({ x = 1, y = 1 })
assert(movement.x < 1)
```

Keep the returned platform state between calls, refresh `state.grounded` from
your collision check, and supply `dt` in seconds. Both timing values must be
positive for a jump to trigger; omitted values default to zero. `input.jump`
should represent a press, for example `Input.pressed("jump")`. The module
does not read input or apply movement itself.

`top_down` clamps vectors longer than one and carries optional `aim_x/aim_y`.
`click_to_move` returns `{ target_x, target_y }` only when `clicked` is true;
it does not find or follow a path. `isometric` returns `grid_x = right - left`
and `grid_y = down - up`, without normalization or world projection.

For direct platform movement, an engine script can use the character helper.
This requires an existing `player` with Transform2D, Rigidbody2D, a collider,
and sprite animation clips named `idle`/`run`, plus `move`/`jump` input actions
and a `platform` collision layer:

```lua
local Character = require("demi.gameplay.character_controller_2d")
local controller = Character.new({ move_speed = 5, jump_speed = 9 })
local Player = {}

function Player:on_update(dt)
  controller:update_horizontal("player")
  controller:try_jump("player", controller:is_grounded("player"))
end

return Player
```

`is_grounded` casts downward from the entity position, ignoring that entity;
adjust `ground_distance` (default 0.56) and `ground_layer` for your collider.
`update_horizontal` reads `Input.value`, sets horizontal velocity, flips the
sprite when enabled, and selects idle/run animation. `try_jump` reads
`Input.pressed` and sets vertical velocity only when passed a grounded state.
Action names, speeds, and animation names are constructor options.

The character helper does not use the intent module's coyote/buffer state.
Neither module configures input bindings, creates entities, supplies navigation,
or implements a 3D controller. The game owns state and update timing; there
are no events to flush or subscriptions to clean up.

From the repository root, test with:

```sh
./build/linux-debug/demi package test packages/sources/demi.gameplay.controllers
```
