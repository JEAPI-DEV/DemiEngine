local Test = require("demi.test")
local Entity = require("demi.entity")
local Transform = require("demi.transform3d")

return {tests = {{name = "live MSAA quality changes", func = function()
  Test.wait(0.2)
  local before = Transform.get_position("moving")
  for _, samples in ipairs({0, 2, 8, 16, 4}) do
    Test.expect(Entity.set_field("environment", "Environment3D", "msaa_samples", samples),
      "Could not change MSAA sample count")
    Test.wait(0.15)
    Test.expect(Entity.get_config("environment", "Environment3D", "msaa_samples") == samples,
      "MSAA setting did not persist")
  end
  local after = Transform.get_position("moving")
  Test.expect(math.abs(after - before) > 0.01,
    "Animation stopped while changing render quality")
end}}}
