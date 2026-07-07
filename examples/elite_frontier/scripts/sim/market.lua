local Market = {}

Market.goods = {
  { id = "food", label = "Food", base = 12, agricultural = -5, industrial = 4 },
  { id = "machines", label = "Machines", base = 58, agricultural = 12, industrial = -10 },
  { id = "medicine", label = "Medicine", base = 38, agricultural = 2, industrial = 1 },
}

local function economy_delta(good, system)
  if system.economy == "Agricultural" or system.economy == "Frontier" then
    return good.agricultural
  end
  if system.economy == "Industrial" or system.economy == "Corporate" then
    return good.industrial
  end
  if system.economy == "Research" and good.id == "medicine" then
    return -8
  end
  return 0
end

function Market.price(system, good_id)
  for _, good in ipairs(Market.goods) do
    if good.id == good_id then
      return math.max(2, good.base + economy_delta(good, system) + math.floor(system.tech * 0.6))
    end
  end
  return 0
end

function Market.buy(state, good_id)
  local price = Market.price(state.current_system(), good_id)
  if state.cargo_free() <= 0 then
    state.message = "Cargo bay full."
    return false
  end
  if state.commander.credits < price then
    state.message = "Insufficient credits."
    return false
  end
  state.commander.credits = state.commander.credits - price
  state.commander.cargo[good_id] = (state.commander.cargo[good_id] or 0) + 1
  state.message = "Bought 1t " .. good_id .. "."
  return true
end

function Market.sell(state, good_id)
  local amount = state.commander.cargo[good_id] or 0
  if amount <= 0 then
    state.message = "No " .. good_id .. " in hold."
    return false
  end
  local price = math.max(1, Market.price(state.current_system(), good_id) - 2)
  state.commander.cargo[good_id] = amount - 1
  state.commander.credits = state.commander.credits + price
  state.message = "Sold 1t " .. good_id .. "."
  return true
end

return Market
