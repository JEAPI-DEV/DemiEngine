local chunks = require("worldgen.chunks")
local config = require("worldgen.config")
local terrain = require("worldgen.terrain")

local Interaction = {}

local function camera_direction()
  local pitch, yaw = Transform3D.get_rotation(config.camera_id)
  pitch = pitch or 0.0
  yaw = yaw or 0.0
  local cos_pitch = math.cos(pitch)
  return {
    x = -math.sin(yaw) * cos_pitch,
    y = math.sin(pitch),
    z = -math.cos(yaw) * cos_pitch,
  }
end

function Interaction.raycast_block(world)
  local ox, oy, oz = Transform3D.get_position(config.camera_id)
  if ox == nil or oy == nil or oz == nil then
    return nil
  end
  local direction = camera_direction()
  local previous = nil
  local distance = 0.0
  while distance <= config.interaction_range do
    local sx = ox + direction.x * distance
    local sy = oy + direction.y * distance
    local sz = oz + direction.z * distance
    local bx = math.floor(sx)
    local by = math.floor(sy)
    local bz = math.floor(sz)
    if terrain.block_at(world, bx, by, bz) ~= 0 then
      local normal = { x = 0, y = 1, z = 0 }
      if previous ~= nil then
        normal = {
          x = previous.x - bx,
          y = previous.y - by,
          z = previous.z - bz,
        }
      end
      return { x = bx, y = by, z = bz, normal = normal }
    end
    previous = { x = bx, y = by, z = bz }
    distance = distance + config.ray_step
  end
  return nil
end

function Interaction.set_selection_visible(hit)
  if hit == nil then
    Transform3D.set_position(config.selection_id, 0.0, -10000.0, 0.0)
    return
  end
  Transform3D.set_position(config.selection_id, hit.x + 0.5, hit.y + 0.5, hit.z + 0.5)
end

function Interaction.create_selection()
  Entity.create(config.selection_id, {
    name = "Selected Block",
    components = {
      Transform3D = {
        position = { 0.0, -10000.0, 0.0 },
        rotation = { 0.0, 0.0, 0.0 },
        scale = { 1.0, 1.0, 1.0 },
      },
      MeshRenderer = {
        shape = "cube",
        size = { 1.04, 1.04, 1.04 },
        color = { 1.0, 1.0, 1.0, 0.08 },
        wireframe = true,
      },
    },
  })
end

function Interaction.break_block(world, hit)
  if hit == nil then
    return
  end
  local height = terrain.column_height(world, hit.x, hit.z)
  if hit.y > height then
    return
  end
  world.column_heights[terrain.column_key(hit.x, hit.z)] = math.max(hit.y - 1, -1)
  chunks.rebuild_column(world, hit.x, hit.z)
end

function Interaction.place_block(world, hit)
  if hit == nil then
    return
  end
  local place_x = hit.x + hit.normal.x
  local place_y = hit.y + hit.normal.y
  local place_z = hit.z + hit.normal.z
  if place_y < 0 or place_y >= config.chunk_height then
    return
  end
  local height = terrain.column_height(world, place_x, place_z)
  if place_y <= height then
    return
  end
  world.column_heights[terrain.column_key(place_x, place_z)] = place_y
  chunks.rebuild_column(world, place_x, place_z)
end

function Interaction.update(world)
  local hit = Interaction.raycast_block(world)
  Interaction.set_selection_visible(hit)

  local left_down = Input.mouse_down("left")
  local right_down = Input.mouse_down("right")
  if left_down and not world.left_mouse_down then
    Interaction.break_block(world, hit)
    hit = Interaction.raycast_block(world)
    Interaction.set_selection_visible(hit)
  elseif right_down and not world.right_mouse_down then
    Interaction.place_block(world, hit)
    hit = Interaction.raycast_block(world)
    Interaction.set_selection_visible(hit)
  end
  world.left_mouse_down = left_down
  world.right_mouse_down = right_down
  return hit
end

return Interaction
