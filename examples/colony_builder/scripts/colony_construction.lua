local Camera3D = require("demi.camera3d")
local Entity = require("demi.entity")
local Events = require("demi.events")
local Hud = require("demi.hud")
local Input = require("demi.input")
local Time = require("demi.time")
local Physics = require("demi.physics.query3d")
local Transform = require("demi.transform3d")

local Construction = {}

local catalog = {
  solar = {
    name = "Solar array",
    prefab = "prefab://utilities/solar",
    cost = 6,
    width = 8,
    depth = 5,
  },
  water = {
    name = "Water extractor",
    prefab = "prefab://utilities/water",
    cost = 8,
    width = 4,
    depth = 4,
  },
}
local ghost = "construction_preview"

function Construction:message(text)
  if text == self.status then
    return
  end
  self.status = text
  Hud.set_text("construction.status", text)
end

function Construction:select(kind)
  self.kind = (catalog[kind] or kind == "connect" or kind == "disconnect") and kind or nil
  self.first_node, self.selected_job = nil, nil
  self.cached_key, self.visual_key = nil, nil
  Entity.set_enabled(ghost, false)
  self:message(
    self.kind
        and (catalog[self.kind] and catalog[self.kind].name .. ": choose clear ground" or "Select the first completed building")
      or "Choose a utility to extend the colony."
  )
end

function Construction:is_terrain(id)
  return self.terrain_service:owns(id)
end

function Construction:ground(x, z)
  local y, normal = self.terrain_service:sample(x, z)
  if y then
    return { point = { x, y, z }, normal = { 0, normal, 0 } }
  end
end

function Construction:site(x, z)
  local item = catalog[self.kind]
  local key = self.kind .. ":" .. x .. ":" .. z .. ":" .. self.network.revision
  if key == self.cached_key then
    return self.cached_site
  end
  self.cached_key = key
  local result = { x = x, z = z, valid = false, reason = "Choose clear terrain" }
  self.cached_site = result
  local center = self:ground(x, z)
  if not center then
    return result
  end
  result.y, result.ground_y = center.point[2], center.point[2]
  -- A conservative footprint rejects slopes and building overlap, including
  -- the starter buildings. Sampling is cached per snapped site, not per frame.
  local low, high = result.y, result.y
  for _, dx in ipairs({ -item.width / 2, item.width / 2 }) do
    for _, dz in ipairs({ -item.depth / 2, item.depth / 2 }) do
      local hit = self:ground(x + dx, z + dz)
      if not hit then
        result.reason = "Footprint crosses a building or terrain edge"
        return result
      end
      low, high = math.min(low, hit.point[2]), math.max(high, hit.point[2])
      if hit.normal[2] < 0.9 then
        result.reason = "Terrain is too steep"
        return result
      end
    end
  end
  if high - low > 1.25 then
    result.reason = "Terrain is too uneven"
    return result
  end
  result.y, result.relief = high, high - low
  for _, hit in ipairs(Physics.overlap_box_all(x, high + 2, z, item.width + 1, 4, item.depth + 1)) do
    if not self:is_terrain(hit.entity_id) then
      result.reason = "Leave clearance around other buildings"
      return result
    end
  end
  result.valid = true
  result.reason = "Click to build · " .. item.cost .. " metal · Esc / right-click cancels"
  return result
end

function Construction:point(x, y)
  local w, h = Input.viewport_size()
  local ray = Camera3D.screen_ray(self.camera, x, y, w, h)
  if not ray then
    return
  end
  local o, d = ray.origin, ray.direction
  local hit = Physics.raycast(o[1], o[2], o[3], d[1], d[2], d[3], 1000)
  if not hit or not self:is_terrain(hit.entity_id) then
    return
  end
  return self:site(math.floor(hit.point[1] / 2 + 0.5) * 2, math.floor(hit.point[3] / 2 + 0.5) * 2)
end

function Construction:build(site)
  if not site or not site.valid then
    self:message(site and site.reason or "Choose clear terrain")
    return
  end
  local job, message = self.jobs:enqueue_build(self.kind, catalog[self.kind], site)
  if job then
    self:select(nil)
    self.selected_job = job
    self.observed_job = job
  end
  self:message(message)
end

function Construction:world_click(x, y)
  if catalog[self.kind] then
    return self:build(self:point(x, y))
  end
  local w, h = Input.viewport_size()
  local ray = Camera3D.screen_ray(self.camera, x, y, w, h)
  if not ray then
    return
  end
  local o, d = ray.origin, ray.direction
  local hit = Physics.raycast(o[1], o[2], o[3], d[1], d[2], d[3], 1000)
  local node = hit and self.network:pick(hit.entity_id)
  if not node then
    self:message("Select a building or construction site")
    return
  end
  if not self.kind then
    self.selected_job = self.jobs:find_site(node.site_id)
    self:message(
      self.selected_job
          and "Planned site selected · Cancel releases reservations; delivered metal is hauled back"
        or (
          self.network.connected[node.id] and "Connected to habitat"
          or "Disconnected · use Connect to join the utility network"
        )
    )
    return
  end
  if not node.complete then
    self:message("Finish this building before connecting")
    return
  end
  if not self.first_node then
    self.first_node = node.id
    self:message("Select the second building")
    return
  end
  local first = self.first_node
  self.first_node = nil
  if self.kind == "disconnect" then
    local edge = self.network:pair(first, node.id)
    if edge then
      if edge.complete then
        self.network:remove(edge)
      else
        for _, job in ipairs(self.jobs.queue) do
          if job.edge == edge then
            self.jobs:cancel(job)
            break
          end
        end
      end
      self:message("Connection removed")
    else
      self:message("No connection between these buildings")
    end
  else
    local job, message = self.jobs:enqueue_link(first, node.id)
    self.observed_job = job
    self:message(message)
  end
end

function Construction.new(config, network, jobs, terrain)
  local self = setmetatable(
    { network = network, jobs = jobs, terrain_service = terrain, camera = config.camera },
    { __index = Construction }
  )
  jobs:publish()
  assert(Entity.create(ghost, {
    enabled = false,
    components = {
      Transform3D = {},
      MeshRenderer = {
        shape = "cube",
        surface_mode = "transparent",
        opacity = 0.35,
        color = { 0.3, 1, 0.65, 1 },
      },
    },
  }))
  self.subscription = Events.subscribe("ui_event", function(event)
    if event.type ~= "submit" then
      return
    end
    if event.action == "build.solar" then
      self:select("solar")
    elseif event.action == "build.water" then
      self:select("water")
    elseif event.action == "build.connect" then
      self:select("connect")
    elseif event.action == "build.disconnect" then
      self:select("disconnect")
    elseif event.action == "time.pause" then
      Time.set_paused(not Time.is_paused())
    elseif event.action == "time.normal" then
      Time.set_paused(false)
      Time.set_scale(1)
    elseif event.action == "time.fast" then
      Time.set_paused(false)
      Time.set_scale(3)
    elseif event.action == "worker.rest" then
      jobs.worker.needs.forced = true
      self:message("Engineer ordered to rest and refill at the habitat")
    elseif event.action == "build.cancel" then
      if not self.kind and self.selected_job and jobs:cancel(self.selected_job) then
        self:select(nil)
        self:message("Plan cancelled · uncollected metal released; other loads return by hauling")
      else
        self:select(nil)
      end
    end
  end)
  self:select(nil)
  return self
end

function Construction:update()
  local clock = Time.is_paused() and "Paused" or string.format("%dx", Time.get_scale())
  if clock ~= self.clock_label then
    Hud.set_text("time_label", clock)
    self.clock_label = clock
  end
  if self.observed_job and self.observed_job.state == "complete" then
    local job = self.observed_job
    self.observed_job = nil
    if not self.first_node and not catalog[self.kind] then
      self:message(
        job.kind == "link" and "Connection complete · select another pair or Cancel"
          or job.item.name .. " complete · Connect it to the habitat to enable production"
      )
    end
  end
  local mouse = Input.mouse_down("left")
  local click = mouse and not self.mouse_was_down
  self.mouse_was_down = mouse
  if Input.key_pressed("escape") or Input.mouse_down("right") then
    self:select(nil)
    return
  end
  local x, y = Input.mouse_position()
  local site = catalog[self.kind] and self:point(x, y)
  if site and site.y then
    local item = catalog[self.kind]
    Entity.set_enabled(ghost, true)
    local valid = site.valid and self.jobs:available_metal() >= item.cost
    local visual_key = self.cached_key .. tostring(valid)
    if self.visual_key ~= visual_key then
      Transform.set_position(ghost, site.x, site.y + 0.18, site.z)
      Transform.set_scale(ghost, item.width, 0.2, item.depth)
      Entity.set_field(
        ghost,
        "MeshRenderer",
        "color",
        valid and { 0.3, 1, 0.65, 1 } or { 1, 0.25, 0.3, 1 }
      )
      self.visual_key = visual_key
    end
    self:message(self.jobs:available_metal() < item.cost and "Not enough metal" or site.reason)
  else
    Entity.set_enabled(ghost, false)
  end
  local touches = Input.touches()
  for _, touch in ipairs(touches) do
    if touch.phase == "began" and not Input.ui_pointer_captured(touch.id) then
      self:world_click(touch.x, touch.y)
      return
    end
  end
  if #touches == 0 and click and not Input.ui_pointer_captured() then
    self:world_click(x, y)
  end
end

function Construction:destroy()
  if self.subscription then
    Events.unsubscribe(self.subscription)
  end
end

return Construction
