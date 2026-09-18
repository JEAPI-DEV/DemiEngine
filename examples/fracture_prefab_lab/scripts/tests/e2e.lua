local Destruction3D = require("demi.physics.destruction3d")
local Physics3D = require("demi.physics.query3d")
local Test = require("demi.test")
local Vector3 = require("demi.math.vector3")

return { tests = {{ name = "generated prefab instances fracture independently", func = function()
  Test.wait(0.3)
  Test.expect(Destruction3D.state("wall_a/wall").bodies == 1, "First wall must start as one body")
  Test.expect(Destruction3D.state("wall_b/wall").bodies == 1, "Second wall must start as one body")
  local hit = Physics3D.raycast(-3.2, 1.5, 5, 0, 0, -1, 10)
  Test.expect(hit and hit.collider_part_id, "Generated geometry must be raycastable")
  local state = Destruction3D.state("wall_a/wall")
  -- Use the probe's actual strike budget. Energy is shared across contacts and
  -- bonds; repeated strikes, rather than per-bond duplicated energy, open a hole.
  for strike = 1, 3 do
    local revision = state.revision
    local ok, issue, affected = Destruction3D.impact({
      entity = hit.entity_id, position = hit.point, radius = 0.3,
      energy = 6000, impulse = 180, direction = {0,0,-1},
    })
    Test.expect(ok, issue)
    Test.expect(affected == 1, "Local impact must queue exactly one assembly")
    local invalid = Destruction3D.impact({ position = {1,2}, energy = 1000 })
    Test.expect(not invalid, "Invalid vector must not replace the queued hit")
    Test.wait(0.15)
    state = Destruction3D.state("wall_a/wall")
    Test.expect(state.status == "applied" and state.revision == revision + 1,
      "Each strike must commit: " .. state.status .. " " .. state.error)
    if state.bodies > 1 then break end
  end
  Test.expect(state.bodies > 1,
    "Generated bond graph must split: status=" .. state.status .. " revision=" .. state.revision .. " bodies=" .. state.bodies .. " " .. state.error)
  Test.expect(Destruction3D.state("wall_b/wall").revision == 0, "Other instance must remain intact")
  local first_revision = state.revision
  local second = Physics3D.raycast(2.5, 1.5, 5, 0, 0, -1, 10)
  Test.expect(second ~= nil, "Second wall must remain raycastable")
  for blast = 1, 3 do
    local ok, issue = Destruction3D.impact({
      position = Vector3.add(second.point, Vector3.scale(second.normal, 0.1)),
      radius = 1.2, energy = 24000, impulse = 1200,
    })
    Test.expect(ok, issue)
    Test.wait(0.15)
    state = Destruction3D.state("wall_b/wall")
    Test.expect(state.status == "applied", "Radial blast must commit: " .. state.error)
    if state.bodies > 1 then break end
  end
  Test.expect(state.bodies > 1, "Repeated radial blasts must fracture the second wall")
  Test.expect(Destruction3D.state("wall_a/wall").revision == first_revision,
    "Out-of-radius assembly must not receive blast damage")
end }} }
