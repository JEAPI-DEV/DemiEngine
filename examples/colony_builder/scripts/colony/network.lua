local Entity = require("demi.entity")
local Network = {}
Network.__index = Network
local function enabled(id)
  return Entity.exists(id) and Entity.is_enabled(id)
end

function Network.new(config, terrain)
  local self = setmetatable(
    { root = config.habitat, nodes = {}, edges = {}, revision = 0, terrain = terrain },
    Network
  )
  for _, entry in ipairs({
    { config.habitat, "habitat", 8, 8 },
    { config.solar_array, "solar", 8, 5 },
    { config.water_extractor, "water", 4, 4 },
  }) do
    local position = Entity.world_position(entry[1])
    assert(position, "Missing starter building: " .. entry[1])
    self.nodes[entry[1]] = {
      id = entry[1],
      kind = entry[2],
      width = entry[3],
      depth = entry[4],
      x = position[1],
      y = position[2],
      z = position[3],
      complete = true,
    }
  end
  self:refresh()
  return self
end

function Network:pick(id)
  while id and id ~= "" do
    for _, node in pairs(self.nodes) do
      if node.id == id or node.site_id == id then
        return node
      end
    end
    id = Entity.parent(id)
  end
end

function Network:pair(a, b)
  for _, edge in ipairs(self.edges) do
    if (edge.a == a and edge.b == b) or (edge.a == b and edge.b == a) then
      return edge
    end
  end
end

function Network:refresh()
  local connected = {}
  if enabled(self.root) then
    connected[self.root] = true
  end
  local queue = { self.root }
  local queue_index = 1
  while connected[self.root] and queue_index <= #queue do
    local id = queue[queue_index]
    queue_index = queue_index + 1
    for _, edge in ipairs(self.edges) do
      if edge.complete and enabled(edge.id) then
        local other = edge.a == id and edge.b or edge.b == id and edge.a or nil
        local node = other and self.nodes[other]
        if node and node.complete and enabled(other) and not connected[other] then
          connected[other] = true
          queue[#queue + 1] = other
        end
      end
    end
  end
  self.connected = connected
end

function Network:counts()
  local solar, water = 0, 0
  for id in pairs(self.connected) do
    local node = self.nodes[id]
    if node.kind == "solar" then
      solar = solar + 1
    elseif node.kind == "water" then
      water = water + 1
    end
  end
  return solar, water
end

function Network:route(a, b)
  if a == b then
    return nil, "Choose two different buildings"
  end
  local first, last = self.nodes[a], self.nodes[b]
  if not first or not last or not first.complete or not last.complete then
    return nil, "Finish both buildings before connecting"
  end
  if self:pair(a, b) then
    return nil, "A connection already exists or is queued"
  end
  local dx, dz = last.x - first.x, last.z - first.z
  local distance = math.sqrt(dx * dx + dz * dz)
  if distance > 28 then
    return nil, "Connection is too long (maximum 28 m)"
  end
  local function exit(node)
    return math.min(
      math.abs(dx) > 0.001 and node.width / 2 / math.abs(dx) * distance or math.huge,
      math.abs(dz) > 0.001 and node.depth / 2 / math.abs(dz) * distance or math.huge
    ) + 0.3
  end
  local start, finish = exit(first), distance - exit(last)
  if finish <= start then
    return nil, "Buildings need clearance for a connection"
  end
  local points = {}
  local steps = math.max(1, math.ceil((finish - start) / 2))
  for i = 0, steps do
    local t = (start + (finish - start) * i / steps) / distance
    local x, z = first.x + dx * t, first.z + dz * t
    for id, node in pairs(self.nodes) do
      if
        id ~= a
        and id ~= b
        and math.abs(x - node.x) < node.width / 2 + 0.35
        and math.abs(z - node.z) < node.depth / 2 + 0.35
      then
        return nil, "Connection crosses another building or planned site"
      end
    end
    local y, normal = self.terrain:sample(x, z)
    if not y or normal < 0.85 then
      return nil, "Connection crosses blocked or steep ground"
    end
    points[#points + 1] = { x, y + 0.3, z }
  end
  return points
end

function Network:draw_link(id, points, pending)
  local children = {}
  for i = 1, #points - 1 do
    local a, b = points[i], points[i + 1]
    local dx, dy, dz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
    local horizontal = math.sqrt(dx * dx + dz * dz)
    children[#children + 1] = {
      id = id .. "/segment_" .. i,
      components = {
        Transform3D = {
          position = { (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, (a[3] + b[3]) / 2 },
          rotation = { -math.atan(dy, horizontal), math.atan(dx, dz), 0 },
        },
        MeshRenderer = {
          shape = "cube",
          size = { 0.35, 0.2, math.sqrt(horizontal * horizontal + dy * dy) },
          color = pending and { 0.8, 0.55, 0.2, 1 } or { 0.2, 0.65, 0.7, 1 },
        },
      },
    }
  end
  if not Entity.create(id, { components = { Transform3D = {} } }) then
    return false
  end
  for _, child in ipairs(children) do
    child.components.Transform3D.parent = id
    if not Entity.create(child.id, { components = child.components }) then
      for _, created in ipairs(children) do
        Entity.destroy(created.id)
      end
      Entity.destroy(id)
      return false
    end
  end
  return true
end

function Network:remove(edge)
  -- Destroy children as well as the owner; entity destruction is not assumed recursive.
  for _, id in ipairs(Entity.children(edge.id)) do
    Entity.destroy(id)
  end
  Entity.destroy(edge.id)
  for i, item in ipairs(self.edges) do
    if item == edge then
      table.remove(self.edges, i)
      break
    end
  end
  self:refresh()
end

return Network
