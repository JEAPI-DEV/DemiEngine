return { tests = {
  { name = "compound opening and part identity", func = function()
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
    local _, y = Transform3D.get_position("arch")
    Test.expect(y > 0.1, "Native impulse must move the compound")
    Test.touch("reset")
    Test.wait(0.3)
    local x, reset_y = Transform3D.get_position("arch")
    Test.expect(math.abs(x) < 0.1 and math.abs(reset_y) < 0.1, "Reset must restore the authored assembly")
  end }
} }
