local Test = require("demi.test")
local Hud = require("demi.hud")
local Orbit = require("demi.ui.orbit")
local Scene = require("demi.scene")
local function expect_visible_bounds(id)
  for _, node in ipairs(Hud.accessibility_snapshot()) do
    if node.id == id then
      Test.expect(node.width > 0 and node.height > 0, id .. " has usable bounds")
      return
    end
  end
  Test.expect(false, id .. " is visible to accessibility")
end
return {tests = {{name="orbit_controls_and_runtime_factory", func=function()
  Test.expect_scene("scene://orbit/main")
  Test.wait(0.2)
  expect_visible_bounds("demo.settings.tabs.tab2")
  Test.touch("demo.settings.tabs.tab2")
  Test.wait(0.1)
  Test.touch("demo.settings.quality.trigger")
  Test.wait(0.1)
  expect_visible_bounds("demo.settings.quality.option3")
  Test.touch("demo.settings.quality.option3")
  Test.wait(0.1)
  Test.expect(Hud.get_text("demo.settings.quality.trigger") == "Ultra", "Dropdown selects an option")
  Test.touch("demo.settings.tabs.tab3")
  Test.wait(0.1)
  Test.touch("demo.settings.controls.option2")
  Test.wait(0.1)
  local checked = {}
  for _, node in ipairs(Hud.accessibility_snapshot()) do checked[node.id] = node.checked end
  Test.expect(checked["demo.settings.controls.option2"] == true and checked["demo.settings.controls.option1"] == false,
    "Choice group is mutually exclusive")
  Test.touch("demo.settings.tabs.tab1")
  Test.wait(0.1)
  Test.touch("demo.settings.apply")
  Test.wait(0.2)
  Test.expect(Hud.get_text("demo.feedback") == "Settings saved. Ready for the next mission.", "Prefab button delivers an event")
  Test.touch("demo.settings.reset")
  Test.wait(0.2)
  Test.expect(Hud.get_text("demo.feedback") == "Defaults restored. A fresh start.", "Reset button works")
  local button = Orbit.button("runtime_button", "Runtime button", {palette=Orbit.palette("glacier")})
  button.overrides = {at = {380,730}}
  local handle, error = Hud.create("ui_root", button)
  Test.expect(handle ~= nil, error or "Runtime factory creates a native button")
  Test.touch("runtime_button")
  Test.expect(Hud.remove(handle), "Runtime node can be removed")
  local settings = Orbit.node("settings", "runtime_settings")
  settings.overrides = {at={1500,0}}
  local tree, tree_error = Hud.create("ui_root", settings)
  Test.expect(tree ~= nil, tree_error or "Runtime prefab creates a complete tree")
  Test.expect(Hud.find("runtime_settings.tabs.tab1") ~= nil, "Nested runtime prefab IDs match authored IDs")
  Test.expect(Hud.get_text("runtime_settings.apply") == "Apply changes", "Runtime prefab content is preserved")
  Test.expect(Hud.remove(tree), "Runtime tree can be removed together")
  local invalid = Hud.create("ui_root", {id="rejected",type="container",children={{id="demo.feedback",type="label"}}})
  Test.expect(invalid == nil and Hud.find("rejected") == nil, "Failed child creation rolls back the whole tree")
  Test.touch("demo.browse_components")
  Test.expect_scene("scene://orbit/components")
  Test.wait(0.2)
  Test.touch("demo.forms.quantity.plus")
  Test.wait(0.1)
  Test.expect(Hud.get_text("demo.forms.quantity.value") == "2", "Stepper increments")
  Test.touch("demo.navigation.pages.next")
  Test.wait(0.1)
  Test.expect(Hud.get_text("demo.navigation.pages.page2") == "[2]", "Pagination advances")
  Test.touch("demo.open_dialog")
  Test.wait(0.1)
  Test.touch("demo.confirm.confirm")
  Test.wait(0.1)
  Test.expect(Hud.get_text("demo.feedback") == "Expedition confirmed.", "Modal confirmation delivers its result")
  Test.touch("demo.notice.close")
  Test.wait(0.1)
  local notice_visible = false
  for _,node in ipairs(Hud.accessibility_snapshot()) do if node.id == "demo.notice.message" then notice_visible = true end end
  Test.expect(not notice_visible, "Notification can be dismissed")

  for _, scene in ipairs({"glacier", "main_menu", "inventory", "icons"}) do
    Scene.load("scene://orbit/" .. scene)
    Test.wait(0.2)
    Test.expect_scene("scene://orbit/" .. scene)
    Test.expect(Hud.find("demo") ~= nil, "Composition loads: " .. scene)
  end
  Test.touch("demo.save")
  Test.wait(0.1)
  Test.expect(Hud.get_text("demo.save.label") == "Saved", "Icon button delivers activation")
end}}}
