local Controllers = require("demi.gameplay.controllers")

Test.case("platform coyote and buffered jump are package policy", function()
  local intent = Controllers.platform({ x = 1, jump = true, dt = 0.01 },
    { grounded = false, coyote = 0.05, jump_buffer = 0 }, { coyote_time = 0.1, jump_buffer = 0.1 })
  Test.equal(intent.jump, true)
end)

Test.case("top down diagonal is normalized", function()
  local intent = Controllers.top_down({ x = 1, y = 1 })
  Test.near(math.sqrt(intent.x * intent.x + intent.y * intent.y), 1)
end)

Test.case("character controller module is independently loadable", function()
  local Controller = require("demi.gameplay.character_controller_2d")
  local controller = Controller.new({ move_speed = 7 })
  Test.equal(controller.move_speed, 7); Test.equal(controller.ground_layer, "platform")
end)
