---@demi_component
---@display_name Procedural Spider
---@category Animation
---@description Eight-legged terrain-aware locomotion driven by raycasts and two-bone IK.
local Spider = {}

---@demi_property
---@label Travel Speed
---@range 0.1 5.0
Spider.speed = 0.9

---@demi_property
---@label Maximum Foot Lift
---@range 0.05 0.5
Spider.step_height = 0.22

---@demi_property
---@label Stride
---@range 0.1 1.0
Spider.stride = 0.58

---@demi_property
---@label Terrain Seed
---@range 1 100000
Spider.terrain_seed = 1337

local body_height = 0.72
local terrain_start = -7.5
local terrain_columns = 15
local wall_x = 7.55

local function deterministic_noise(index, seed)
  local value = (index * 1103515245 + seed * 48271 + 12345) % 2147483647
  return (value % 1000) / 999.0
end

local function create_box(id, position, size, color, collider)
  local components = {
    Transform3D = { position = position },
    MeshRenderer = { shape = "cube", size = size, color = color },
  }
  if collider then
    components.BoxCollider3D = { size = size, layer = "terrain" }
    components.Rigidbody3D = { body_type = "static" }
  end
  return Entity.create(id, { name = id, components = components })
end

local function create_sphere(id, position, size, color, collider)
  local components = {
    Transform3D = { position = position },
    MeshRenderer = { shape = "sphere", size = { size, size, size }, color = color },
  }
  if collider then
    components.SphereCollider3D = { radius = size * 0.5, layer = "terrain" }
    components.Rigidbody3D = { body_type = "static" }
  end
  return Entity.create(id, { name = id, components = components })
end

function Spider:create_terrain()
  self.heights = { {}, {}, {} }
  local lane_heights = { 0.0, 0.0, 0.0 }
  for index = 0, terrain_columns - 1 do
    for lane = 1, 3 do
      if index >= 4 then
        local sample = deterministic_noise(index + lane * 31, self.terrain_seed)
        local delta = sample > 0.58 and 0.22 or (sample < 0.32 and -0.16 or 0.0)
        lane_heights[lane] = math.max(0.0,
          math.min(1.15, lane_heights[lane] + delta))
      end
      local height = lane_heights[lane]
      self.heights[lane][index + 1] = height
      local size_y = 1.0 + height
      create_box("terrain_step_" .. index .. "_lane_" .. lane,
        { terrain_start + index, (height - 1.0) * 0.5, (lane - 2) * 1.3 },
        { 1.02, size_y, 1.32 },
        { 0.12 + height * 0.07, 0.22, 0.16, 1.0 }, true)
    end
  end

  for index = 1, 5 do
    local column = 2 + ((index * 5) % 12)
    local rock_size = 0.32 + deterministic_noise(index + 40, self.terrain_seed) * 0.38
    local lane = index % 2 == 0 and 3 or 1
    local side = (lane - 2) * 1.3
    local x = terrain_start + column +
      (deterministic_noise(index + 70, self.terrain_seed) - 0.5) * 0.3
    create_sphere("terrain_rock_" .. index,
      { x, self.heights[lane][column + 1] + rock_size * 0.45, side },
      rock_size, { 0.28, 0.25, 0.22, 1.0 }, true)
  end

  self.edge_height = self.heights[2][#self.heights[2]]
  create_box("terrain_wall", { wall_x + 0.25, 2.5, 0.0 },
    { 0.5, 7.0, 4.2 }, { 0.16, 0.18, 0.23, 1.0 }, true)
end

function Spider:create_rig()
  self.legs = {}
  local longitudinal = { 0.58, 0.2, -0.2, -0.58 }
  local upper_lengths = { 0.649, 0.610, 0.610, 0.643 }
  local lower_lengths = { 0.733, 0.726, 0.726, 0.732 }
  for pair = 1, 4 do
    for _, side in ipairs({ -1, 1 }) do
      local suffix = tostring(pair) .. (side < 0 and "_l" or "_r")
      self.legs[#self.legs + 1] = {
        upper = "leg_" .. suffix .. "_upper",
        lower = "leg_" .. suffix .. "_lower",
        side = side,
        longitudinal = longitudinal[pair],
        upper_length = upper_lengths[pair],
        lower_length = lower_lengths[pair],
        phase_offset = ((pair - 1) * 0.25 + (side > 0 and 0.5 or 0.0)) % 1.0,
        foot = nil,
        swing_start = nil,
        swing_goal = nil,
        swinging = false,
      }
    end
  end
end

function Spider:surface_frame()
  local floor_distance = 14.25
  local corner_distance = body_height * math.pi * 0.5
  local climb_distance = 4.7
  local route_distance = floor_distance + corner_distance + climb_distance + 1.0
  local progress = self.travel % route_distance

  if progress < floor_distance then
    local x = -7.0 + progress
    local probe = Physics3D.raycast(x, 3.5, 0.0, 0.0, -1.0, 0.0, 6.0)
    local surface_y = probe and probe.point[2] or 0.0
    return { x, surface_y + body_height, 0.0 }, { 1, 0, 0 },
      { 0, 1, 0 }, "Walking over generated steps and rocks"
  end

  if progress < floor_distance + corner_distance then
    local angle = (progress - floor_distance) / body_height
    local normal = { -math.sin(angle), math.cos(angle), 0.0 }
    local forward = { math.cos(angle), math.sin(angle), 0.0 }
    local edge = { wall_x, self.edge_height, 0.0 }
    return Vector3.add(edge, Vector3.scale(normal, body_height)), forward, normal,
      "Transitioning from floor to wall"
  end

  local climb = math.min(progress - floor_distance - corner_distance, climb_distance)
  return { wall_x - body_height, self.edge_height + climb, 0.0 },
    { 0, 1, 0 }, { -1, 0, 0 }, "Climbing the wall"
end

function Spider:foot_target(body, forward, normal, side, longitudinal, stride)
  local lateral = { 0.0, 0.0, side }
  local candidate = Vector3.add(body, Vector3.add(
    Vector3.scale(forward, longitudinal + stride), lateral))
  local origin = Vector3.add(candidate, Vector3.scale(normal, 1.4))
  local hit = Physics3D.raycast(origin[1], origin[2], origin[3],
    -normal[1], -normal[2], -normal[3], 3.2)
  local contact = hit and hit.point or
    Vector3.add(candidate, Vector3.scale(normal, -body_height))
  return Vector3.add(contact, Vector3.scale(normal, 0.075))
end

function Spider:update_leg(leg, body, forward, normal)
  local lateral = { 0.0, 0.0, leg.side }
  local root = Vector3.add(body, Vector3.add(
    Vector3.scale(forward, leg.longitudinal), Vector3.scale(lateral, 0.31)))
  local cycle = (self.gait + leg.phase_offset) % 1.0
  local swinging = cycle < 0.32

  if leg.foot == nil then
    leg.foot = self:foot_target(body, forward, normal, leg.side,
      leg.longitudinal, 0.0)
  end
  if swinging and not leg.swinging then
    leg.swing_start = leg.foot
    leg.swing_goal = self:foot_target(body, forward, normal, leg.side,
      leg.longitudinal, self.stride)
  end
  if swinging then
    local amount = Mathf.smoothstep(0.0, 1.0, cycle / 0.32)
    leg.foot = Vector3.lerp(leg.swing_start, leg.swing_goal, amount)
    local contact_delta = Vector3.subtract(leg.swing_goal, leg.swing_start)
    local terrain_change = math.abs(Vector3.dot(contact_delta, normal))
    local clearance = math.min(self.step_height, 0.07 + terrain_change * 0.45)
    leg.foot = Vector3.add(leg.foot, Vector3.scale(normal,
      math.sin(amount * math.pi) * clearance))
  end
  leg.swinging = swinging

  local pole = Vector3.add(root, Vector3.add(
    Vector3.scale(lateral, 0.9), Vector3.scale(normal, 0.48)))
  local solved = Animation.solve_two_bone_3d({
    root = root,
    target = leg.foot,
    pole = pole,
    upper_length = leg.upper_length,
    lower_length = leg.lower_length,
  })
  if solved then
    local upper_applied = Animation.set_bone_segment(self.entity_id, leg.upper, {
      start = root,
      tail = solved.joint,
      pole = pole,
    })
    local lower_applied = Animation.set_bone_segment(self.entity_id, leg.lower, {
      start = solved.joint,
      tail = solved.end_position,
      pole = pole,
    })
    if not upper_applied or not lower_applied then
      error("The procedural pose could not update bone " .. leg.upper)
    end
  end
end

function Spider:on_create()
  self.travel = 0.0
  self.gait = 0.0
  self:create_terrain()
  self:create_rig()
  Debug.log("Procedural spider created: 8 raycast feet, 16 runtime IK bones.")
end

function Spider:on_update(dt)
  self.travel = self.travel + self.speed * dt
  self.gait = self.gait + self.speed * dt / 0.72
  local body, forward, normal, mode = self:surface_frame()
  if self.smoothed_body then
    body = Vector3.lerp(self.smoothed_body, body,
      1.0 - math.exp(-math.max(dt, 0.0) * 8.0))
  end
  self.smoothed_body = body

  Transform3D.set_position(self.entity_id, body[1], body[2], body[3])
  Transform3D.set_rotation(self.entity_id, 0.0, 0.0,
    math.atan(forward[2], forward[1]))
  for _, leg in ipairs(self.legs) do
    self:update_leg(leg, body, forward, normal)
  end
  Hud.set_text("mode", mode)
end

return Spider
