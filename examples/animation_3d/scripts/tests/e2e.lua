local Application = require("demi.application")
local Entity = require("demi.entity")
local Transform3D = require("demi.transform3d")
local Physics3D = require("demi.physics.query3d")
local Hud = require("demi.hud")
local Test = require("demi.test")

local suite = { tests = {
  { name = "visual pose budget preserves gravity collision and HUD input", func = function()
    Test.touch("open_crowd")
    Test.expect_scene("scene://animation_3d/crowd", 10)
    Test.wait(0.1)
    Test.expect(Entity.create("budget_floor", {components={
      Transform3D={position={0,-0.5,-20}}, BoxCollider3D={size={4,1,4}},
      Rigidbody3D={body_type="static"},
    }}) ~= nil, "Probe floor must be created")
    Test.expect(Entity.create("budget_actor", {components={
      Transform3D={position={0,3,-20}},
      MeshRenderer={model="asset://animation_lib/ual1_standard"},
      AnimationPlayer3D={clip_name="Walk_Loop",visual_update_rate=1,visual_update_distance=0},
      SphereCollider3D={radius=0.5,offset={0,0.5,0}},
      Rigidbody3D={body_type="dynamic"},
    }}) ~= nil, "Budgeted actor must be created")
    Test.wait(0.4)
    local _, falling = Transform3D.get_position("budget_actor")
    Test.expect(falling < 2.8 and falling > 0, "Physics must advance independently of the 1 Hz visual rate")
    Test.wait(1.2)
    local _, resting = Transform3D.get_position("budget_actor")
    Test.expect(resting > -0.1 and resting < 0.2, "Collider must keep the actor above the floor")
    local hit=Physics3D.raycast(0,4,-20,0,-1,0,5)
    Test.expect(hit and hit.entity_id=="budget_actor", "Budgeted actor must retain its real sphere collider")
    Test.touch("crowd_back")
    Test.expect_scene("scene://animation_3d/main", 2)
  end },
  { name = "crowd scene populates and releases characters across repeated visits", func = function()
    for visit = 1, 2 do
      Test.touch("open_crowd")
      Test.expect_scene("scene://animation_3d/crowd", 10)
      Test.wait(0.2)
      Test.expect(Transform3D.get_position("character_1") ~= nil, "First character must exist")
      Test.expect(Transform3D.get_position("character_64") ~= nil, "Last character must exist")
      Test.expect(Transform3D.get_position("character_65") == nil, "No extra characters")
      Test.expect(Hud.get_text("crowd_status"):find("INDEPENDENT WALK PLAYBACK", 1, true) ~= nil,
        "The live workload must initialize")
      Test.touch("crowd_back")
      Test.expect_scene("scene://animation_3d/main", 10)
      Test.wait(0.2)
      Test.expect(Transform3D.get_position("character_1") == nil, "Crowd must be released on scene exit")
      Test.expect(Transform3D.get_position("ent_animated_character") ~= nil, "Single-character scene must return")
    end
  end },
} }
if Application.platform() == "android" then
  table.insert(suite.tests, { name = "Android orientation preserves crowd and touch input", func = function()
    Test.touch("open_crowd")
    Test.expect_scene("scene://animation_3d/crowd", 10)
    Test.expect(Application.request_orientation("portrait"), "Portrait request must be accepted")
    Test.wait(1.2)
    Test.expect(Application.orientation() == "portrait", "Drawable must follow the portrait request")
    Test.expect(Transform3D.get_position("character_64") ~= nil, "Rotation must retain the crowd")
    Test.expect(Application.request_orientation("landscape"), "Landscape request must be accepted")
    Test.wait(1.2)
    Test.expect(Application.orientation() == "landscape", "Drawable must return to landscape")
    Test.touch("crowd_back")
    Test.expect_scene("scene://animation_3d/main", 10)
  end })
end
return suite
