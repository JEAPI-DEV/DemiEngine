local CharacterController3D = require("demi.physics.character_controller3d")
local Test = require("demi.test")
local Transform3D = require("demi.transform3d")

local function wait_for_ground(stage)
  for attempt = 1, 80 do
    local controller = CharacterController3D.state("mount")
    if controller and controller.grounded then
      return
    end
    Test.wait(0.05)
  end
  Test.expect(false, stage .. ": mount capsule did not settle onto the solid trail")
end

return {
  tests = {
    {
      name = "mounted riding follows real input and solid terrain",
      func = function()
        Test.expect(Test.node_center("loading_progress") ~= nil, "Boot scene must show progress")
        Test.expect_scene("scene://amber_pass/main", 90)
        wait_for_ground("After loading")
        Test.touch("touch_toggle")
        Test.wait(0.1)
        local start_x, start_y, start_z = Transform3D.get_world_position("mount")
        Test.expect(start_x ~= nil, "Mounted player must exist")
        Test.expect(Test.node_center("riding_stick") ~= nil, "Riding stick must exist")
        Test.expect(Test.node_center("riding_forward") ~= nil, "Forward drag target must exist")

        -- With the chase camera behind a +Z-facing horse, screen-right is -X.
        -- Check the actual virtual-stick path, not only controller arithmetic.
        local stick = Test.node_center("riding_stick")
        local _, initial_yaw = Transform3D.get_rotation("mount")
        Test.swipe_xy(stick[1], stick[2], stick[1] + 45, stick[2], 0.3)
        Test.wait(0.1)
        local _, right_yaw = Transform3D.get_rotation("mount")
        Test.expect(right_yaw < initial_yaw - 0.1, "Right input must turn toward camera-right")
        Test.swipe_xy(stick[1], stick[2], stick[1] - 45, stick[2], 0.3)
        Test.wait(0.1)
        local _, left_yaw = Transform3D.get_rotation("mount")
        Test.expect(left_yaw > right_yaw + 0.1, "Left input must turn toward camera-left")

        -- A real HUD drag feeds the same riding_move action as keyboard input.
        Test.swipe("riding_stick", "riding_forward", 1.4)
        Test.wait(0.25)
        local end_x, end_y, end_z = Transform3D.get_world_position("mount")
        local distance_x = end_x - start_x
        local distance_z = end_z - start_z
        Test.expect(distance_x * distance_x + distance_z * distance_z > 0.25,
          "Riding input must move the capsule across the trail")
        Test.expect(math.abs(end_y - start_y) < 5.0,
          "Mount must remain on the trail instead of falling through it")
        wait_for_ground("After riding")

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
