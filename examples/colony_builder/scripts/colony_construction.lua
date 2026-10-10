local Camera3D = require("demi.camera3d")
local Entity = require("demi.entity")
local Events = require("demi.events")
local Hud = require("demi.hud")
local Input = require("demi.input")
local Physics = require("demi.physics.query3d")
local Prefab = require("demi.prefab")
local Transform = require("demi.transform3d")

---@demi_component
---@description Places utility prefabs on clear, gently sloped terrain. Costs and placement belong to the game.
local Construction = {}
---@demi_property entity
Construction.camera = "camera"
---@demi_property entity
Construction.terrain = "terrain"
---@demi_property
---@range 0 1000
Construction.starting_metal = 24

local catalog = {
  solar = {name="Solar array", prefab="prefab://utilities/solar", cost=6, width=8, depth=5},
  water = {name="Water extractor", prefab="prefab://utilities/water", cost=8, width=4, depth=4},
}
local ghost = "construction_preview"

function Construction:message(text)
  if text == self.status then return end
  self.status = text
  Hud.set_text("construction.status", text)
end

function Construction:select(kind)
  self.kind = catalog[kind] and kind or nil
  self.cached_key, self.visual_key = nil, nil
  Entity.set_enabled(ghost, false)
  self:message(self.kind and (catalog[self.kind].name .. ": choose clear ground") or "Choose a utility to extend the colony.")
end

function Construction:is_terrain(id)
  while id and id ~= "" do
    if id == self.terrain then return true end
    id = Entity.parent(id)
  end
  return false
end

function Construction:ground(x, z)
  local hit = Physics.raycast(x, 500, z, 0, -1, 0, 1000)
  if hit and self:is_terrain(hit.entity_id) then return hit end
end

function Construction:site(x, z)
  local item = catalog[self.kind]
  local key = self.kind .. ":" .. x .. ":" .. z
  if key == self.cached_key then return self.cached_site end
  self.cached_key = key
  local result = {x=x, z=z, valid=false, reason="Choose clear terrain"}
  self.cached_site = result
  local center = self:ground(x, z)
  if not center then return result end
  result.y = center.point[2]
  -- A conservative footprint rejects slopes and building overlap, including
  -- the starter buildings. Sampling is cached per snapped site, not per frame.
  local low, high = result.y, result.y
  for _, dx in ipairs({-item.width/2, item.width/2}) do
    for _, dz in ipairs({-item.depth/2, item.depth/2}) do
      local hit = self:ground(x+dx, z+dz)
      if not hit then result.reason="Footprint crosses a building or terrain edge"; return result end
      low, high = math.min(low, hit.point[2]), math.max(high, hit.point[2])
      if hit.normal[2] < 0.9 then result.reason="Terrain is too steep"; return result end
    end
  end
  if high-low > 1.25 then result.reason="Terrain is too uneven"; return result end
  result.y, result.relief = high, high-low
  for _, hit in ipairs(Physics.overlap_box_all(x, high+2, z, item.width+1, 4, item.depth+1)) do
    if not self:is_terrain(hit.entity_id) then result.reason="Leave clearance around other buildings"; return result end
  end
  result.valid = true
  result.reason = "Click to build · " .. item.cost .. " metal · Esc / right-click cancels"
  return result
end

function Construction:point(x, y)
  local w, h = Input.viewport_size()
  local ray = Camera3D.screen_ray(self.camera, x, y, w, h)
  if not ray then return end
  local o, d = ray.origin, ray.direction
  local hit = Physics.raycast(o[1], o[2], o[3], d[1], d[2], d[3], 1000)
  if not hit or not self:is_terrain(hit.entity_id) then return end
  return self:site(math.floor(hit.point[1] / 2 + 0.5)*2, math.floor(hit.point[3] / 2 + 0.5)*2)
end

function Construction:build(site)
  local item = catalog[self.kind]
  if not site or not site.valid then self:message(site and site.reason or "Choose clear terrain"); return end
  if self.metal < item.cost then self:message("Not enough metal · cancel or choose a cheaper utility"); return end
  local id = "built_" .. self.next_id
  local foundation = {components={Transform3D={
    position={0,-site.relief/2,0}, scale={item.width,site.relief+0.35,item.depth}
  }}}
  if not Prefab.instantiate(item.prefab, {
    id=id, position={site.x,site.y,site.z}, overrides={foundation=foundation}
  }) then
    self:message("Could not construct this utility"); return
  end
  self.next_id = self.next_id + 1
  self.metal = self.metal - item.cost
  Hud.set_text("metal", string.format("Metal: %.0f", self.metal))
  Events.emit("colony.building_created", {id=id.."/base", kind=self.kind})
  self.cached_key = nil
  self:select(nil)
  self:message(item.name .. " built · select another utility to continue")
end

function Construction:on_start()
  self.metal, self.next_id = self.starting_metal, 1
  Hud.set_text("metal", string.format("Metal: %.0f", self.metal))
  assert(Entity.create(ghost, {enabled=false, components={
    Transform3D={}, MeshRenderer={shape="cube", surface_mode="transparent", opacity=0.35, color={0.3,1,0.65,1}}
  }}))
  self.subscription = Events.subscribe("ui_event", function(event)
    if event.type ~= "submit" then return end
    if event.action == "build.solar" then self:select("solar")
    elseif event.action == "build.water" then self:select("water")
    elseif event.action == "build.cancel" then self:select(nil) end
  end)
  self:select(nil)
end

function Construction:on_update()
  local mouse = Input.mouse_down("left")
  local click = mouse and not self.mouse_was_down
  self.mouse_was_down = mouse
  if not self.kind then return end
  if Input.key_pressed("escape") or Input.mouse_down("right") then self:select(nil); return end
  local x, y = Input.mouse_position()
  local site = self:point(x, y)
  if site and site.y then
    local item = catalog[self.kind]
    Entity.set_enabled(ghost, true)
    local valid = site.valid and self.metal >= item.cost
    local visual_key = self.cached_key .. tostring(valid)
    if self.visual_key ~= visual_key then
      Transform.set_position(ghost, site.x, site.y+0.18, site.z)
      Transform.set_scale(ghost, item.width, 0.2, item.depth)
      Entity.set_field(ghost, "MeshRenderer", "color", valid and {0.3,1,0.65,1} or {1,0.25,0.3,1})
      self.visual_key = visual_key
    end
    self:message(self.metal < item.cost and "Not enough metal" or site.reason)
  else Entity.set_enabled(ghost, false) end
  local touches = Input.touches()
  for _, touch in ipairs(touches) do
    if touch.phase == "began" and not Input.ui_pointer_captured(touch.id) then
      self:build(self:point(touch.x, touch.y)); return
    end
  end
  if #touches == 0 and click and not Input.ui_pointer_captured() then self:build(site) end
end

function Construction:on_destroy()
  if self.subscription then Events.unsubscribe(self.subscription) end
end
return Construction
