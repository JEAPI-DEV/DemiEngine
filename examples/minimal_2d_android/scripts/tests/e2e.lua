-- Mobile end-to-end tests for the physical-device qualification gate.
--
-- Runs when the runtime launches in test mode: `demi test android` sets the
-- `.demi_run_tests` marker, and desktop runs use `demi run --mobile-tests`.
-- Touches resolve HUD nodes by id and flow through the real input pipeline,
-- so these tests exercise the same path as a finger on the screen.

local tests = {}

tests[#tests + 1] = {
  name = "settings save writes through the options screen",
  func = function()
    Mobile.wait(1.5)
    Mobile.touch("menu_button_options")
    Mobile.touch("menu_volume_plus")
    Mobile.touch("menu_back")
  end,
}

tests[#tests + 1] = {
  name = "level select loads the platformer scene",
  func = function()
    Mobile.touch("menu_button_levels")
    Mobile.touch("menu_button_level_1")
    Mobile.expect_scene("scene://minimal_2d_android/platformer", 10.0)
  end,
}

return {tests = tests}
