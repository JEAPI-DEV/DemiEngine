local Entity = require("demi.entity")
local Sprite = require("demi.sprite2d")
local Transform = require("demi.transform2d")
local Body = require("dungeon.body")
local Melee = require("dungeon.melee")
local Inventory = require("dungeon.inventory")
local CombatView = {}

function CombatView.create(world, actor)
  world:sprite(actor.id .. "_weapon", actor.x, actor.y, {
    texture = "asset://dungeon/short_blade",
    size = { 1.55, 0.25 },
    pivot = { 0, 0.5 },
    sorting_order = 700,
  })
  for index = 0, 8 do
    world:sprite(
      actor.id .. "_arc_" .. index,
      actor.x,
      actor.y,
      { shape = "circle", size = { 0.18, 0.18 }, color = { 1, 0.7, 0.15, 0 }, sorting_order = 699 }
    )
  end
end

function CombatView.update(world, actor, cache, revealed)
  local weapon
  if not actor.kind then
    weapon = Inventory.stats(actor).weapon
  elseif actor.kind == "skirmisher" then
    weapon = "short_sword"
  elseif actor.kind == "sentinel" or actor.kind == "boss" then
    weapon = "long_sword"
  elseif actor.kind == "acolyte" then
    weapon = "oak_staff"
  end
  local id = world:id(actor.id .. "_weapon")
  local visible = revealed and actor.hp > 0 and weapon and weapon ~= "unarmed"
  Entity.set_enabled(id, visible or false)
  local attack = actor.melee
  local spec = attack and Melee.profiles[attack.profile]
  local x, y = Body.muzzle(actor)
  if visible then
    local texture = weapon == "short_sword" and "short_blade"
      or (
        weapon == "long_sword" and "long_blade"
        or (
          weapon == "frost_staff" and "held_frost_staff"
          or (weapon == "ember_staff" and "held_ember_staff" or "held_staff")
        )
      )
    if cache[actor.id] ~= texture then
      Entity.set_field(id, "Sprite", "texture", "asset://dungeon/" .. texture)
      cache[actor.id] = texture
    end
    local length = spec and spec.range or (weapon == "long_sword" and 2.3 or 1.25)
    local angle = attack and Melee.angle(attack) or math.atan(actor.aim_y or -0.5, actor.aim_x or 1)
    Sprite.set_size(id, length, weapon == "long_sword" and 0.32 or 0.24)
    Transform.set_position(id, x, y)
    Transform.set_rotation(id, angle)
    Sprite.set_color(
      id,
      1,
      attack and attack.phase == "windup" and 0.7 or 1,
      attack and attack.phase == "windup" and 0.3 or 1,
      1
    )
  end
  for index = 0, 8 do
    local marker = world:id(actor.id .. "_arc_" .. index)
    local show = revealed and actor.hp > 0 and attack and attack.phase ~= "recovery"
    if show then
      local angle = attack.angle - spec.arc / 2 + index / 8 * spec.arc
      Transform.set_position(
        marker,
        x + math.cos(angle) * spec.range,
        y + math.sin(angle) * spec.range
      )
      local active = attack.phase == "active"
      Sprite.set_color(marker, 1, active and 0.2 or 0.7, 0.15, 0.85)
    else
      Sprite.set_color(marker, 1, 0.7, 0.15, 0)
    end
  end
end

return CombatView
