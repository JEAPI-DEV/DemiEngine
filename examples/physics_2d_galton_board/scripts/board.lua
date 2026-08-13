local Board = {}

Board.rows = 12
Board.bin_count = Board.rows + 1
Board.spacing = 1.05
Board.release_x = 0.0
Board.release_y = 8.0

local function static_body()
  return {
    body_type = "static",
    gravity_scale = 0.0,
    lock_rotation = true,
  }
end

local function create_box(id, x, y, width, height, rotation, color)
  assert(Entity.create(id, {
    components = {
      Transform2D = {
        position = { x, y },
        rotation = rotation or 0.0,
      },
      Sprite = {
        shape = "rectangle",
        size = { width, height },
        color = color,
        layer = "board",
      },
      Rigidbody2D = static_body(),
      BoxCollider2D = {
        size = { width, height },
        friction = 0.22,
        restitution = 0.15,
        layer = "board",
        debug_visible = false,
      },
    },
  }))
end

local function create_peg(id, x, y)
  assert(Entity.create(id, {
    components = {
      Transform2D = { position = { x, y } },
      Sprite = {
        shape = "circle",
        size = { 0.28, 0.28 },
        color = { 1.0, 0.52, 0.18, 1.0 },
        layer = "pegs",
        sorting_order = 2,
      },
      Rigidbody2D = static_body(),
      CircleCollider2D = {
        radius = 0.14,
        friction = 0.04,
        restitution = 0.32,
        layer = "peg",
        debug_visible = false,
      },
    },
  }))
end

function Board.create()
  local wall_color = { 0.25, 0.48, 0.62, 1.0 }
  local divider_color = { 0.18, 0.36, 0.5, 1.0 }

  create_box("board_floor", 0.0, -8.65, 15.0, 0.35, 0.0, wall_color)
  create_box("board_left_wall", -7.35, -0.3, 0.3, 16.4, 0.0, wall_color)
  create_box("board_right_wall", 7.35, -0.3, 0.3, 16.4, 0.0, wall_color)
  create_box("board_funnel_left", -2.65, 7.65, 5.5, 0.22, -0.78, wall_color)
  create_box("board_funnel_right", 2.65, 7.65, 5.5, 0.22, 0.78, wall_color)

  for row = 0, Board.rows - 1 do
    local y = 5.85 - row * 0.78
    local first_x = -row * Board.spacing * 0.5
    for column = 0, row do
      create_peg("peg_" .. row .. "_" .. column,
        first_x + column * Board.spacing, y)
    end
  end

  local first_divider = -Board.bin_count * Board.spacing * 0.5
  for divider = 0, Board.bin_count do
    create_box("bin_divider_" .. divider,
      first_divider + divider * Board.spacing, -7.15,
      0.11, 2.65, 0.0, divider_color)
  end
end

function Board.bin_for_x(x)
  local left = -Board.bin_count * Board.spacing * 0.5
  local bin = math.floor((x - left) / Board.spacing) + 1
  return math.max(1, math.min(Board.bin_count, bin))
end

return Board
