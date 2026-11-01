local Data = require("demi.data")
local Floors = {}
local names = { "floor1", "floor2", "floor3", "boss1" }

function Floors.scene(index)
  return "scene://dungeon/" .. assert(names[index], "Unknown dungeon floor")
end

function Floors.index(scene)
  for index, name in ipairs(names) do
    if scene == "scene://dungeon/" .. name then
      return index
    end
  end
  return 1
end

function Floors.create(index)
  local id = "asset://dungeon/layouts/" .. assert(names[index], "Unknown dungeon floor")
  local document, problem = Data.load(id)
  assert(document, problem and problem.message or ("Cannot load " .. id))
  local floor = assert(document.dungeon, "Map is missing its dungeon metadata")
  floor.index = index
  floor.width, floor.height = document.map_size[1], document.map_size[2]
  floor.tiles = {}
  local ground
  for _, layer in ipairs(document.layers) do
    if layer.name == "ground" then
      ground = layer.tiles
    end
  end
  assert(ground, "Map is missing its ground layer")
  for y = 0, floor.height - 1 do
    floor.tiles[y] = {}
    for x = 0, floor.width - 1 do
      local cell = (floor.height - 1 - y) * floor.width + x + 1
      local walkable = ground[cell] ~= 0
      for _, layer in ipairs(document.layers) do
        if (layer.collision or layer.navigation_blocked) and layer.tiles[cell] ~= 0 then
          walkable = false
        end
      end
      floor.tiles[y][x] = walkable
    end
  end
  return floor
end

function Floors.walkable(floor, x, y)
  local row = floor.tiles[math.floor(y + 0.5)]
  return row and row[math.floor(x + 0.5)] == true or false
end

function Floors.room(floor, x, y)
  for _, room in ipairs(floor.rooms) do
    if x >= room[1] - 0.5 and x <= room[3] + 0.5 and y >= room[2] - 0.5 and y <= room[4] + 0.5 then
      return room[5]
    end
  end
  return "Passage"
end

return Floors
