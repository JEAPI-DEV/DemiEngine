local Terrain = require("colony.terrain")
local Network = require("colony.network")
local Jobs = require("colony.jobs")
local Resources = require("colony_resources")
local Construction = require("colony_construction")
---@demi_component
---@description Coordinates colony resources, utility connections and engineer construction jobs.
local Colony = {}
---@demi_property entity
Colony.habitat = "cylinder"
---@demi_property entity
Colony.solar_array = "cube"
---@demi_property entity
Colony.water_extractor = "cylinder_2"
---@demi_property entity
Colony.engineer = "engineer"
---@demi_property entity
Colony.camera = "camera"
---@demi_property entity
Colony.terrain = "terrain"
---@demi_property
Colony.starting_metal = 24
---@demi_property
Colony.power_supply = 12
---@demi_property
---@label Habitat Power Demand (kW)
Colony.power_demand = 6
---@demi_property
Colony.water_production = 2
---@demi_property
Colony.water_capacity = 100
---@demi_property
---@range 1 10
Colony.walk_speed = 3
---@demi_property
---@range 1 60
Colony.build_seconds = 5
---@demi_property
---@label Navigation Cells Per Axis (2 m)
---@range 1 256
Colony.navigation_cells = 64
function Colony:on_start()
  self.terrain_service=Terrain.new(self.terrain,self.engineer)
  self.network=Network.new(self,self.terrain_service)
  self.jobs=Jobs.new(self,self.network,self.terrain_service)
  self.resources=Resources.new(self,self.network)
  self.controls=Construction.new(self,self.network,self.jobs,self.terrain_service)
end
function Colony:on_update() self.controls:update() end
function Colony:on_fixed_update(dt)
  if not self.initial_links then
    local routes={}
    for _,id in ipairs({self.solar_array,self.water_extractor}) do
      local route=self.network:route(self.habitat,id)
      if not route then return end
      routes[#routes+1]={id=id,points=route}
    end
    for i,route in ipairs(routes) do
      local id="starter_link_"..i
      assert(self.network:draw_link(id,route.points,false))
      self.network.edges[#self.network.edges+1]={id=id,a=self.habitat,b=route.id,complete=true}
    end
    self.initial_links=true
  end
  self.jobs:update(dt)
  self.network:refresh()
  self.resources:update(dt)
end
function Colony:on_destroy()
  if self.jobs then self.jobs.agent:stop() end
  if self.controls then self.controls:destroy() end
end
return Colony
