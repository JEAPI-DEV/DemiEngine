local platforms = require("game.platforms")

local World = {}

function World.create_platform(game, id, name, x, y, width, height)
  platforms.create_static_box(id, name, x, y, width, height)
  platforms.remember(game.platforms, id, x, y, width, height)
end

return World
