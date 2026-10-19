-- Attach to an unparented mount with Transform3D, CapsuleCollider3D and
-- CharacterController3D. Keep horse/rider visuals as collider-free children;
-- their model forward axis is +Z. The camera is a separate unparented entity.
-- riding_move uses the wasd_arrows convention (W is negative Y), riding_gallop
-- is a Shift button, and riding_reset is an R button. Horse animation states
-- idle/walk/run map to the imported clips through AnimationStateMachine.
local Application = require("demi.application")
local Animation = require("demi.animation")
local CharacterController3D = require("demi.physics.character_controller3d")
local Events = require("demi.events")
local Hud = require("demi.hud")
local Input = require("demi.input")
local Physics3D = require("demi.physics.query3d")
local Transform3D = require("demi.transform3d")

---@demi_component
---@display_name Mounted Riding Controller
---@category Gameplay
---@description Fixed-step capsule riding with a collision-aware orbit camera.
local RidingController = {}

---@demi_property
RidingController.walk_speed = 4.0
---@demi_property
RidingController.gallop_speed = 9.0
---@demi_property
RidingController.acceleration = 7.0
---@demi_property
RidingController.braking = 40.0
---@demi_property
RidingController.turn_speed = 1.65
---@demi_property
RidingController.horse_visual_id = "horse"
---@demi_property
RidingController.camera_id = "camera"
---@demi_property
RidingController.camera_distance = 7.5
---@demi_property
RidingController.camera_height = 1.8
---@demi_property
RidingController.mouse_sensitivity = 0.003

local function approach(current, target, step)
  if current < target then
    return math.min(target, current + step)
  end
  return math.max(target, current - step)
end

local function angle_difference(target, current)
  return (target - current + math.pi) % (2.0 * math.pi) - math.pi
end

function RidingController:on_create()
  self.spawn_x, self.spawn_y, self.spawn_z = Transform3D.get_position(self.entity_id)
  local _, yaw = Transform3D.get_rotation(self.entity_id)
  self.spawn_yaw = yaw or 0.0
  self.yaw = self.spawn_yaw
  self.speed = 0.0
  self.steering = 0.0
  self.throttle = 0.0
  self.galloping = false
  self.reset_requested = false
  self.looking = false
  self.camera_yaw = self.yaw
  self.camera_pitch = 0.25
  self.last_x = self.spawn_x
  self.last_z = self.spawn_z
  self.animation_state = ""
  -- Physics owns translation, so imported clips must remain in-place.
  Animation.set_root_motion(self.horse_visual_id, false)
end

function RidingController:on_start()
  self.touch_controls = false
  self.ui_subscription = Events.subscribe("ui_event", function(event)
    if event.type == "submit" and event.id == "touch_toggle" then
      self.touch_controls = not self.touch_controls
      Hud.set_visible("riding_stick", self.touch_controls)
      Hud.set_visible("riding_forward", self.touch_controls)
    end
  end)
end

function RidingController:reset_mount()
  Transform3D.set_position(self.entity_id, self.spawn_x, self.spawn_y, self.spawn_z)
  Transform3D.set_rotation(self.entity_id, 0.0, self.spawn_yaw, 0.0)
  CharacterController3D.set_velocity(self.entity_id, 0.0, 0.0, 0.0)
  self.yaw = self.spawn_yaw
  self.camera_yaw = self.yaw
  self.speed = 0.0
  self.last_x = self.spawn_x
  self.last_z = self.spawn_z
  self.reset_requested = false
end

function RidingController:update_animation(dt)
  local x, _, z = Transform3D.get_position(self.entity_id)
  if x == nil or dt <= 0.0 then
    return
  end
  local distance_x = x - self.last_x
  local distance_z = z - self.last_z
  local actual_speed = math.sqrt(distance_x * distance_x + distance_z * distance_z) / dt
  self.last_x = x
  self.last_z = z

  local state = "idle"
  if actual_speed > self.walk_speed + 1.0 then
    state = "run"
  elseif actual_speed > 0.15 then
    state = "walk"
  end
  -- Enter only on a state change; replaying every frame restarts the clip.
  if state ~= self.animation_state then
    if Animation.play(self.horse_visual_id, state) then
      self.animation_state = state
    end
  end
end

function RidingController:update_camera(dt)
  local target_x, target_y, target_z = Transform3D.get_world_position(self.entity_id)
  if target_x == nil then
    return
  end
  target_y = target_y + self.camera_height

  local looking = Input.mouse_down("right") and not Input.ui_pointer_captured()
  if looking ~= self.looking then
    self.looking = looking
    Application.set_mouse_captured(looking)
  end
  if looking then
    local mouse_x, mouse_y = Input.mouse_delta()
    self.camera_yaw = self.camera_yaw - mouse_x * self.mouse_sensitivity
    self.camera_pitch = math.max(0.05, math.min(0.8,
      self.camera_pitch + mouse_y * self.mouse_sensitivity))
  elseif math.abs(self.speed) > 0.1 then
    local follow_weight = 1.0 - math.exp(-2.5 * dt)
    self.camera_yaw = self.camera_yaw + angle_difference(self.yaw, self.camera_yaw) * follow_weight
  end

  local horizontal = math.cos(self.camera_pitch)
  local direction_x = -math.sin(self.camera_yaw) * horizontal
  local direction_y = math.sin(self.camera_pitch)
  local direction_z = -math.cos(self.camera_yaw) * horizontal
  local distance = self.camera_distance
  local hit = Physics3D.sphere_cast(target_x, target_y, target_z, 0.25,
    direction_x, direction_y, direction_z, distance, "", self.entity_id, false)
  if hit then
    -- Do not impose a minimum boom length: that would push through close walls.
    distance = math.max(0.0, math.min(distance, hit.distance - 0.12))
  end
  Transform3D.set_position(self.camera_id,
    target_x + direction_x * distance,
    target_y + direction_y * distance,
    target_z + direction_z * distance)
  Transform3D.look_at(self.camera_id, target_x, target_y, target_z)
end

function RidingController:on_update(dt)
  local steering, forward = Input.vector("riding_move")
  self.steering = steering
  self.throttle = -forward
  self.galloping = Input.down("riding_gallop")
  if Input.pressed("riding_reset") then
    self.reset_requested = true
  end
  self:update_camera(dt)
end

function RidingController:on_fixed_update(dt)
  local _, height = Transform3D.get_position(self.entity_id)
  if self.reset_requested or (height and height < self.spawn_y - 30.0) then
    self:reset_mount()
    return
  end

  -- Sample completed physics movement once per fixed step, independent of render FPS.
  self:update_animation(dt)

  local maximum_speed = self.galloping and self.gallop_speed or self.walk_speed
  if self.throttle < 0.0 then
    maximum_speed = self.walk_speed * 0.45
  end
  local target_speed = self.throttle * maximum_speed
  local changing_direction = self.speed * target_speed < 0.0
  local slowing_down = math.abs(target_speed) < math.abs(self.speed)
  local speed_change = self.acceleration
  if changing_direction or slowing_down then
    speed_change = self.braking
  end
  if changing_direction then
    target_speed = 0.0
  end
  self.speed = approach(self.speed, target_speed, speed_change * dt)
  -- Steering remains available at rest; reversing keeps natural rein direction.
  local reverse_direction = self.speed < -0.1 and -1.0 or 1.0
  -- The camera uses right = forward cross up: looking along +Z, screen right
  -- is -X. Negative yaw therefore turns the mount toward the D/right input.
  self.yaw = self.yaw - self.steering * self.turn_speed * reverse_direction * dt
  Transform3D.set_rotation(self.entity_id, 0.0, self.yaw, 0.0)
  CharacterController3D.set_velocity(self.entity_id,
    math.sin(self.yaw) * self.speed, 0.0, math.cos(self.yaw) * self.speed)
end

function RidingController:on_destroy()
  if self.ui_subscription then
    Events.unsubscribe(self.ui_subscription)
  end
  Application.set_mouse_captured(false)
end

return RidingController
