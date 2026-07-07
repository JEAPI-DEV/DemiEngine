local Actions = {}

local ctx = nil

function Actions.bind(context)
  ctx = context
end

local function buy(good)
  ctx.market.buy(ctx.state, good)
  ctx.save.write(ctx.state)
  ctx.hud.update(ctx.state)
end

local function sell(good)
  ctx.market.sell(ctx.state, good)
  ctx.save.write(ctx.state)
  ctx.hud.update(ctx.state)
end

-- @HandleAction("buy_food")
function Actions.buy_food()
  buy("food")
end

-- @HandleAction("sell_food")
function Actions.sell_food()
  sell("food")
end

-- @HandleAction("buy_machines")
function Actions.buy_machines()
  buy("machines")
end

-- @HandleAction("sell_machines")
function Actions.sell_machines()
  sell("machines")
end

-- @HandleAction("buy_medicine")
function Actions.buy_medicine()
  buy("medicine")
end

-- @HandleAction("sell_medicine")
function Actions.sell_medicine()
  sell("medicine")
end

-- @HandleAction("launch")
function Actions.launch()
  ctx.ship.launch(ctx.state)
  ctx.hud.update(ctx.state)
end

-- @HandleAction("dock")
function Actions.dock()
  ctx.ship.try_dock(ctx.state)
  ctx.save.write(ctx.state)
  ctx.hud.update(ctx.state)
end

-- @HandleAction("jump")
function Actions.jump()
  ctx.ship.jump(ctx.state)
  ctx.save.write(ctx.state)
  ctx.hud.update(ctx.state)
end

-- @HandleAction("touch_left")
-- @HandleAction("touch_right")
-- @HandleAction("touch_throttle")
function Actions.touch_noop()
end

return Actions
