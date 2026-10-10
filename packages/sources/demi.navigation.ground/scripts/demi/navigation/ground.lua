local Navigation = require("demi.navigation")
local Entity = require("demi.entity")
local Physics = require("demi.physics.query3d")
local Transform = require("demi.transform3d")
local Controller = require("demi.physics.character_controller3d")
local Ground = {}
local function finite(value) return type(value)=="number" and value==value and math.abs(value)<math.huge end
local function positive(value,name)
  assert(finite(value) and value>0,name.." must be positive and finite")
  return value
end
local Surface = {}; Surface.__index=Surface
local Agent = {}; Agent.__index=Agent

---A terrain-backed X/Z grid with independent ownership. Sampling is incremental.
function Ground.create(options)
  local self=setmetatable({},Surface)
  self.terrain=assert(options.terrain,"terrain entity required")
  self.columns,self.rows=options.columns or 64,options.rows or 64
  self.cell_size=positive(options.cell_size or 2,"cell_size")
  assert(finite(self.columns) and self.columns%1==0 and self.columns>0,"columns must be a positive integer")
  assert(finite(self.rows) and self.rows%1==0 and self.rows>0,"rows must be a positive integer")
  self.radius=options.agent_radius or 0.4
  assert(finite(self.radius) and self.radius>=0,"agent_radius must be nonnegative and finite")
  local slope=options.max_slope or 30
  assert(finite(slope) and slope>=0 and slope<90,"max_slope must be in [0,90)")
  self.min_normal=math.cos(math.rad(slope))
  self.sample_height=options.sample_height or 500
  self.sample_depth=positive(options.sample_depth or 1000,"sample_depth")
  assert(finite(self.sample_height),"sample_height must be finite")
  self.ignore=options.ignored_entity or ""
  local origin=options.origin or {0,0}
  self.grid=assert(Navigation.create_grid(self.columns,self.rows,self.cell_size,origin[1],origin[2]),"invalid ground grid")
  self.next_cell,self.heights,self.obstacles,self.revision=0,{},{},0
  return self
end
function Surface:sample(x,z)
  local hit=Physics.raycast(x,self.sample_height,z,0,-1,0,self.sample_depth,self.ignore)
  if not hit then return end
  local id=hit.entity_id
  while id and id~="" and id~=self.terrain do id=Entity.parent(id) end
  if id==self.terrain then return hit.point[2],hit.normal[2] end
end
function Surface:blocked(x,z,y)
  if not y then return true end
  for _,box in ipairs(self.obstacles) do
    if x>=box.min_x-self.radius and x<=box.max_x+self.radius and z>=box.min_z-self.radius and z<=box.max_z+self.radius then return true end
  end
  return false
end
function Surface:update(budget)
  budget=budget or 64
  assert(finite(budget) and budget>=0,"survey budget must be nonnegative and finite")
  for _=1,math.floor(budget) do
    if self:ready() then return true end
    local index=self.next_cell;self.next_cell=index+1
    local cx,cz=index%self.columns,math.floor(index/self.columns)
    local x,z=self.grid:cell_to_world(cx,cz)
    local y,normal=self:sample(x,z)
    local valid=y and normal>=self.min_normal
    if valid then
      for _,offset in ipairs({{-self.radius,0},{self.radius,0},{0,-self.radius},{0,self.radius}}) do
        local h,n=self:sample(x+offset[1],z+offset[2])
        if not h or n<self.min_normal then valid=false;break end
      end
    end
    if valid then self.heights[index]=y end
    self.grid:set_blocked(cx,cz,self:blocked(x,z,self.heights[index]))
  end
  return self:ready()
end
function Surface:ready() return self.next_cell>=self.columns*self.rows end
function Surface:set_obstacles(boxes)
  local copy={}
  for _,box in ipairs(boxes) do
    assert(finite(box.min_x) and finite(box.max_x) and finite(box.min_z) and finite(box.max_z) and
      box.min_x<=box.max_x and box.min_z<=box.max_z,"invalid obstacle bounds")
    copy[#copy+1]={min_x=box.min_x,max_x=box.max_x,min_z=box.min_z,max_z=box.max_z}
  end
  self.obstacles=copy;self.revision=self.revision+1
  for index=0,self.next_cell-1 do
    local cx,cz=index%self.columns,math.floor(index/self.columns)
    local x,z=self.grid:cell_to_world(cx,cz)
    self.grid:set_blocked(cx,cz,self:blocked(x,z,self.heights[index]))
  end
end
---Resurvey after terrain/static collision changes; agents stop until ready again.
function Surface:invalidate()
  self.next_cell,self.heights=0,{};self.revision=self.revision+1
end
function Surface:path(from,goal,radius)
  if not self:ready() then return {},"SURFACE_NOT_READY" end
  local sx,sz=self.grid:world_to_cell(from[1],from[3])
  local gx,gz=self.grid:world_to_cell(goal[1],goal[3])
  if not sx or not gx then return {},"PATH_OUT_OF_BOUNDS" end
  local candidates={};local cells=math.ceil((radius or 0)/self.cell_size)
  for z=gz-cells,gz+cells do for x=gx-cells,gx+cells do
    local wx,wz=self.grid:cell_to_world(x,z)
    if wx and not self.grid:blocked(x,z) then
      local distance=(wx-goal[1])^2+(wz-goal[3])^2
      if cells==0 or distance<=(radius or 0)^2 then candidates[#candidates+1]={x=x,z=z,d=distance} end
    end
  end end
  table.sort(candidates,function(a,b) if a.d==b.d then return a.z==b.z and a.x<b.x or a.z<b.z end return a.d<b.d end)
  for _,target in ipairs(candidates) do
    local path,diagnostic=self.grid:path(sx,sz,target.x,target.z,false)
    if #path>0 then
      -- Start at the actual actor, not a possibly blocked cell centre. A route
      -- may leave a blocked start cell, but must never ask the actor to enter it.
      local points={{from[1],from[2],from[3]}}
      for i=2,#path do
        local cell=path[i]
        points[#points+1]={cell.world_x,self.heights[cell[2]*self.columns+cell[1]],cell.world_y}
      end
      if #path==1 then
        local cell=path[1]
        points[#points+1]={cell.world_x,self.heights[cell[2]*self.columns+cell[1]],cell.world_y}
      end
      return points,diagnostic
    end
  end
  return {},"PATH_UNREACHABLE"
end
function Surface:agent(entity,options)
  options=options or {}
  return setmetatable({surface=self,id=entity,speed=positive(options.speed or 3,"speed"),arrival=positive(options.arrival_distance or 0.15,"arrival_distance"),
    face_movement=options.face_movement==true,
    stuck_timeout=positive(options.stuck_timeout or 3,"stuck_timeout"),state="idle",elapsed=0},Agent)
end
function Agent:stop()
  Controller.set_velocity(self.id,0,0,0)
  self.goal,self.path,self.state=nil,nil,"idle"
end
function Agent:move_to(goal,radius)
  assert(type(goal)=="table" and finite(goal[1]) and finite(goal[2]) and finite(goal[3]),"goal must be a finite Vec3")
  radius=radius or 0
  assert(finite(radius) and radius>=0,"goal radius must be nonnegative and finite")
  self.goal,self.radius={goal[1],goal[2],goal[3]},radius
  local position=Entity.world_position(self.id)
  if not position then self.state,self.diagnostic="unreachable","AGENT_UNAVAILABLE";return false,self.diagnostic end
  self.path,self.diagnostic=self.surface:path(position,self.goal,self.radius)
  self.index,self.revision,self.elapsed=1,self.surface.revision,0
  self.last_position=Entity.world_position(self.id)
  self.state=#self.path>0 and "moving" or "unreachable"
  if self.state=="unreachable" then Controller.set_velocity(self.id,0,0,0) end
  return self.state=="moving",self.diagnostic
end
function Agent:update(dt)
  assert(finite(dt) and dt>=0,"dt must be nonnegative and finite")
  if not Entity.exists(self.id) or not Entity.is_enabled(self.id) then
    Controller.set_velocity(self.id,0,0,0);return "paused"
  end
  if not self.goal then return self.state end
  if not self.surface:ready() then Controller.set_velocity(self.id,0,0,0);return "surveying" end
  if self.revision~=self.surface.revision then self:move_to(self.goal,self.radius) end
  if self.state~="moving" then return self.state end
  local position=Entity.world_position(self.id)
  local target=self.path[self.index]
  local dx,dz=target[1]-position[1],target[3]-position[3]
  local distance=math.sqrt(dx*dx+dz*dz)
  if distance<=self.arrival then
    self.index=self.index+1
    if self.index>#self.path then self.state="arrived";Controller.set_velocity(self.id,0,0,0);return self.state end
    return self.state
  end
  local speed=math.min(self.speed,distance/math.max(dt,0.001))
  if self.face_movement then Transform.look_at(self.id,position[1]+dx,position[2],position[3]+dz) end
  Controller.set_velocity(self.id,dx/distance*speed,0,dz/distance*speed)
  self.elapsed=self.elapsed+dt
  if self.elapsed>=self.stuck_timeout then
    local old=self.last_position
    if (position[1]-old[1])^2+(position[3]-old[3])^2<0.04 then
      self.state,self.diagnostic="blocked","AGENT_BLOCKED";Controller.set_velocity(self.id,0,0,0)
    end
    self.last_position,self.elapsed=position,0
  end
  return self.state
end
return Ground
