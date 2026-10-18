local CharacterController3D = require("demi.physics.character_controller3d")
local Test = require("demi.test")
local Transform3D = require("demi.transform3d")

local function wait_for_ground()
  for attempt = 1, 80 do
    local controller = CharacterController3D.state("mount")
    if controller and controller.grounded then
      return
    end
    Test.wait(0.05)
  end
  Test.expect(false, "Mount capsule did not settle onto the solid trail")
end

return {
  tests = {
    {
      name = "mounted riding follows real input and solid terrain",
      func = function()
        wait_for_ground()
        Test.touch("touch_toggle")
        Test.wait(0.1)
        local start_x, start_y, start_z = Transform3D.get_world_position("mount")
        Test.expect(start_x ~= nil, "Mounted player must exist")
        Test.expect(Test.node_center("riding_stick") ~= nil, "Riding stick must exist")
        Test.expect(Test.node_center("riding_forward") ~= nil, "Forward drag target must exist")

        -- A real HUD drag feeds the same riding_move action as keyboard input.
        Test.swipe("riding_stick", "riding_forward", 1.4)
        Test.wait(0.8)
        local end_x, end_y, end_z = Transform3D.get_world_position("mount")
        local distance_x = end_x - start_x
        local distance_z = end_z - start_z
        Test.expect(distance_x * distance_x + distance_z * distance_z > 0.25,
          "Riding input must move the capsule across the trail")
        Test.expect(math.abs(end_y - start_y) < 5.0,
          "Mount must remain on the trail instead of falling through it")
        wait_for_ground()

        local stopped_x, _, stopped_z = Transform3D.get_world_position("mount")
        Test.wait(0.4)
        local idle_x, _, idle_z = Transform3D.get_world_position("mount")
        local drift_x = idle_x - stopped_x
        local drift_z = idle_z - stopped_z
        Test.expect(drift_x * drift_x + drift_z * drift_z < 0.04,
          "Releasing the stick must brake the mount")

        local camera_x, camera_y, camera_z = Transform3D.get_world_position("camera")
        Test.expect(camera_x ~= nil and camera_y > end_y and camera_z ~= nil,
          "Chase camera must track above the mounted player")
      end,
    },
  },
}
