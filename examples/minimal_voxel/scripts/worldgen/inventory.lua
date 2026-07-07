local config = require("worldgen.config")

local Inventory = {}

local slot_count = 36
local hotbar_count = 9
local max_stack = 64
local canvas_width = 960.0
local canvas_height = 540.0
local empty_label = ""

local block_colors = {
  [1] = { 0.28, 0.62, 0.20 },
  [2] = { 0.45, 0.29, 0.18 },
  [3] = { 0.48, 0.48, 0.48 },
  [4] = { 0.50, 0.27, 0.12 },
  [5] = { 0.13, 0.32, 0.12 },
  [6] = { 0.78, 0.70, 0.45 },
  [7] = { 0.04, 0.13, 0.10 },
  [8] = { 0.64, 0.27, 0.10 },
  [9] = { 0.34, 0.70, 0.22 },
  [10] = { 0.95, 0.84, 0.06 },
  [11] = { 0.95, 0.95, 0.88 },
  [12] = { 0.18, 0.65, 0.95 },
}

local function block_tile(block)
  local tiles = config.pack.blocks[block]
  if tiles == nil then
    return nil
  end
  return tiles.top or tiles.side or tiles.bottom
end

local function block_name(block)
  local definition = config.blocks[block]
  return definition and definition.name or ("Block " .. tostring(block))
end

local function block_label(block)
  if block == nil or block == 0 then
    return empty_label
  end
  return string.sub(block_name(block), 1, 3)
end

local function slot_id(prefix, index, suffix)
  return prefix .. "_" .. index .. "_" .. suffix
end

local function clear_stack(stack)
  stack.block = nil
  stack.count = 0
end

local function normalize_stack(stack)
  if stack.count <= 0 then
    clear_stack(stack)
  end
end

local function set_inventory_open(inventory, open)
  if inventory.open == open then
    return
  end
  inventory.open = open
  inventory.dirty = true
  Runtime.set_mouse_captured(not open)
  Hud.set_group_visible("inventory", open)
end

local function canvas_mouse()
  local x, y = Input.mouse_position()
  local width, height = Input.viewport_size()
  width = width or canvas_width
  height = height or canvas_height
  if width > 0 and height > 0 then
    x = (x or 0.0) * (canvas_width / width)
    y = (y or 0.0) * (canvas_height / height)
  end
  return x or 0.0, y or 0.0
end

local function slot_at(x, y)
  local start_x = 320.0
  local slot_size = 32.0
  local stride = 36.0

  for row = 0, 2 do
    local top = 262.0 + (row * stride)
    if y >= top and y <= top + slot_size then
      local column = math.floor((x - start_x) / stride)
      local left = start_x + (column * stride)
      if column >= 0 and column < 9 and x >= left and x <= left + slot_size then
        return (row * 9) + column + 10
      end
    end
  end

  if y >= 390.0 and y <= 422.0 then
    local column = math.floor((x - start_x) / stride)
    local left = start_x + (column * stride)
    if column >= 0 and column < 9 and x >= left and x <= left + slot_size then
      return column + 1
    end
  end

  return nil
end

local function merge_or_swap(carried, slot)
  if carried.block == nil then
    carried.block = slot.block
    carried.count = slot.count
    clear_stack(slot)
    return
  end

  if slot.block == nil then
    slot.block = carried.block
    slot.count = carried.count
    clear_stack(carried)
    return
  end

  if slot.block == carried.block and slot.count < max_stack then
    local moved = math.min(max_stack - slot.count, carried.count)
    slot.count = slot.count + moved
    carried.count = carried.count - moved
    normalize_stack(carried)
    return
  end

  carried.block, slot.block = slot.block, carried.block
  carried.count, slot.count = slot.count, carried.count
end

local function split_or_place_one(carried, slot)
  if carried.block == nil then
    if slot.block == nil then
      return
    end
    local taken = math.ceil(slot.count / 2)
    carried.block = slot.block
    carried.count = taken
    slot.count = slot.count - taken
    normalize_stack(slot)
    return
  end

  if slot.block == nil then
    slot.block = carried.block
    slot.count = 1
    carried.count = carried.count - 1
    normalize_stack(carried)
    return
  end

  if slot.block == carried.block and slot.count < max_stack then
    slot.count = slot.count + 1
    carried.count = carried.count - 1
    normalize_stack(carried)
  end
end

local function stack_count_text(stack)
  if stack.block == nil or stack.count <= 0 then
    return empty_label
  end
  return stack.count > 1 and tostring(stack.count) or empty_label
end

local function set_slot_hud(prefix, index, stack, selected, open)
  local icon_id = slot_id(prefix, index, "icon")
  local tile = block_tile(stack.block)
  local show_icon = prefix == "hotbar" or open
  if tile ~= nil and config.pack.texture ~= "" then
    Hud.set_image(icon_id, config.pack.texture, tile * 16.0, 0.0, 16.0, 16.0)
    Hud.set_visible(icon_id, show_icon)
    Hud.set_text(slot_id(prefix, index, "item"), empty_label)
    Hud.set_color(slot_id(prefix, index, "bg"), 0.08, 0.08, 0.08, prefix == "hotbar" and 0.0 or 1.0)
  else
    local color = block_colors[stack.block]
    if color == nil then
      Hud.set_color(slot_id(prefix, index, "bg"), 0.08, 0.08, 0.08, prefix == "hotbar" and 0.0 or 1.0)
    else
      Hud.set_color(slot_id(prefix, index, "bg"), color[1], color[2], color[3], prefix == "hotbar" and 0.88 or 1.0)
    end
    Hud.set_visible(icon_id, false)
    Hud.set_text(slot_id(prefix, index, "item"), block_label(stack.block))
  end
  Hud.set_text(slot_id(prefix, index, "count"), stack_count_text(stack))
  if prefix == "hotbar" then
    Hud.set_color(slot_id(prefix, index, "select"), 1.0, 1.0, 1.0, 0.0)
    if selected then
      Hud.set_rect("hotbar_select", 202.0 + ((index - 1) * 50.0), 473.0, 60.0, 60.0)
    end
  else
    Hud.set_visible(slot_id(prefix, index, "item"), open)
    Hud.set_visible(slot_id(prefix, index, "count"), open)
    Hud.set_visible(slot_id(prefix, index, "bg"), open)
  end
end

function Inventory.create()
  local inventory = {
    selected = 1,
    slots = {},
    carried = { block = nil, count = 0 },
    open = false,
    left_mouse_down = false,
    right_mouse_down = false,
    dirty = true,
  }

  for index = 1, slot_count do
    inventory.slots[index] = { block = nil, count = 0 }
  end

  for index, block in ipairs(config.hotbar.slots) do
    inventory.slots[index].block = block
    inventory.slots[index].count = max_stack
  end

  return inventory
end

function Inventory.create_hud()
  Hud.set_group_visible("inventory", false)
  Hud.set_group_visible("hotbar", true)
end

function Inventory.is_open(inventory)
  return inventory ~= nil and inventory.open
end

function Inventory.selected_block(inventory)
  local stack = inventory.slots[inventory.selected or 1]
  return stack and stack.block or nil
end

function Inventory.count(inventory, block)
  local total = 0
  for _, stack in ipairs(inventory.slots) do
    if stack.block == block then
      total = total + stack.count
    end
  end
  return total
end

function Inventory.add(inventory, block, amount)
  if block == nil or block == 0 then
    return
  end

  local remaining = math.max(1, amount or 1)
  for _, stack in ipairs(inventory.slots) do
    if stack.block == block and stack.count < max_stack then
      local moved = math.min(max_stack - stack.count, remaining)
      stack.count = stack.count + moved
      remaining = remaining - moved
      if remaining <= 0 then
        inventory.dirty = true
        return
      end
    end
  end

  for _, stack in ipairs(inventory.slots) do
    if stack.block == nil then
      stack.block = block
      stack.count = math.min(max_stack, remaining)
      remaining = remaining - stack.count
      if remaining <= 0 then
        inventory.dirty = true
        return
      end
    end
  end

  inventory.dirty = true
end

function Inventory.consume_selected(inventory)
  local stack = inventory.slots[inventory.selected or 1]
  if stack == nil or stack.block == nil or stack.count <= 0 then
    return nil
  end
  local block = stack.block
  stack.count = stack.count - 1
  normalize_stack(stack)
  inventory.dirty = true
  return block
end

function Inventory.drop_for(block)
  local definition = config.blocks[block]
  return definition and definition.drop or block
end

function Inventory.update_input(inventory)
  if Input.is_pressed("e") then
    set_inventory_open(inventory, not inventory.open)
  end

  for index = 1, hotbar_count do
    if Input.is_pressed(tostring(index)) then
      inventory.selected = index
      inventory.dirty = true
      return
    end
  end

  if not inventory.open then
    inventory.left_mouse_down = Input.mouse_down("left")
    inventory.right_mouse_down = Input.mouse_down("right")
    return
  end

  local x, y = canvas_mouse()
  local slot_index = slot_at(x, y)
  local left_down = Input.mouse_down("left")
  local right_down = Input.mouse_down("right")
  if slot_index ~= nil and left_down and not inventory.left_mouse_down then
    merge_or_swap(inventory.carried, inventory.slots[slot_index])
    inventory.dirty = true
  elseif slot_index ~= nil and right_down and not inventory.right_mouse_down then
    split_or_place_one(inventory.carried, inventory.slots[slot_index])
    inventory.dirty = true
  end
  inventory.left_mouse_down = left_down
  inventory.right_mouse_down = right_down
end

function Inventory.update_hud(inventory)
  if inventory.dirty then
    for index = 1, hotbar_count do
      set_slot_hud("hotbar", index, inventory.slots[index], index == inventory.selected, inventory.open)
    end

    for visual_index = 1, slot_count do
      local slot_index = visual_index <= 27 and visual_index + 9 or visual_index - 27
      set_slot_hud("inventory_slot", visual_index, inventory.slots[slot_index], false, inventory.open)
    end
    inventory.dirty = false
  end

  if inventory.open and inventory.carried.block ~= nil then
    local x, y = canvas_mouse()
    local tile = block_tile(inventory.carried.block)
    if tile ~= nil and config.pack.texture ~= "" then
      Hud.set_image("inventory_carry_icon", config.pack.texture, tile * 16.0, 0.0, 16.0, 16.0)
      Hud.set_rect("inventory_carry_icon", x + 10.0, y + 8.0, 24.0, 24.0)
      Hud.set_visible("inventory_carry_icon", true)
      Hud.text("inventory_carry", inventory.carried.count > 1 and tostring(inventory.carried.count) or "", x + 28.0, y + 24.0, 1.0, 1.0, 1.0, 1.0, 1.0)
    else
      Hud.text("inventory_carry", block_label(inventory.carried.block) .. " " .. tostring(inventory.carried.count), x + 14.0, y + 10.0, 1.2, 1.0, 1.0, 1.0, 1.0)
    end
    Hud.set_visible("inventory_carry", true)
  else
    Hud.set_visible("inventory_carry_icon", false)
    Hud.set_visible("inventory_carry", false)
  end
end

return Inventory
