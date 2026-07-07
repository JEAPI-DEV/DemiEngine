local Platforms = {}

function Platforms.create_static_box(id, name, x, y, width, height)
  Entity.create(id, {
    name = name,
    components = {
      Transform2D = {
        position = { x, y },
        rotation = 0.0,
        scale = { 1.0, 1.0 },
      },
      Rigidbody2D = {
        body_type = "static",
        velocity = { 0.0, 0.0 },
        gravity_scale = 0.0,
        lock_rotation = true,
      },
      BoxCollider2D = {
        size = { width, height },
        offset = { 0.0, 0.0 },
        is_trigger = false,
        layer = "platform",
      },
    },
  })
end

function Platforms.remember(platforms, id, x, y, width, height)
  platforms[id] = { x = x, y = y, width = width, height = height }
end

return Platforms
