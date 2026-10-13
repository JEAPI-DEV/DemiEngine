local Test = require("demi.test")
local Hud = require("demi.hud")
local Entity = require("demi.entity")
local Transform3D = require("demi.transform3d")
local Camera = require("demi.camera3d")
local Physics = require("demi.physics.query3d")
local Input = require("demi.input")
local Time = require("demi.time")

local function read_amount(id)
  local text = Hud.get_text(id) or ""
  local amount = tonumber(text:match("[%+%-]?%d+"))
  Test.expect(amount ~= nil, "Resource HUD contains a number: " .. id)
  return amount
end

local function tap_world(x, z)
  local ground = Physics.raycast(x, 500, z, 0, -1, 0, 1000)
  Test.expect(ground ~= nil, "World target has a surface")
  local w, h = Input.viewport_size()
  local screen = Camera.world_to_screen("camera", x, ground.point[2], z, w, h)
  Test.expect(screen ~= nil, "World target projects into camera depth")
  local cw, ch = Hud.canvas_size()
  Test.expect(
    screen[2] / h < 0.66 and screen[2] / h > 0.16,
    "Test site overlaps HUD: " .. x .. "," .. z .. " y=" .. screen[2] / h
  )
  Test.tap(screen[1] / w * cw, screen[2] / h * ch)
  Test.wait(0.3)
end

local function wait_for(predicate, message, seconds)
  for _ = 1, (seconds or 90) * 4 do
    if predicate() then
      return
    end
    Test.wait(0.25)
  end
  Test.expect(false, message .. " / " .. (Hud.get_text("job_status") or ""))
end
local function inventory(label)
  return tonumber((Hud.get_text("logistics_status") or ""):match(label .. ":%s*(%d+)"))
end
local function suit()
  return tonumber((Hud.get_text("needs_status") or ""):match("Suit O2:%s*(%d+)")) or 0
end
local function conserved(expected)
  Test.expect(
    inventory("Depot") + inventory("Carrying") + inventory("On sites") + inventory("Recoverable")
      == expected,
    "Physical metal is conserved"
  )
  Test.expect(
    read_amount("metal") == inventory("Depot") - inventory("Reserved"),
    "Available metal excludes reservations"
  )
end
local function pause()
  Test.touch("time_pause")
  Test.wait(0.2)
  Test.expect(Time.is_paused(), "Pause stops simulation")
end
local function fast()
  Test.touch("time_fast")
  Test.wait(0.2)
  Test.expect(not Time.is_paused() and Time.get_scale() == 3, "3x control resumes simulation")
end
local function link(action, ax, az, bx, bz)
  Test.touch(action)
  Test.wait(0.2)
  tap_world(ax, az)
  tap_world(bx, bz)
end

return {
  tests = {
    {
      name = "colony_hauling_needs_and_connections",
      func = function()
        Test.expect_scene("scene://colony")
        Test.wait(0.3)
        Test.expect(Test.node_center("label") ~= nil, "Power readout exists")
        Test.expect(read_amount("label") == 4, "Default colony has 4 kW surplus")
        local water = read_amount("label_copy")
        local oxygen = read_amount("label_copy_copy")
        local x, y, z = Transform3D.get_position("camera")
        Test.wait(2)
        local later_water = read_amount("label_copy")
        local later_oxygen = read_amount("label_copy_copy")
        Test.expect(later_water > water and later_water <= 100, "Water production reaches HUD")
        Test.expect(
          later_oxygen > oxygen and later_oxygen <= 100,
          "Powered oxygen production reaches HUD"
        )
        local nx, ny, nz = Transform3D.get_position("camera")
        Test.expect(nx == x and ny == y and nz == z, "Camera stays still without input")

        local config = Entity.get_config("ent_new", "LuaScript", "properties")
        Test.expect(Entity.exists(config.solar_array), "Solar reference survives prefab conversion")
        Test.expect(Entity.exists(config.water_extractor), "Water extractor reference resolves")
        Test.expect(Entity.set_enabled(config.solar_array, false), "Disable solar array")
        Test.wait(0.3)
        Test.expect(read_amount("label") == -8, "Disabled solar array removes supply")
        local stopped_water = read_amount("label_copy")
        local stopped_oxygen = read_amount("label_copy_copy")
        Test.wait(2)
        Test.expect(
          read_amount("label_copy") == stopped_water,
          "No water production or oxygen-generator consumption without power"
        )
        Test.expect(read_amount("label_copy_copy") < stopped_oxygen, "Oxygen falls without power")

        Test.expect(Entity.set_enabled(config.solar_array, true), "Restore solar array")
        Test.expect(Entity.set_enabled(config.water_extractor, false), "Disable extractor")
        Test.wait(0.3)
        local stored_water = read_amount("label_copy")
        Test.wait(2)
        Test.expect(read_amount("label") == 6, "Disabled extractor stops its power draw")
        Test.expect(
          read_amount("label_copy") < stored_water,
          "Oxygen production consumes stored water while extraction is disabled"
        )
        Test.expect(Entity.set_enabled(config.water_extractor, true), "Restore extractor")
        local before_recovery = read_amount("label_copy")
        Test.wait(2)
        Test.expect(read_amount("label_copy") > before_recovery, "Water production recovers")

        Test.expect(
          Entity.exists("engineer") and Entity.exists("depot/base"),
          "Engineer and physical depot exist"
        )
        Test.expect(read_amount("meals") == 12, "Starter meals are in depot inventory")
        Test.expect(Entity.set_enabled("engineer", false), "Pause engineer")
        local worker = Entity.world_position("engineer")
        Test.touch("build_solar")
        tap_world(82.822, 77.408)
        Test.expect(
          not Entity.exists("site_1/base") and read_amount("metal") == 24,
          "Occupied site reserves nothing"
        )
        Test.touch("build_cancel")
        tap_world(88, 88)
        Test.expect(not Entity.exists("site_1/base"), "Leaving placement creates no site")
        Test.touch("build_solar")
        tap_world(88, 88)
        Test.expect(
          Entity.exists("site_1/base") and not Entity.exists("built_1/base"),
          "Plan starts as an unfinished site"
        )
        Test.expect(
          inventory("Depot") == 24 and inventory("Reserved") == 6 and inventory("Carrying") == 0,
          "Reservation keeps metal physically in depot"
        )
        conserved(24)
        Test.wait(1)
        local stopped = Entity.world_position("engineer")
        Test.expect(
          stopped[1] == worker[1] and stopped[3] == worker[3],
          "Disabled engineer cannot haul or work"
        )
        Test.expect(Entity.set_enabled("engineer", true), "Resume engineer")
        Test.wait(1)
        local moving = Entity.world_position("engineer")
        local travelled = math.sqrt((moving[1] - worker[1]) ^ 2 + (moving[3] - worker[3]) ^ 2)
        Test.expect(travelled > 0.1 and travelled < 4, "Engineer walks rather than teleporting")
        fast()
        wait_for(function()
          return inventory("Carrying") == 4
        end, "Engineer picks up a capacity-limited load")
        Test.expect(
          inventory("Depot") == 20 and inventory("Reserved") == 2,
          "Pickup removes only its reserved load"
        )
        Test.expect(Entity.is_enabled("construction/cargo"), "Carried crate is visible")
        conserved(24)
        pause()
        local paused = Entity.world_position("engineer")
        local paused_suit = suit()
        Test.wait(0.5)
        local still = Entity.world_position("engineer")
        Test.expect(
          still[1] == paused[1] and still[3] == paused[3] and suit() == paused_suit,
          "Paused time freezes hauling and needs"
        )
        fast()
        Test.touch("rest_engineer")
        Test.wait(0.2)
        wait_for(function()
          return suit() >= 94 and read_amount("meals") == 11
        end, "Habitat restores needs and consumes a meal")
        Test.expect(inventory("Carrying") == 4, "Rest preserves the carried load")
        conserved(24)
        wait_for(function()
          return inventory("On sites") == 4
        end, "First load reaches site")
        Test.expect(
          not Entity.exists("built_1/base"),
          "Construction cannot finish before all materials arrive"
        )
        pause()
        Test.touch("build_cancel")
        Test.wait(0.2)
        Test.expect(
          read_amount("metal") == 20 and inventory("Recoverable") == 4,
          "Cancellation does not teleport delivered metal back"
        )
        Test.expect(
          Entity.exists("recovery_1/base") and not Entity.exists("site_1/base"),
          "Cancelled site leaves a physical recovery pile"
        )
        conserved(24)
        fast()
        wait_for(function()
          return inventory("Recoverable") == 0
            and inventory("Carrying") == 0
            and inventory("Depot") == 24
        end, "Recovered metal is hauled back to depot")
        Test.expect(not Entity.exists("recovery_1/base"), "Empty recovery pile is removed")
        conserved(24)

        Test.touch("build_solar")
        tap_world(88, 88)
        wait_for(function()
          return Entity.exists("built_2/base")
        end, "Delivered materials allow solar construction")
        Test.wait(0.2)
        conserved(18)
        Test.expect(read_amount("label") == 4, "Disconnected solar stays offline")
        link("build_connect", 82.822, 77.408, 88, 88)
        wait_for(function()
          return read_amount("label") == 16
        end, "Hauled and completed conduit activates solar")
        conserved(17)
        link("build_connect", 82.822, 77.408, 88, 88)
        Test.expect(read_amount("metal") == 17, "Duplicate link costs nothing")
        Test.touch("build_water")
        tap_world(80, 90)
        wait_for(function()
          return Entity.exists("built_3/base")
        end, "Extractor receives both loads and completes")
        Test.wait(0.2)
        conserved(9)
        Test.expect(read_amount("label") == 16, "Disconnected extractor draws no power")
        link("build_connect", 88, 88, 80, 90)
        wait_for(function()
          return read_amount("label") == 14
        end, "Connected extractor joins transitive grid")
        conserved(8)
        Test.expect(Entity.set_enabled("built_2/base", false), "Disable bridge")
        Test.wait(0.3)
        Test.expect(read_amount("label") == 4, "Bridge loss disconnects downstream utility")
        Test.expect(Entity.set_enabled("built_2/base", true), "Restore bridge")
        Test.wait(0.3)
        Test.expect(read_amount("label") == 14, "Bridge recovery restores utility")

        Test.touch("build_water")
        tap_world(70, 88)
        wait_for(function()
          return inventory("Carrying") == 4
        end, "Carrier collects another reserved load")
        pause()
        Test.touch("build_cancel")
        Test.wait(0.2)
        Test.expect(
          inventory("Carrying") == 4 and read_amount("metal") == 4,
          "Cancelled cargo remains physically carried"
        )
        conserved(8)
        fast()
        wait_for(function()
          return inventory("Carrying") == 0 and read_amount("metal") == 8
        end, "Cancelled cargo returns to storage")
        link("build_disconnect", 82.822, 77.408, 88, 88)
        Test.expect(read_amount("label") == 4, "Disconnect removes branch supply")
        link("build_connect", 82.822, 77.408, 88, 88)
        wait_for(function()
          return read_amount("label") == 14
        end, "Rebuilt link restores both utilities")
        Test.expect(read_amount("metal") == 7, "Completed links spend their delivered material")
        Test.touch("build_water")
        tap_world(70, 88)
        Test.expect(
          not Entity.exists("site_5/base") and read_amount("metal") == 7,
          "Unaffordable plan creates no reservation"
        )
        Test.touch("build_cancel")
        Test.touch("time_normal")
        Test.wait(0.2)
        Test.expect(
          Time.get_scale() == 1 and not Time.is_paused(),
          "Normal-speed control restores 1x"
        )
        conserved(7)
      end,
    },
  },
}
