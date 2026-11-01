local Entity = require("demi.entity")
local Sprite = require("demi.sprite2d")
local Hud = require("dungeon.ui")
local Exploration = require("dungeon.exploration")
local ExplorationView = {}

function ExplorationView.create(world, run)
  world.gate_states = {}
  world.fog_color = Entity.get_config("dungeon_camera", "Camera2D", "clear_color")
    or { 0.025, 0.028, 0.05, 1 }
  for _, gate in ipairs(run.gates) do
    world:create("gate_" .. gate.id, {
      Transform2D = { position = { gate.x, gate.y } },
      BoxCollider2D = { size = { gate.width, gate.height }, layer = "wall", debug_visible = false },
    })
    for y = math.ceil(gate.y - gate.height / 2), math.floor(gate.y + gate.height / 2) do
      for x = math.ceil(gate.x - gate.width / 2), math.floor(gate.x + gate.width / 2) do
        world:sprite("gate_" .. gate.id .. "_" .. x .. "_" .. y, x, y, {
          texture = gate.secret and "asset://dungeon/walls" or "asset://dungeon/stone",
          source_position = gate.secret and { 16, 32 } or { 80, 256 },
          source_size = { 16, 16 },
          size = { 1, 1 },
          sorting_order = 10,
        })
      end
    end
  end
  for _, gate in ipairs(run.gates) do
    if gate.secret then
      world:sprite("crack_" .. gate.id, gate.x, gate.y, {
        texture = "asset://dungeon/stone",
        source_position = { 128, 192 },
        source_size = { 16, 16 },
        size = { 0.65, 0.65 },
        color = { 0.65, 0.75, 0.95, 0.8 },
        sorting_order = 12,
      })
    end
  end
  for _, lever in ipairs(run.levers) do
    world:sprite(
      lever.id,
      lever.x,
      lever.y,
      { texture = "asset://dungeon/lever", size = { 0.8, 0.8 }, sorting_order = 30 }
    )
  end
  for _, clue in ipairs(run.clues) do
    world:sprite(
      clue.id,
      clue.x,
      clue.y,
      { texture = "asset://dungeon/note", size = { 0.65, 0.65 }, sorting_order = 30 }
    )
  end
  for _, prop in ipairs(run.breakables) do
    world:sprite(prop.id, prop.x, prop.y, {
      texture = "asset://dungeon/stone",
      source_position = { 32, 192 },
      source_size = { 16, 16 },
      size = { 0.9, 0.9 },
      sorting_order = 20,
    })
  end
  for y = 0, run.floor.height - 1 do
    for x = 0, run.floor.width - 1 do
      world:sprite(
        "fog_" .. x .. "_" .. y,
        x,
        y,
        { size = { 1, 1 }, color = world.fog_color, sorting_order = 2000 }
      )
    end
  end
end

function ExplorationView.sync_gates(world, run)
  for _, gate in ipairs(run.gates) do
    if world.gate_states[gate.id] ~= gate.open then
      world.gate_states[gate.id] = gate.open
      Entity.set_enabled(world:id("gate_" .. gate.id), not gate.open)
      if gate.secret then
        Entity.set_enabled(world:id("crack_" .. gate.id), not gate.open)
      end
      for y = math.ceil(gate.y - gate.height / 2), math.floor(gate.y + gate.height / 2) do
        for x = math.ceil(gate.x - gate.width / 2), math.floor(gate.x + gate.width / 2) do
          world.navigation:set_blocked(x, y, not gate.open)
          Entity.set_enabled(world:id("gate_" .. gate.id .. "_" .. x .. "_" .. y), not gate.open)
        end
      end
    end
  end
end

function ExplorationView.update(view, run)
  local world = view.world
  for _, prop in ipairs(run.breakables) do
    Entity.set_enabled(world:id(prop.id), prop.hp > 0)
  end
  for _, lever in ipairs(run.levers) do
    Sprite.set_color(world:id(lever.id), lever.used and 0.4 or 1, 1, lever.used and 0.4 or 1, 1)
  end
  if view.visibility_revision == run.visibility_revision then
    return
  end
  view.visibility_revision = run.visibility_revision
  view.map_seen = view.map_seen or {}
  for y = 0, run.floor.height - 1 do
    for x = 0, run.floor.width - 1 do
      local seen = Exploration.discovered(run, x, y)
      local visible = Exploration.visible(run, x, y)
      Sprite.set_color(
        world:id("fog_" .. x .. "_" .. y),
        world.fog_color[1],
        world.fog_color[2],
        world.fog_color[3],
        visible and 0 or (seen and 0.55 or 1)
      )
      local key = Exploration.key(x, y)
      if view.map_seen[key] ~= seen then
        Hud.set_visible("map_" .. x .. "_" .. y, seen)
        view.map_seen[key] = seen
      end
    end
  end
  Hud.set_visible("map_exit", Exploration.discovered(run, run.floor.exit[1], run.floor.exit[2]))
  for _, seal in ipairs(run.seals) do
    Hud.set_visible("map_" .. seal.id, Exploration.discovered(run, seal.x, seal.y))
  end
end

return ExplorationView
