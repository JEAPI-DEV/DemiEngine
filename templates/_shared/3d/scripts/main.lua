local Script = require("demi.script")
local Main = {}

function Main:on_create()
  Script.bind(self)
  self.speed = 3.0
end

function Main:on_fixed_update(dt)
  local x = Input.value("move_right")
  local z = Input.value("move_forward")
  self:move3d(x * self.speed * dt, 0.0, -z * self.speed * dt)
end

function Main:on_destroy()
  Script.release(self)
end

-- Re-export Script helper methods so self:move3d works.
for key, value in pairs(Script) do
  if Main[key] == nil then
    Main[key] = value
  end
end

return Main
