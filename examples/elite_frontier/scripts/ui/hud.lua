local Market = require("sim.market")

local HudView = {}

local function set_group(group, visible)
  Hud.set_group_visible(group, visible)
end

function HudView.sync_visibility(state)
  set_group("flight", state.mode == "flight")
  set_group("touch", state.mode == "flight")
  set_group("station", state.mode == "station")
end

function HudView.update_market(state)
  local system = state.current_system()
  for _, good in ipairs(Market.goods) do
    local id = good.id
    local label = string.upper(good.label)
    local cargo = state.commander.cargo[id] or 0
    local price = Market.price(system, id)
    Hud.set_text("market_" .. id, string.format("%-9s %3d/%3d H:%2d", label, price, math.max(1, price - 2), cargo))
  end
end

function HudView.update(state)
  local system = state.current_system()
  local target = state.target_system()
  Hud.set_text("hud_system", string.format("SYSTEM: %s  %s  TL%d", system.name, string.upper(system.economy), system.tech))
  Hud.set_text("hud_credits", string.format("CR: %d", state.commander.credits))
  Hud.set_text("hud_ship", string.format("SPD %03d  FUEL %d  HOLD %d/%d  %s",
    math.floor(state.ship.speed + 0.5),
    state.commander.fuel,
    state.cargo_used(),
    state.commander.hold_capacity,
    state.ship.alert))
  Hud.set_text("hud_target", string.format("TARGET: %s", target.name))
  Hud.set_text("hud_price", string.format("JUMP FUEL: %d", math.max(1, math.ceil(math.abs(target.x - system.x) / 16))))
  Hud.set_text("hud_status", state.message)
  Hud.set_text("station_info", string.format("%s  %s  GOV %s  FUEL %d", system.name, system.economy, system.government, state.commander.fuel))
  Hud.set_text("station_manifest", string.format("CREDITS %d   CARGO %d/%d", state.commander.credits, state.cargo_used(), state.commander.hold_capacity))
  HudView.update_market(state)
  HudView.sync_visibility(state)
end

return HudView
