local Floors = require("dungeon.floors")
local Exploration = {}

function Exploration.key(x, y)
  return math.floor(x + 0.5) .. ":" .. math.floor(y + 0.5)
end

function Exploration.create(run)
  run.discovered, run.visible, run.visited_rooms, run.journal = {}, {}, {}, {}
  run.gates, run.levers, run.clues, run.breakables = {}, {}, {}, {}
  run.visibility_revision, run.gate_revision = 0, 0
  for index, source in ipairs(run.floor.gates) do
    local gate = {}
    for key, value in pairs(source) do
      gate[key] = value
    end
    gate.open = false
    run.gates[index] = gate
  end
  for index, source in ipairs(run.floor.levers) do
    run.levers[index] =
      { id = source.id, x = source.x, y = source.y, label = source.label, used = false }
  end
  for index, source in ipairs(run.floor.clues) do
    run.clues[index] =
      { id = source.id, x = source.x, y = source.y, text = source.text, read = false }
  end
  for index, point in ipairs(run.floor.urns) do
    run.breakables[index] = {
      id = "urn_" .. index,
      kind = "urn",
      x = point[1],
      y = point[2],
      hp = 1,
      loot = point[3] or "mana_shard",
      rewarded = false,
    }
  end
end

function Exploration.gate_open(run, id)
  if not id then
    return true
  end
  for _, gate in ipairs(run.gates) do
    if gate.id == id then
      return gate.open
    end
  end
  return false
end

function Exploration.gate_contains(gate, x, y)
  return math.abs(x - gate.x) < gate.width / 2 + 0.01
    and math.abs(y - gate.y) < gate.height / 2 + 0.01
end

function Exploration.visible(run, x, y)
  return run.visible[Exploration.key(x, y)] == true
end

function Exploration.discovered(run, x, y)
  return run.discovered[Exploration.key(x, y)] == true
end

-- Stop the visibility ray at the near face of its target tile. Subtracting a
-- fixed half-tile from center distance makes oblique wall rays hit themselves.
function Exploration.tile_entry_distance(origin_x, origin_y, x, y)
  local dx, dy = x - origin_x, y - origin_y
  local distance = math.sqrt(dx * dx + dy * dy)
  if distance == 0 then
    return 0
  end
  local entry = 0
  if math.abs(dx) > 0.000001 then
    local near_x = dx > 0 and x - 0.5 or x + 0.5
    entry = math.max(entry, (near_x - origin_x) / dx)
  end
  if math.abs(dy) > 0.000001 then
    local near_y = dy > 0 and y - 0.5 or y + 0.5
    entry = math.max(entry, (near_y - origin_y) / dy)
  end
  return math.max(0, entry * distance)
end

function Exploration.update(run, geometry)
  local px, py = math.floor(run.player.x + 0.5), math.floor(run.player.y + 0.5)
  local origin = px .. ":" .. py .. ":" .. run.gate_revision
  if run.vision_origin == origin then
    return
  end
  run.vision_origin = origin
  run.visible = {}
  for y = math.max(0, py - 7), math.min(run.floor.height - 1, py + 7) do
    for x = math.max(0, px - 7), math.min(run.floor.width - 1, px + 7) do
      local dx, dy = x - run.player.x, y - run.player.y
      local distance = math.sqrt(dx * dx + dy * dy)
      -- A wall's near face remains visible, while geometry behind it does not.
      local visible = distance < 0.7
        or (
          distance <= 7
          and not geometry:wall_distance(
            run.player.x,
            run.player.y,
            dx / distance,
            dy / distance,
            math.max(0, Exploration.tile_entry_distance(run.player.x, run.player.y, x, y) - 0.01)
          )
        )
      if visible then
        local key = Exploration.key(x, y)
        run.visible[key], run.discovered[key] = true, true
      end
    end
  end
  -- Light boundary walls adjoining a visible floor cell, including walls just
  -- beyond the circular radius. Otherwise corridors end in floating floor strips.
  local visible_floors = {}
  for key in pairs(run.visible) do
    local x, y = key:match("^(%d+):(%d+)$")
    x, y = tonumber(x), tonumber(y)
    if Floors.walkable(run.floor, x, y) then
      visible_floors[#visible_floors + 1] = { x, y }
    end
  end
  for _, cell in ipairs(visible_floors) do
    for dy = -1, 1 do
      for dx = -1, 1 do
        local x, y = cell[1] + dx, cell[2] + dy
        if
          x >= 0
          and y >= 0
          and x < run.floor.width
          and y < run.floor.height
          and not Floors.walkable(run.floor, x, y)
        then
          local key = Exploration.key(x, y)
          run.visible[key], run.discovered[key] = true, true
        end
      end
    end
  end
  -- Discovery remembers the whole entered room and its wall rim. Live enemy
  -- visibility still uses the occluded rays above, so this reveals no actors.
  for _, bounds in ipairs(run.floor.rooms) do
    if px >= bounds[1] and px <= bounds[3] and py >= bounds[2] and py <= bounds[4] then
      for y = math.max(0, bounds[2] - 1), math.min(run.floor.height - 1, bounds[4] + 1) do
        for x = math.max(0, bounds[1] - 1), math.min(run.floor.width - 1, bounds[3] + 1) do
          run.discovered[Exploration.key(x, y)] = true
        end
      end
    end
  end
  local room = Floors.room(run.floor, px, py)
  if room ~= "Passage" and not run.visited_rooms[room] then
    run.visited_rooms[room] = true
  end
  run.visibility_revision = run.visibility_revision + 1
end

local function open_gate(run, gate)
  if gate.open then
    return
  end
  gate.open = true
  run.gate_revision = run.gate_revision + 1
  for _, enemy in ipairs(run.enemies) do
    enemy.path_timer = 0
  end
end

local function nearby(run, point, range)
  return (point.x - run.player.x) ^ 2 + (point.y - run.player.y) ^ 2 < range ^ 2
end

function Exploration.interact(run)
  for _, lever in ipairs(run.levers) do
    if nearby(run, lever, 1.4) then
      if not lever.used then
        lever.used = true
        for _, gate in ipairs(run.gates) do
          if gate.lever == lever.id then
            open_gate(run, gate)
          end
        end
        run.message = lever.label .. ": chains rattle; the vault and its return route are open."
      else
        run.message = "This lever is already lowered."
      end
      return true
    end
  end
  for _, gate in ipairs(run.gates) do
    if not gate.open and nearby(run, gate, 1.6) then
      if gate.secret then
        open_gate(run, gate)
        run.message = "The cracked masonry swings inward. A hidden chamber!"
        run.journal[#run.journal + 1] = "Found: " .. gate.label
      else
        run.message = gate.clue
      end
      return true
    end
  end
  for _, clue in ipairs(run.clues) do
    if nearby(run, clue, 1.4) then
      if not clue.read then
        clue.read = true
        run.journal[#run.journal + 1] = clue.text
      end
      run.message = clue.text
      return true
    end
  end
  return false
end

function Exploration.prompt(run)
  for _, lever in ipairs(run.levers) do
    if nearby(run, lever, 1.4) and not lever.used then
      return "[E] Pull " .. lever.label
    end
  end
  for _, gate in ipairs(run.gates) do
    if not gate.open and nearby(run, gate, 1.6) then
      return gate.secret and "A draft through cracked stone... [E] investigate"
        or "[E] Inspect the barred gate"
    end
  end
  for _, clue in ipairs(run.clues) do
    if nearby(run, clue, 1.4) then
      return "[E] Read the keeper's note / [J] journal"
    end
  end
end

return Exploration
