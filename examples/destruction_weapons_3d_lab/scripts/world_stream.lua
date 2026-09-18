local Streaming=require("demi.gameplay.destruction.streaming")
local Native=require("demi.gameplay.destruction.native")
local Entity=require("demi.entity")
local Hud=require("demi.hud")
local Save=require("demi.save")

---@demi_component
local WorldStream={}
---@demi_property number
WorldStream.load_distance=24
---@demi_property number
WorldStream.unload_distance=32
---@demi_property number
WorldStream.debris_lifetime=45
---@demi_property string
WorldStream.save_slot=""

function WorldStream:on_start()
  local values=Entity.get(self.entity_id,"GameplayData","values")
  self.stream=Streaming.new(Native.streaming(),values.walls,{
    load_distance=self.load_distance,unload_distance=self.unload_distance,
    debris_lifetime=self.debris_lifetime,max_active=8,spawn_budget=1,
  })
  if self.save_slot~="" and Save.exists(self.save_slot) then
    local saved=Save.read(self.save_slot)
    local ok,error=self.stream:import_state(saved)
    if not ok then self.load_error=error end
  end
end
function WorldStream:on_update(dt)
  if self.load_error then Hud.set_text("status",self.load_error);return end
  local observer=Entity.world_position("player")
  if observer then self.stream:update(dt,observer) end
  if self.last_count~=self.stream.active_count then
    self.last_count=self.stream.active_count
    Hud.set_text("stream_stats","Nearby walls: "..self.last_count.." / 8 | Load "..self.load_distance.." m | Unload "..self.unload_distance.." m")
  end
  if self.stream.last_error~="" then Hud.set_text("status",self.stream.last_error) end
end
-- @HandleAction("save_world")
function WorldStream:save_world()
  if self.load_error then Hud.set_text("status","Cannot save after failed load: "..self.load_error);return end
  if self.save_slot=="" then Hud.set_text("status","Set World Stream Save Slot to enable persistence");return end
  local state,error=self.stream:export_state()
  local ok=state and Save.write(self.save_slot,state,1)
  Hud.set_text("status",ok and "World damage saved" or (error or Save.last_error()))
end
function WorldStream:on_destroy()
  if self.stream then self.stream:dispose() end
end
return WorldStream
