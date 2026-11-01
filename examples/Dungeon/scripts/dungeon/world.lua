local Entity = require("demi.entity")
local Transform = require("demi.transform2d")
local Rigidbody = require("demi.physics.rigidbody2d")
local Physics = require("demi.physics.query2d")
local Navigation = require("demi.navigation")
local Floors = require("dungeon.floors")
local Body = require("dungeon.body")
local Items = require("dungeon.items")
local CombatView = require("dungeon.combat_view")
local EncounterView = require("dungeon.encounter_view")
local ExplorationView = require("dungeon.exploration_view")

local World = {}
World.__index = World

function World.new(generation)
  return setmetatable({ prefix = "dungeon_" .. generation .. "_", ids = {}, serial = 0 }, World)
end

function World:id(name)
  return self.prefix .. name
end

function World:create(name, components)
  local id = self:id(name)
  assert(Entity.create(id, { components = components }), "Cannot create " .. id)
  self.ids[#self.ids + 1] = id
  return id
end

function World:sprite(name, x, y, sprite, extra)
  local components = { Transform2D = { position = { x, y } }, Sprite = sprite }
  for key, value in pairs(extra or {}) do
    components[key] = value
  end
  return self:create(name, components)
end

function World:actor(actor)
  local scale = Body.scale(actor)
  local size, pivot = Body.sprite(actor)
  self:create(actor.id, {
    Transform2D = { position = { actor.x, actor.y } },
    Sprite = {
      texture = "asset://dungeon/" .. (actor.kind or "warden"),
      size = size,
      pivot = pivot,
      sorting_order = 100,
    },
    SpriteAnimator2D = {
      frame_size = { 32, 48 },
      clip = "idle",
      clips = {
        idle = { start_frame = 0, frame_count = 1, fps = 1, loop = true },
        walk = { start_frame = 0, frame_count = 2, fps = 8, loop = true },
      },
    },
    Rigidbody2D = { body_type = "kinematic", gravity_scale = 0, lock_rotation = true },
    BoxCollider2D = {
      size = { actor.radius * 2, actor.radius * 2 },
      layer = "actor",
      debug_visible = false,
    },
  })
  CombatView.create(self, actor)
  self:sprite(actor.id .. "_shadow", actor.x, actor.y - 0.1, {
    shape = "circle",
    size = { scale * 0.95, scale * 0.36 },
    color = { 0.025, 0.025, 0.05, 0.65 },
    sorting_order = 5,
  })
  if actor.kind then
    self:sprite(actor.id .. "_warning", actor.x, actor.y, {
      shape = "circle",
      size = { scale * 1.4, scale * 0.65 },
      color = { 1, 0.3, 0.25, 0 },
      sorting_order = 6,
    })
    self:sprite(
      actor.id .. "_health",
      actor.x,
      actor.y + scale * 1.35,
      { size = { scale, 0.08 }, color = { 0.95, 0.3, 0.35, 1 }, sorting_order = 1000 }
    )
  end
end

function World:build(run)
  local floor = run.floor
  self.navigation = assert(Navigation.create_grid(floor.width, floor.height, 1, -0.5, -0.5))
  -- Static tiles and wall collision come from this floor's authored Tilemap2D.
  for y = 0, floor.height - 1 do
    for x = 0, floor.width - 1 do
      self.navigation:set_blocked(x, y, not Floors.walkable(floor, x, y))
    end
  end
  for number, room in ipairs(floor.rooms) do
    if room[3] - room[1] >= 6 then
      self:sprite(
        "banner_" .. number,
        (room[1] + room[3]) / 2,
        room[4] + 0.15,
        { texture = "asset://dungeon/banner", size = { 0.8, 1.6 }, sorting_order = 15 }
      )
    end
    for side = 0, 1 do
      local x = side == 0 and room[1] + 1 or room[3] - 1
      self:sprite(
        "torch_" .. number .. "_" .. side,
        x,
        room[4] + 0.45,
        { texture = "asset://dungeon/torch", size = { 1, 1 }, sorting_order = 20 },
        {
          SpriteAnimator2D = {
            frame_size = { 16, 16 },
            clip = "burn",
            clips = { burn = { start_frame = 0, frame_count = 8, fps = 10, loop = true } },
          },
        }
      )
    end
  end
  if floor.index == 2 then
    for y = 8, 17 do
      for x = 15, 22 do
        if not Floors.walkable(floor, x, y) then
          self:sprite(
            "shelf_" .. x .. "_" .. y,
            x,
            y,
            { texture = "asset://dungeon/bookshelf", size = { 1, 1 }, sorting_order = -10 }
          )
        end
      end
    end
  end
  self:sprite("stairs", floor.exit[1], floor.exit[2], {
    texture = "asset://dungeon/stairs",
    source_position = { 64, 0 },
    source_size = { 64, 112 },
    size = { 2, 3.5 },
    sorting_order = 0,
    color = { 0.4, 0.4, 0.5, 1 },
  })
  EncounterView.create(self, run)
  for _, trap in ipairs(floor.traps) do
    for y = math.ceil(trap.y - trap.height / 2), math.floor(trap.y + trap.height / 2) do
      for x = math.ceil(trap.x - trap.width / 2), math.floor(trap.x + trap.width / 2) do
        self.navigation:set_cost(x, y, 4)
      end
    end
  end
  for _, region in ipairs(floor.water) do
    self:sprite("cistern_" .. region[1], (region[1] + region[3]) / 2, (region[2] + region[4]) / 2, {
      texture = "asset://dungeon/water",
      source_position = { 0, 0 },
      source_size = { 32, 32 },
      size = { region[3] - region[1] + 1, region[4] - region[2] + 1 },
      sorting_order = -5,
    })
  end
  self:actor(run.player)
  for _, enemy in ipairs(run.enemies) do
    self:actor(enemy)
  end
  for _, pickup in ipairs(run.pickups) do
    self:pickup(pickup)
  end
  for _, chest in ipairs(run.chests) do
    self:sprite(
      chest.id,
      chest.x,
      chest.y,
      { texture = "asset://dungeon/chest", size = { 1, 1 }, sorting_order = 15 }
    )
  end
  self:sprite("merchant", run.merchant.x, run.merchant.y, {
    texture = "asset://dungeon/acolyte",
    source_position = { 0, 0 },
    source_size = { 32, 48 },
    size = { 1, 1.5 },
    pivot = { 0.5, 0.85 },
    color = { 0.6, 1, 0.85, 1 },
    sorting_order = 500 - run.merchant.y * 10,
  })
  ExplorationView.create(self, run)
  self:sync_gates(run)
  Entity.set_field("dungeon_camera", "Camera2D", "target", self:id("player"))
  Entity.set_field("dungeon_camera", "Camera2D", "follow_speed", 7)
  Transform.set_position("dungeon_camera", run.player.x, run.player.y)
end

function World:pickup(pickup)
  local item = Items[pickup.item]
  self:sprite(pickup.id, pickup.x, pickup.y, {
    texture = item.texture or "asset://dungeon/buttons",
    source_position = item.icon,
    source_size = { item.icon_size or 24, item.icon_size or 24 },
    size = { 0.65, 0.65 },
    sorting_order = 20,
  })
end

function World:sync_gates(run)
  ExplorationView.sync_gates(self, run)
end

function World:ready()
  return Entity.exists(self:id("player"))
end

function World:move(actor, dx, dy)
  local x, y = Rigidbody.move_and_slide(self:id(actor.id), dx, dy)
  assert(x, "Missing kinematic dungeon actor: " .. actor.id)
  actor.x, actor.y = Transform.get_position(self:id(actor.id))
end

function World:wall_distance(x, y, dx, dy, distance)
  if distance < 0.0001 then
    return nil
  end
  local hit = Physics.raycast(x, y, dx, dy, distance, "wall")
  return hit and hit.distance or nil
end

function World:path(x, y, target_x, target_y)
  return self.navigation:path(
    math.floor(x + 0.5),
    math.floor(y + 0.5),
    math.floor(target_x + 0.5),
    math.floor(target_y + 0.5),
    false
  )
end

function World:destroy()
  Entity.destroy_many(self.ids)
  self.ids = {}
  if self.navigation then
    self.navigation:clear()
  end
end

return World
