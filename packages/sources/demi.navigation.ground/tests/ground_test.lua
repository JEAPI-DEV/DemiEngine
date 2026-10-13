local Test = require("demi.test")
local facing
package.preload["demi.transform3d"] = function()
  return {
    look_at = function(_, x, y, z)
      facing = { x, y, z }
      return true
    end,
  }
end
local samples, position, enabled, velocity, path_calls = 0, { 0.5, 1, 0.5 }, true, nil, 0
package.preload["demi.entity"] = function()
  return {
    exists = function()
      return position ~= nil
    end,
    is_enabled = function()
      return enabled
    end,
    world_position = function()
      return position
    end,
    parent = function()
      return "terrain"
    end,
  }
end
package.preload["demi.physics.query3d"] = function()
  return {
    raycast = function(x, _, z)
      samples = samples + 1
      return { entity_id = "surface", point = { x, 0, z }, normal = { 0, 1, 0 } }
    end,
  }
end
package.preload["demi.physics.character_controller3d"] = function()
  return {
    set_velocity = function(_, x, y, z)
      velocity = { x, y, z }
      return true
    end,
  }
end
package.preload["demi.navigation"] = function()
  return {
    create_grid = function(w, h, size)
      local grid = { flags = {} }
      function grid:set_blocked(x, z, blocked)
        self.flags[z * w + x] = blocked
      end
      function grid:blocked(x, z)
        return x < 0 or z < 0 or x >= w or z >= h or self.flags[z * w + x]
      end
      function grid:cell_to_world(x, z)
        if x >= 0 and z >= 0 and x < w and z < h then
          return (x + 0.5) * size, (z + 0.5) * size
        end
      end
      function grid:world_to_cell(x, z)
        return math.floor(x / size), math.floor(z / size)
      end
      function grid:path(sx, sz, gx, gz)
        path_calls = path_calls + 1
        if self:blocked(gx, gz) then
          return {}, "PATH_GOAL_BLOCKED"
        end
        return {
          { sx, sz, world_x = (sx + 0.5) * size, world_y = (sz + 0.5) * size },
          { gx, gz, world_x = (gx + 0.5) * size, world_y = (gz + 0.5) * size },
        },
          "OK"
      end
      return grid
    end,
  }
end
local Ground = require("demi.navigation.ground")
local function surface()
  return Ground.create({ terrain = "terrain", columns = 4, rows = 4, cell_size = 1 })
end
Test.case("survey work is bounded and obstacle inputs are copied", function()
  samples = 0
  local nav = surface()
  local path, reason = nav:path({ 0.5, 0, 0.5 }, { 3.5, 0, 0.5 })
  Test.equal(#path, 0)
  Test.equal(reason, "SURFACE_NOT_READY")
  Test.equal(nav:update(2), false)
  Test.equal(samples, 10)
  Test.truthy(nav:update(14))
  local boxes = { { min_x = 1, max_x = 2, min_z = 1, max_z = 2 } }
  nav:set_obstacles(boxes)
  boxes[1].min_x = 99
  Test.equal(nav.obstacles[1].min_x, 1)
  Test.truthy(nav.grid:blocked(1, 1))
  nav:set_obstacles({})
  Test.equal(nav.grid:blocked(1, 1), false)
end)
Test.case("stalled actors report blocked rather than arriving", function()
  position, enabled = { 0.5, 1, 0.5 }, true
  local nav = surface()
  nav:update(16)
  local agent = nav:agent("worker", { stuck_timeout = 1 })
  Test.truthy(agent:move_to({ 3.5, 0, 0.5 }))
  for _ = 1, 20 do
    agent:update(0.1)
  end
  Test.equal(agent.state, "blocked")
  Test.equal(velocity[1], 0)
end)
Test.case("pause and resurvey halt motion; obstacle changes replan", function()
  local nav = surface()
  nav:update(16)
  local agent = nav:agent("worker")
  Test.truthy(agent:move_to({ 3.5, 0, 0.5 }))
  agent:update(0.1)
  enabled = false
  Test.equal(agent:update(0.1), "paused")
  Test.equal(velocity[1], 0)
  enabled = true
  local before = path_calls
  nav:set_obstacles({})
  agent:update(0.1)
  Test.truthy(path_calls > before)
  nav:invalidate()
  Test.equal(agent:update(0.1), "surveying")
  Test.equal(velocity[1], 0)
  agent:stop()
  Test.equal(agent.state, "idle")
end)
Test.case("invalid settings and missing actors have explicit failures", function()
  Test.equal(
    pcall(function()
      Ground.create({ terrain = "terrain", agent_radius = -1 })
    end),
    false
  )
  local nav = surface()
  nav:update(16)
  local agent = nav:agent("missing")
  position = nil
  local ok, reason = agent:move_to({ 1, 0, 1 })
  Test.equal(ok, false)
  Test.equal(reason, "AGENT_UNAVAILABLE")
  position = { 0.5, 1, 0.5 }
end)

Test.case("routes start at the actor and facing is opt in", function()
  local nav = surface()
  nav:update(16)
  position, enabled = { 0.8, 1, 0.8 }, true
  nav.heights[0] = nil
  nav.grid:set_blocked(0, 0, true)
  local points = nav:path(position, { 3.5, 0, 0.5 })
  Test.equal(points[1][1], 0.8)
  Test.equal(points[1][2], 1)
  Test.equal(points[1][3], 0.8)
  local agent = nav:agent("worker", { face_movement = true })
  Test.truthy(agent:move_to({ 3.5, 0, 0.5 }))
  agent:update(0.1)
  agent:update(0.1)
  Test.truthy(facing)
  Test.equal(facing[2], position[2])
end)

local Route = require("demi.navigation.ground_route")
Test.case("open ground routes are direct and preserve off-grid goals", function()
  local nav = surface()
  nav:update(16)
  local route = Route.simplify(
    nav,
    { { 0.5, 0, 0.5 }, { 1.5, 0, 0.5 }, { 2.5, 0, 0.5 }, { 2.5, 0, 1.5 }, { 2.5, 0, 2.5 } }
  )
  Test.equal(#route, 2)
  local points = nav:path({ 0.7, 0, 0.8 }, { 2.8, 0, 2.2 })
  Test.equal(#points, 2)
  Test.equal(points[2][1], 2.8)
  Test.equal(points[2][3], 2.2)
end)
Test.case("shortcuts retain detours and cannot cross blocked corners", function()
  local nav = surface()
  nav:update(16)
  nav.grid:set_blocked(1, 1, true)
  Test.equal(Route.clear(nav, { 0.5, 0, 0.5 }, { 2.5, 0, 2.5 }), false)
  Test.equal(Route.clear(nav, { 0.5, 0, 1.5 }, { 1.5, 0, 2.5 }), false)
  local route = Route.simplify(
    nav,
    { { 0.5, 0, 1.5 }, { 0.5, 0, 2.5 }, { 1.5, 0, 2.5 }, { 2.5, 0, 2.5 }, { 2.5, 0, 1.5 } }
  )
  Test.truthy(#route > 2)
  for i = 2, #route do
    Test.truthy(Route.clear(nav, route[i - 1], route[i]))
  end
  Test.equal(Route.clear(nav, { 1, 0, 0.5 }, { 1, 0, 2.5 }), false)
end)
Test.case("footprint clearance catches obstacles between survey centres", function()
  local nav = surface()
  nav:update(16)
  nav.radius = 0.1
  nav:set_obstacles({ { min_x = 1.9, max_x = 2.1, min_z = 0.9, max_z = 1.1 } })
  Test.equal(nav.grid:blocked(1, 0), false)
  Test.equal(Route.clear(nav, { 0.5, 0, 0.85 }, { 3.5, 0, 0.85 }), false)
  Test.truthy(Route.clear(nav, { 0.5, 0, 0.5 }, { 3.5, 0, 0.5 }))
end)
Test.case("agents move at arbitrary angles without a frame of stale velocity", function()
  position, enabled = { 0.7, 1, 0.8 }, true
  local nav = surface()
  nav:update(16)
  local agent = nav:agent("worker")
  Test.truthy(agent:move_to({ 2.8, 0, 2.2 }))
  agent:update(0.1)
  Test.truthy(velocity[1] > 0 and velocity[3] > 0)
  Test.truthy(math.abs(velocity[1] / velocity[3] - 1.5) < 0.0001)
  position = { 2.8, 1, 2.2 }
  Test.equal(agent:update(0.1), "arrived")
  Test.equal(velocity[1], 0)
end)

Test.case("segments ending on grid boundaries work in both directions", function()
  local nav = surface()
  nav:update(16)
  for _, pair in ipairs({
    { { 0.5, 0, 2.5 }, { 2, 0, 1 } },
    { { 2.5, 0, 0.5 }, { 1, 0, 2 } },
    { { 0.5, 0, 0.5 }, { 2, 0, 2 } },
    { { 2.5, 0, 2.5 }, { 1, 0, 1 } },
  }) do
    Test.truthy(Route.clear(nav, pair[1], pair[2]))
    Test.truthy(Route.clear(nav, pair[2], pair[1]))
  end
end)

Test.case("controller contact noise does not strand intermediate waypoints", function()
  local nav = surface()
  nav:update(16)
  position, enabled = { 0.5, 1, 0.5 }, true
  local agent = nav:agent("worker")
  Test.truthy(agent:move_to({ 3.5, 0, 2.5 }))
  agent.path = { { 0.5, 0, 0.5 }, { 1.5, 0, 0.5 }, { 3.5, 0, 2.5 } }
  agent.index = 2
  position = { 1.4996, 1, 0.5002 }
  agent:update(0.1)
  Test.equal(agent.index, 3)
  Test.truthy(velocity[1] > 0 and velocity[3] > 0)
end)

Test.case("escaping a blocked start cannot shortcut through a footprint", function()
  local nav = surface()
  nav:update(16)
  nav:set_obstacles({ { min_x = 1, max_x = 2, min_z = 1, max_z = 2 } })
  Test.equal(Route.clear(nav, { 0.8, 0, 1.5 }, { 3.5, 0, 1.5 }), false)
  local points =
    { { 0.8, 0, 1.5 }, { 0.5, 0, 1.5 }, { 0.5, 0, 2.5 }, { 2.5, 0, 2.5 }, { 3.5, 0, 1.5 } }
  local route = Route.simplify(nav, points)
  Test.equal(route[2][1], 0.5)
  Test.equal(route[2][3], 1.5)
end)
