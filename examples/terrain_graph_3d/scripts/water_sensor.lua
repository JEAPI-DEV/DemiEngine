local Water = require("demi.terrain.water")
local Transform3D = require("demi.transform3d")
local Events = require("demi.events")

---@demi_component
local WaterSensor = {}

---@demi_property number
WaterSensor.offset_y = 0

---@demi_property boolean
WaterSensor.emit_stay = true

---@demi_property entity
WaterSensor.target = ""

function WaterSensor:on_create()
    self.tracker = Water.tracker()
end

function WaterSensor:on_fixed_update(dt)
    local x, y, z = Transform3D.get_world_position(self.entity_id)
    if x == nil then
        self.tracker:reset()
        return
    end

    for _, event in ipairs(self.tracker:update({x, y + self.offset_y, z})) do
        if event.phase ~= "stay" or self.emit_stay then
            Events.emit("water_" .. event.phase, {
                entity_id = self.entity_id,
                water = event.sample,
            })
        end
    end
end

return WaterSensor
