local Entity = require("demi.entity")
local Transform3D = require("demi.transform3d")
local Destruction3D = require("demi.physics.destruction3d")
local Camera3D = require("demi.camera3d")
local Physics3D = require("demi.physics.query3d")
local Test = require("demi.test")

return { tests = {
  { name = "localized split, anchored collision and reset", func = function()
    Test.wait(0.2)
    Test.expect(Physics3D.raycast(0, 1.5, 5, 0, 0, -1, 10) == nil, "Arch opening must not collide")
    local hit = Physics3D.raycast(-1.5, 1.5, 5, 0, 0, -1, 10)
    Test.expect(hit and hit.entity_id == "arch" and hit.collider_part_id == "left", "Raycast must report the native compound part")
    -- Coordinates inside each visible part at 1280x720, rather than rays
    -- constructed directly in world space (which bypass camera conversion).
    for _, sample in ipairs({{520, 400, "left"}, {760, 400, "right"}, {640, 245, "lintel"}}) do
      local ray = Camera3D.screen_ray("camera", sample[1], sample[2], 1280, 720)
      Test.expect(ray ~= nil, "Camera must produce a screen ray")
      local o, d = ray.origin, ray.direction
      local picked = Physics3D.raycast(o[1], o[2], o[3], d[1], d[2], d[3], 100)
      Test.expect(picked and picked.entity_id == "arch" and picked.collider_part_id == sample[3],
        "Screen ray must pick visible part " .. sample[3])
    end
    Test.touch("impulse")
    Test.wait(0.2)
    local state = Destruction3D.state("arch")
    Test.expect(state.status == "applied" and state.bodies == 3, "Strike must split the native assembly: " .. state.error)
    local beam = state.parts.lintel
    Test.expect(beam and beam ~= state.parts.left, "Beam must have an independent physical owner")
    local x = Transform3D.get_position(beam)
    Test.expect(x > 0.1, "Strike impulse must move the detached beam")
    local anchored = Physics3D.raycast(-1.5, 1.5, 5, 0, 0, -1, 10)
    Test.expect(anchored and anchored.collider_part_id == "left", "Anchored pillar must retain real collision")
    Test.wait(1.5)
    local center = Entity.world_position("arch_lintel")
    Test.expect(center and center[2] < 2.5, "Detached beam's visual center must fall below its supports; y=" .. tostring(center and center[2]))
    Test.touch("reset")
    Test.wait(0.3)
    local x, reset_y = Transform3D.get_position("arch")
    Test.expect(math.abs(x) < 0.1 and math.abs(reset_y) < 0.1, "Reset must restore the authored assembly")
    Test.expect(Destruction3D.state("arch").bodies == 1, "Reset must clear fragment ownership")
  end }
} }
