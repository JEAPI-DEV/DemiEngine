local Galaxy = {}

local names = {
  "Lave", "Zaonce", "Tionisla", "Reorte", "Leesti", "Diso", "Riedquat", "Orerve",
}

local economies = {
  "Agricultural", "Industrial", "Refinery", "Corporate", "Frontier", "Research",
}

local function seeded(seed)
  local value = seed
  return function(minimum, maximum)
    value = (value * 1103515245 + 12345) % 2147483648
    local t = value / 2147483648
    return minimum + (maximum - minimum) * t
  end
end

function Galaxy.generate_cluster()
  local random = seeded(1984)
  local systems = {}
  for index, name in ipairs(names) do
    local economy = economies[((index * 3) % #economies) + 1]
    local tech = math.floor(random(2, 12))
    systems[index] = {
      id = index,
      name = name,
      economy = economy,
      tech = tech,
      government = index % 3 == 0 and "Anarchy" or (index % 2 == 0 and "Feudal" or "Democracy"),
      x = random(-42, 42),
      y = random(-26, 26),
    }
  end
  return systems
end

function Galaxy.distance(a, b)
  local dx = a.x - b.x
  local dy = a.y - b.y
  return math.sqrt(dx * dx + dy * dy)
end

return Galaxy
