local Test = require("demi.test")
local Transform = require("demi.transform3d")
local Rigidbody = require("demi.physics.rigidbody3d")
local Water = require("demi.terrain.water")

return {
  tests = {
    {
      name = "opt-in buoyancy and swimming preserve native body policy",
      func = function()
        Test.expect_scene("scene://water_interaction_3d/main")
        local inlet = Water.sample({16, 3, 30}, "terrain")
        Test.expect(inlet and inlet.body_id == "inlet_river", "The inlet must publish a separate stable river body")
        Test.expect(inlet and inlet.flow_velocity[3] < 0, "The river current must flow toward the lake")
        Test.wait(5)
        local x, y, z = Transform.get_world_position("floating_body")
        Test.expect(y > 1.2 and y < 3, "Opt-in buoyant body must settle near the lake surface")
        local immersed = Water.sample({x, y - .25, z})
        Test.expect(immersed and immersed.underwater, "Floating body must overlap real prepared water")
        local swim_x, swim_y = Transform.get_world_position("swimmer")
        Test.expect(swim_x > 13 and swim_x < 21, "Swimming must move the dynamic body without teleporting out of the basin")
        Test.expect(math.abs(swim_y - 1) < .1, "Swimming controller must preserve authored zero-gravity vertical policy")
        local air_x, air_y, air_z = Rigidbody.get_velocity("air_control")
        Test.expect(math.abs(air_x) + math.abs(air_y) + math.abs(air_z) < .001,
          "A dry body with gravity disabled must receive no water force")
      end,
    },
  },
}
