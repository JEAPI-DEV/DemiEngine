local Wireframe = {}

local stars = {}
for i = 1, 48 do
  stars[i] = {
    x = ((i * 37) % 200 - 100) / 10,
    y = ((i * 71) % 120 - 60) / 10,
    z = 1 + ((i * 23) % 80) / 10,
  }
end

local station_edges = {
  {1, 2}, {2, 3}, {3, 4}, {4, 1},
  {5, 6}, {6, 7}, {7, 8}, {8, 5},
  {1, 5}, {2, 6}, {3, 7}, {4, 8},
}

local cube = {
  {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
  {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
}

local function project(x, y, z)
  local scale = 8.8 / math.max(z, 0.35)
  return x * scale, y * scale
end

local function line3(a, b, offset_x, offset_y, z, r, g, bl, alpha)
  local ax, ay = project(a[1], a[2], a[3] + z)
  local bx, by = project(b[1], b[2], b[3] + z)
  Debug.line(offset_x + ax, offset_y + ay, offset_x + bx, offset_y + by, r, g, bl, alpha)
end

local function draw_station(distance, roll)
  local z = math.max(1.2, distance)
  local c = math.cos(roll)
  local s = math.sin(roll)
  local rotated = {}
  for i, p in ipairs(cube) do
    rotated[i] = {
      p[1] * c - p[2] * s,
      p[1] * s + p[2] * c,
      p[3],
    }
  end
  for _, edge in ipairs(station_edges) do
    line3(rotated[edge[1]], rotated[edge[2]], 0, 0, z, 0.54, 1.0, 0.82, 1.0)
  end
end

function Wireframe.draw(state, dt)
  Debug.clear_lines()
  if state.mode == "station" then
    return
  end

  for _, star in ipairs(stars) do
    star.z = star.z - math.max(0.2, state.ship.speed * 0.012) * dt
    if star.z < 0.35 then
      star.z = 9.0
    end
    local sx, sy = project(star.x + math.sin(state.ship.yaw) * 2.0, star.y + state.ship.pitch * 2.0, star.z)
    Debug.line(sx - 0.05, sy, sx + 0.05, sy, 0.82, 0.92, 1.0, 1.0)
    Debug.line(sx, sy - 0.05, sx, sy + 0.05, 0.82, 0.92, 1.0, 1.0)
  end

  Debug.line(-1.25, 0.0, 1.25, 0.0, 0.30, 1.0, 0.78, 0.85)
  Debug.line(0.0, -0.9, 0.0, 0.9, 0.30, 1.0, 0.78, 0.85)
  draw_station(state.ship.station_distance, state.ship.roll)
end

return Wireframe
