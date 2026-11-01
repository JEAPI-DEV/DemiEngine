-- Limited encounter resources: no passive refill while safely camping at range.
local Magic = {}
Magic.capacity = 8
Magic.profiles = {
  spark = {
    cost = 2,
    range = 6,
    speed = 9,
    interval = 0.8,
    damage = 1,
    color = { 0.55, 1, 0.85, 1 },
  },
  frost = {
    cost = 3,
    range = 5,
    speed = 8,
    interval = 1,
    damage = 1,
    slow = 2.5,
    color = { 0.45, 0.8, 1, 1 },
  },
  ember = {
    cost = 4,
    range = 4.8,
    speed = 8,
    interval = 1.2,
    damage = 3,
    splash = 1.4,
    color = { 1, 0.45, 0.15, 1 },
  },
}

function Magic.restore(player, amount)
  player.mana = math.min(player.max_mana, player.mana + amount)
end

function Magic.spend(player, profile)
  local spell = assert(Magic.profiles[profile])
  if player.mana < spell.cost then
    return false
  end
  player.mana = player.mana - spell.cost
  return true
end

return Magic
