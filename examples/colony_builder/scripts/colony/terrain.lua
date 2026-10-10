local Entity = require("demi.entity")
local Physics = require("demi.physics.query3d")
local Terrain = {}
Terrain.__index = Terrain
function Terrain.new(id,ignore) return setmetatable({id=id,ignore=ignore or ""},Terrain) end
function Terrain:owns(id)
  while id and id ~= "" do
    if id == self.id then return true end
    id = Entity.parent(id)
  end
  return false
end
function Terrain:sample(x,z)
  local hit = Physics.raycast(x,500,z,0,-1,0,1000,self.ignore)
  if hit and self:owns(hit.entity_id) then return hit.point[2],hit.normal[2] end
end
return Terrain
