local Entity = require("demi.entity")
local Sprite = require("demi.sprite2d")
local Hud = require("dungeon.ui")
local Encounters = require("dungeon.encounters")
local EncounterView = {}

function EncounterView.create(world, run)
  for _, seal in ipairs(run.seals) do
    world:sprite(
      seal.id,
      seal.x,
      seal.y,
      { shape = "circle", size = { 0.7, 0.7 }, color = { 0.7, 0.35, 1, 1 }, sorting_order = 30 }
    )
    world:sprite(
      seal.id .. "_rune",
      seal.x,
      seal.y,
      { shape = "triangle", size = { 0.45, 0.45 }, color = { 1, 0.9, 0.6, 1 }, sorting_order = 31 }
    )
  end
  for _, shrine in ipairs(run.shrines) do
    world:sprite(shrine.id, shrine.x, shrine.y, {
      texture = "asset://dungeon/hearts",
      source_position = { 0, 0 },
      source_size = { 16, 16 },
      size = { 1, 1 },
      sorting_order = 30,
    })
  end
  for index, trap in ipairs(run.floor.traps) do
    world:sprite(
      "trap_" .. index,
      trap.x,
      trap.y,
      { size = { trap.width, trap.height }, color = { 0.4, 0.1, 0.1, 0.2 }, sorting_order = 2 }
    )
    for point = 1, 5 do
      local x = trap.x + (point / 6 - 0.5) * trap.width
      world:sprite("spike_" .. index .. "_" .. point, x, trap.y, {
        shape = "triangle",
        size = { 0.25, 0.45 },
        color = { 0.5, 0.55, 0.65, 1 },
        sorting_order = 3,
      })
    end
  end
end

function EncounterView.update(world, run)
  for _, seal in ipairs(run.seals) do
    local guarded = Encounters.guarded(run, seal)
    Sprite.set_color(world:id(seal.id), seal.claimed and 0.3 or 0.7, guarded and 0.25 or 0.9, 1, 1)
    Entity.set_enabled(world:id(seal.id .. "_rune"), not seal.claimed)
    Hud.set_background_color("map_" .. seal.id, seal.claimed and 0.3 or 1, 0.7, 1, 1)
  end
  for _, shrine in ipairs(run.shrines) do
    Sprite.set_color(world:id(shrine.id), shrine.used and 0.3 or 1, shrine.used and 0.3 or 1, 1, 1)
  end
  for index, trap in ipairs(run.floor.traps) do
    local phase = Encounters.trap_phase(run.floor_time, trap)
    local active = phase == "active"
    Sprite.set_color(
      world:id("trap_" .. index),
      1,
      phase == "warning" and 0.65 or 0.15,
      0.1,
      phase == "safe" and 0.12 or 0.6
    )
    for point = 1, 5 do
      Sprite.set_size(world:id("spike_" .. index .. "_" .. point), 0.25, active and 0.7 or 0.15)
    end
  end
end

return EncounterView
