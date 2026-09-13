return { tests = {
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
