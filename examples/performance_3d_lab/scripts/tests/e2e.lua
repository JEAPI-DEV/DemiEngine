local MeshDeformation = require("demi.mesh_deformation")
local Entity = require("demi.entity")
local Transform3D = require("demi.transform3d")
local Physics3D = require("demi.physics3d")
local Hud = require("demi.hud")
local Test = require("demi.test")

local tests = {}

tests[#tests + 1] = {
  name = "native mesh dent API and demo preserve rigid collision",
  func = function()
    Test.touch("open_denting")
    Test.expect_scene("scene://performance_3d_lab/denting", 10)
    Test.wait(0.2)
    local denied = MeshDeformation.dent("reference_barrel", {
      point = {-0.7, 0.475, 0.35}, direction = {0,0,-1}, radius = 0.32, depth = 0.12,
    })
    Test.expect(not denied, "Unmarked reference barrel must reject dent requests")
    local before = Physics3D.raycast(0.7, 0.475, 2, 0, 0, -1, 4)
    Test.expect(before and before.entity_id == "dent_barrel", "Barrel proxy must be present")
    local ok, err = MeshDeformation.dent("dent_barrel", {
      point = {0.7, 0.475, 0.35}, direction = {0, 0, -1}, radius = 0.32, depth = 0.12,
    })
    Test.expect(ok, err)
    Test.wait(0.2)
    local after = Physics3D.raycast(0.7, 0.475, 2, 0, 0, -1, 4)
    Test.expect(after and math.abs(before.distance - after.distance) < 0.0001,
      "Visual damage must not silently alter collision")
    Test.expect(MeshDeformation.reset("dent_barrel"), "Reset must succeed")
    Test.touch("dent_hit")
    Test.wait(0.8)
    local status = Hud.get_text("dent_status")
    Test.expect(status ~= nil and status:find("Impact 64.0 J", 1, true) ~= nil,
      "A real 2 kg projectile at 8 m/s must report pre-solver kinetic energy")
    Test.touch("dent_reset")
    Test.touch("dent_speed")
    Test.touch("dent_hit")
    Test.wait(0.8)
    status = Hud.get_text("dent_status")
    Test.expect(status ~= nil and status:find("Impact 256.0 J", 1, true) ~= nil,
      "Doubling real projectile speed must quadruple normal impact energy")
    Test.touch("dent_reset")
    Test.touch("dent_mass")
    Test.touch("dent_hit")
    Test.wait(0.8)
    status = Hud.get_text("dent_status")
    Test.expect(status ~= nil and status:find("Impact 1024.0 J", 1, true) ~= nil,
      "Quadrupling projectile mass must quadruple measured energy")
    Test.touch("dent_reset")
    Test.touch("dent_back")
    Test.expect_scene("scene://performance_3d_lab/main", 10)
  end,
}

tests[#tests + 1] = {
  name = "barrel convex proxy rests upright and on its side",
  func = function()
    for _, case in ipairs({
      { id = "test_barrel_upright", x = -2, angle = 0, height = -0.025 },
      { id = "test_barrel_side", x = 2, angle = math.pi / 2, height = -0.15 },
    }) do
      local created = Entity.create(case.id, { components = {
        Transform3D = { position = { case.x, 3, 15 }, rotation = { 0, 0, case.angle } },
        MeshRenderer = { model = "asset://models/barrel", color = { 1, 1, 1, 1 } },
        Rigidbody3D = { body_type = "dynamic" },
        ModelCollider3D = { asset = "asset://colliders/barrel" },
      } })
      Test.expect(created ~= nil, "Collider asset must work without a prefab")
      Test.wait(2)
      local _, y = Transform3D.get_position(case.id)
      Test.expect(y ~= nil and math.abs(y - case.height) < 0.06,
        "Barrel must rest at its authored hull height")
      if case.angle == 0 then
        local center = Physics3D.raycast(case.x, 2, 15, 0, -1, 0, 4)
        local corner = Physics3D.raycast(case.x + 0.3, 2, 15.3, 0, -1, 0, 4)
        Test.expect(center and center.entity_id == case.id,
          "Ray through the barrel center must hit its collider")
        Test.expect(corner and corner.entity_id == "floor",
          "Ray outside the round hull must miss the barrel's bounding-box corner")
      end
      Test.expect(Entity.destroy(case.id), "Barrel entity must destroy")
      Test.wait(0.1)
    end
  end,
}

tests[#tests + 1] = {
  name = "projectile removes tower support and gravity collapses upper barrels",
  func = function()
    Test.touch("open_tower")
    Test.expect_scene("scene://performance_3d_lab/tower", 10)
    Test.wait(0.1)
    Hud.set_text("tower_help", "AUTOMATED TEST - please do not interact")
    local tops = Entity.query({ tags = { "tower_top" } })
    Test.expect(#tops > 0, "Tower must contain tagged top barrels")
    local _, initial_top_y = Transform3D.get_position(tops[1])
    Test.wait(12)
    for _, id in ipairs(tops) do
      local _, y = Transform3D.get_position(id)
      Test.expect(y and y > initial_top_y - 0.5, "Every top barrel must remain supported before firing")
    end
    Test.touch("tower_fire")
    Test.wait(0.2)
    local x = Transform3D.get_position("tower_projectile_1")
    Test.expect(x ~= nil, "Fire button must create a real projectile")
    Test.wait(5)
    local fallen = 0
    local drop = math.min(2, (initial_top_y + 0.5) * 0.5)
    for _, id in ipairs(tops) do
      local _, y = Transform3D.get_position(id)
      if y and y < initial_top_y - drop then fallen = fallen + 1 end
    end
    Test.expect(fallen > 0, "Upper barrels must fall after the base is hit")
    Test.touch("tower_reset")
    Test.wait(4)
    for _, id in ipairs(tops) do
      local _, y = Transform3D.get_position(id)
      Test.expect(y and y > initial_top_y - 0.5, "Reset must rebuild a standing tower")
    end
    Test.touch("tower_back")
    Test.expect_scene("scene://performance_3d_lab/main", 10)
    Test.wait(2)
    local _, y = Transform3D.get_position("probe_1")
    Test.expect(y and y < 1 and y > -1, "Collider assets must survive scene transitions")
  end,
}

return { tests = tests }
