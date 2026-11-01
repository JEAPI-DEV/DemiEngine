local Entity = require("demi.entity")
local Sprite = require("demi.sprite2d")
local Transform = require("demi.transform2d")
local Hud = require("dungeon.ui")
local Audio = require("demi.audio")
local Floors = require("dungeon.floors")
local Run = require("dungeon.run")
local Inventory = require("dungeon.inventory")
local Persistence = require("dungeon.persistence")
local Body = require("dungeon.body")
local Items = require("dungeon.items")
local CombatView = require("dungeon.combat_view")
local EncounterView = require("dungeon.encounter_view")
local Encounters = require("dungeon.encounters")
local Exploration = require("dungeon.exploration")
local ExplorationView = require("dungeon.exploration_view")
local Magic = require("dungeon.magic")

local View = {}
View.__index = View

function View.new(world, run)
  local self = setmetatable(
    { world = world, bolts = {}, poses = {}, weapons = {}, pickups = {}, hud_time = 0 },
    View
  )
  for _, pickup in ipairs(run.pickups) do
    self.pickups[pickup.id] = true
  end
  Hud.clear_children("minimap")
  for y = 0, run.floor.height - 1 do
    for x = 0, run.floor.width - 1 do
      if Floors.walkable(run.floor, x, y) then
        assert(Hud.create("minimap", {
          id = "map_" .. x .. "_" .. y,
          type = "rect",
          position = { x * 3, (25 - y) * 3 },
          size = { 3, 3 },
          background_color = "#192435",
          visible = false,
        }))
      end
    end
  end
  assert(Hud.create("minimap", {
    id = "map_exit",
    type = "rect",
    position = { run.floor.exit[1] * 3, (25 - run.floor.exit[2]) * 3 },
    size = { 4, 4 },
    background_color = "#edbc67",
  }))
  assert(
    Hud.create(
      "minimap",
      { id = "map_player", type = "rect", size = { 4, 4 }, background_color = "#8cffff" }
    )
  )
  for index, _ in ipairs(run.enemies) do
    assert(
      Hud.create(
        "minimap",
        { id = "map_enemy_" .. index, type = "rect", size = { 3, 3 }, background_color = "#ed7389" }
      )
    )
  end
  for _, seal in ipairs(run.seals) do
    assert(Hud.create("minimap", {
      id = "map_" .. seal.id,
      type = "rect",
      position = { seal.x * 3, (25 - seal.y) * 3 },
      size = { 4, 4 },
      background_color = "#df98ff",
    }))
  end
  return self
end

function View:actor(actor, time, visible)
  local world = self.world
  local alive = actor.hp > 0
  Entity.set_enabled(world:id(actor.id), alive)
  Entity.set_enabled(world:id(actor.id .. "_shadow"), alive)
  if not alive then
    if actor.kind then
      Entity.set_enabled(world:id(actor.id .. "_warning"), false)
      Entity.set_enabled(world:id(actor.id .. "_health"), false)
    end
    return
  end
  local scale = Body.scale(actor)
  local art = world:id(actor.id)
  local previous = self.poses[actor.id]
  local moving = previous
    and (math.abs(actor.x - previous.x) + math.abs(actor.y - previous.y)) > 0.001
  local clip = moving and "walk" or "idle"
  if not previous or previous.clip ~= clip then
    Sprite.play_animation(art, clip)
  end
  self.poses[actor.id] = { x = actor.x, y = actor.y, clip = clip }
  Sprite.set_sorting_order(art, 500 - math.floor(actor.y * 10))
  local facing = actor.aim_x
  Sprite.set_flip(art, (facing or 1) < 0, false)
  local flash = actor.flash > 0
  local alpha = (actor.invulnerable or 0) > 0 and (math.floor(time * 18) % 2 == 0) and 0.5 or 1
  alpha = visible and alpha or 0
  Sprite.set_color(art, 1, flash and 0.3 or 1, flash and 0.35 or 1, alpha)
  Sprite.set_color(world:id(actor.id .. "_shadow"), 0.025, 0.025, 0.05, visible and 0.65 or 0)
  Transform.set_position(world:id(actor.id .. "_shadow"), actor.x, actor.y - 0.1)
  if actor.kind then
    local warning = world:id(actor.id .. "_warning")
    Transform.set_position(warning, actor.x, actor.y)
    Sprite.set_color(
      warning,
      1,
      0.28,
      0.18,
      visible and (actor.windup > 0 or (actor.charge_windup or 0) > 0) and 0.65 or 0
    )
    local health = world:id(actor.id .. "_health")
    Entity.set_enabled(health, visible)
    Transform.set_position(health, actor.x, actor.y + scale * 1.35)
    Sprite.set_size(health, scale * actor.hp / actor.max_hp, 0.08)
  end
end

function View:projectiles(run)
  local present = {}
  for _, bolt in ipairs(run.projectiles) do
    present[bolt.id] = true
    local id = self.world:id(bolt.id)
    if not self.bolts[bolt.id] then
      self.world:sprite(bolt.id, bolt.x, bolt.y, {
        shape = "circle",
        size = { 0.25, 0.25 },
        color = bolt.hostile and { 1, 0.36, 0.27, 1 }
          or (bolt.spell and Magic.profiles[bolt.spell].color or { 0.55, 1, 1, 1 }),
        sorting_order = 900,
      })
    else
      Transform.set_position(id, bolt.x, bolt.y)
    end
  end
  for name, _ in pairs(self.bolts) do
    if not present[name] then
      Entity.destroy(self.world:id(name))
    end
  end
  self.bolts = present
end

function View:hud(run, paused)
  local player = run.player
  local remaining = Run.remaining(run)
  local playing = run.phase == "playing" or run.phase == "upgrade"
  for _, id in ipairs({
    "status_cluster",
    "minimap_cluster",
    "quickslots",
    "pause_button",
    "journal_button",
    "inventory_button",
  }) do
    Hud.set_visible(id, playing)
  end
  Hud.set_text("floor", run.floor.index == 4 and "THE THRONE" or ("FLOOR " .. run.floor.index))
  Hud.set_text("character_name", "WARDEN  LV." .. player.level)
  Hud.set_text("health_text", string.format("%.1f / %d", player.hp, player.max_hp))
  Hud.set_size("health_fill", 209 * player.hp / player.max_hp, 12)
  local stats = Inventory.stats(player)
  if self.weapon_badge ~= stats.weapon then
    local item = Items[stats.weapon]
    if item then
      Hud.set_image(
        "staff_badge",
        item.texture or "asset://dungeon/buttons",
        item.icon[1],
        item.icon[2],
        item.icon_size or 24,
        item.icon_size or 24
      )
    end
    self.weapon_badge = stats.weapon
  end
  Hud.set_size("dash_fill", 32 * math.max(0, 1 - player.dash_cooldown / stats.dash_delay), 3)
  Hud.set_text("mana_text", string.format("%d / %d", player.mana, player.max_mana))
  Hud.set_size("mana_fill", 200 * player.mana / player.max_mana, 10)
  Hud.set_text("coins", string.format("x%03d", run.coins))
  Hud.set_text("heal_count", tostring(Inventory.count(player.inventory, "healing_potion")))
  Hud.set_text("mana_count", tostring(Inventory.count(player.inventory, "mana_potion")))
  if self.message ~= run.message then
    self.message = run.message
    self.message_until = run.time + 5
    Hud.set_text("message", run.message)
  end
  Hud.set_visible("message_panel", run.phase == "playing" and run.time < (self.message_until or 0))
  local room = Floors.room(run.floor, player.x, player.y)
  if self.room ~= room then
    self.room = room
    self.room_until = run.time + 3
    Hud.set_text("room_title", room ~= "Passage" and room or "")
  end
  Hud.set_visible(
    "room_title",
    run.phase == "playing" and run.floor.index ~= 4 and run.time < (self.room_until or 0)
  )
  local claimed = 0
  for _, seal in ipairs(run.seals) do
    if seal.claimed then
      claimed = claimed + 1
    end
  end
  local objective = Floors.room(run.floor, player.x, player.y)
    .. " / Find the stairs — fighting is optional"
  if
    (run.floor.exit[1] - player.x) ^ 2 + (run.floor.exit[2] - player.y) ^ 2 < 1.5 ^ 2
    and run.floor.index < 4
  then
    objective = "[E] Descend now — no enemy or sigil requirement"
  end
  if run.floor.index == 4 then
    objective = "ASHEN KING — counter in melee to restore mana; spells create openings"
  end
  for _, seal in ipairs(run.seals) do
    if not seal.claimed and (seal.x - player.x) ^ 2 + (seal.y - player.y) ^ 2 < 1.5 ^ 2 then
      objective = "[E] Take the optional sigil: souls and mana"
    end
  end
  for _, shrine in ipairs(run.shrines) do
    if not shrine.used and (shrine.x - player.x) ^ 2 + (shrine.y - player.y) ^ 2 < 1.5 ^ 2 then
      objective = "[E] Healing shrine — one blessing per expedition"
    end
  end
  for _, chest in ipairs(run.chests) do
    if
      not chest.opened
      and Exploration.gate_open(run, chest.gate)
      and (chest.x - player.x) ^ 2 + (chest.y - player.y) ^ 2 < 1.5 ^ 2
    then
      objective = "[E] Open treasure chest    /    [I] Equipment & inventory"
    end
  end
  if (run.merchant.x - player.x) ^ 2 + (run.merchant.y - player.y) ^ 2 < 1.5 ^ 2 then
    objective = "[E] Trade with the Wayfarer    /    Buy equipment, sell loot"
  end
  objective = Exploration.prompt(run) or objective
  if
    run.floor.index < 4
    and (run.floor.exit[1] - player.x) ^ 2 + (run.floor.exit[2] - player.y) ^ 2 < 1.5 ^ 2
  then
    objective = "[E] Descend now — no enemy or sigil requirement"
  end
  local context = objective:sub(1, 3) == "[E]" or Exploration.prompt(run) ~= nil
  Hud.set_visible("objective", context and run.phase == "playing")
  Hud.set_visible("interact_button", context and run.phase == "playing")
  Hud.set_text("objective", objective:gsub("%[E%] ", ""))
  Hud.set_position("map_player", player.x * 3, (25 - player.y) * 3)
  for index, enemy in ipairs(run.enemies) do
    Hud.set_visible(
      "map_enemy_" .. index,
      enemy.hp > 0 and Exploration.visible(run, enemy.x, enemy.y)
    )
    Hud.set_position("map_enemy_" .. index, enemy.x * 3, (25 - enemy.y) * 3)
  end
  local boss = run.floor.index == 4 and run.enemies[1]
  Hud.set_visible(
    "boss_panel",
    boss and run.phase == "playing" and Exploration.visible(run, boss.x, boss.y) or false
  )
  if boss then
    Hud.set_size("boss_fill", 284 * boss.hp / boss.max_hp, 5)
    Hud.set_text(
      "boss_name",
      boss.hp <= boss.max_hp / 2 and "THE ASHEN KING  /  ENRAGED" or "THE ASHEN KING"
    )
  end
  Hud.set_visible("overlay", paused or run.phase ~= "playing")
  Hud.set_visible("power_choice", run.phase == "upgrade")
  Hud.set_visible("vitality_choice", run.phase == "upgrade")
  Hud.set_visible("restart_button", run.phase == "dead" or run.phase == "victory")
  Hud.set_visible("resume_button", paused)
  Hud.set_visible("start_button", run.phase == "ready")
  Hud.set_visible("continue_button", run.phase == "ready")
  Hud.set_disabled("continue_button.hit", not Persistence.exists())
  Hud.set_visible("reset_button", paused)
  Hud.set_visible("save_button", paused)
  Hud.set_visible("title_button", paused)
  local title, subtitle, detail
  if run.phase == "ready" then
    title = "THE HOLLOW CROWN"
    subtitle = "Three floors. One fallen king. Claim the crown."
    detail =
      "Find the stairs or explore for secrets. Melee restores mana. T uses a flask. J opens your notes."
  elseif paused then
    title, subtitle, detail = "A MOMENT OF SILENCE", "The dungeon can wait.", run.message
  elseif run.phase == "upgrade" then
    title, subtitle = "THE WAY DOWN", "Choose a blessing before you descend."
    detail = "+4 vitality restored with either blessing"
  elseif run.phase == "dead" then
    title, subtitle = "THE HOLLOW CLAIMS YOU", "The crown remains. Try another path."
    detail = string.format(
      "Floor %d  /  %d keepers defeated  /  %d souls",
      run.floor.index,
      run.kills,
      run.coins
    )
  elseif run.phase == "victory" then
    title, subtitle = "THE CROWN IS SILENT", "You ended the reign of the Ashen King."
    detail = string.format(
      "Expedition complete  /  %d souls  /  %dm %02ds",
      run.coins,
      run.time // 60,
      math.floor(run.time) % 60
    )
  else
    title, subtitle, detail = "", "", ""
  end
  Hud.set_text("overlay_title", title)
  Hud.set_text("overlay_subtitle", subtitle)
  Hud.set_text("overlay_detail", detail)
end

function View:update(run, paused, dt)
  if not self.world:ready() then
    return
  end
  CombatView.update(self.world, run.player, self.weapons, true)
  self:actor(run.player, run.time, true)
  for _, enemy in ipairs(run.enemies) do
    self:actor(enemy, run.time, Exploration.visible(run, enemy.x, enemy.y))
    CombatView.update(self.world, enemy, self.weapons, Exploration.visible(run, enemy.x, enemy.y))
  end
  for _, pickup in ipairs(run.pickups) do
    if not self.pickups[pickup.id] then
      self.world:pickup(pickup)
      self.pickups[pickup.id] = true
    end
    Entity.set_enabled(self.world:id(pickup.id), not pickup.taken)
  end
  for _, chest in ipairs(run.chests) do
    Sprite.set_color(
      self.world:id(chest.id),
      chest.opened and 0.4 or 1,
      chest.opened and 0.4 or 1,
      chest.opened and 0.4 or 1,
      1
    )
  end
  EncounterView.update(self.world, run)
  ExplorationView.update(self, run)
  local open = true
  Sprite.set_color(
    self.world:id("stairs"),
    open and 1 or 0.4,
    open and 0.85 or 0.4,
    open and 0.55 or 0.5,
    1
  )
  self:projectiles(run)
  self.hud_time = self.hud_time - dt
  if self.hud_time <= 0 then
    self:hud(run, paused)
    self.hud_time = 0.05
  end
end

function View:sounds(events)
  for _, event in ipairs(events) do
    Audio.play("asset://dungeon/sound_" .. event, {
      bus = "sfx",
      volume = event == "shoot" and 0.12 or 0.22,
      concurrency_group = "dungeon_" .. event,
      max_voices = 3,
    })
  end
end

return View
