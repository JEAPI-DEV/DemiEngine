local Transform3D = require("demi.transform3d")

-- A frame-indexed path gives Full and LOD runs identical camera samples without
-- overriding simulation time or hiding the presentation cost of a slow frame.
local Camera = {}

function Camera:on_create()
    self.frame = 0
end

function Camera:on_update(dt)
    self.frame = self.frame + 1
    local path_frame = math.max(0, self.frame - 60)
    local angle = path_frame * (2 * math.pi / 480)
    local x = 1012 + 140 * math.sin(angle)
    local y = 85 + 160 * (1 - math.cos(angle))
    local z = 1014 + 80 * math.sin(2 * angle)
    Transform3D.set_position(self.entity_id, x, y, z)
end

return Camera
