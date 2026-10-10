local Entity = require("demi.entity")
local Needs = require("colony.needs")
local Worker = {}
Worker.__index = Worker
function Worker.new(config,surface,resources,logistics,network)
  return setmetatable({config=config, surface=surface, resources=resources, logistics=logistics, network=network,
    agent=surface:agent(config.engineer,{speed=config.walk_speed,face_movement=true}), needs=Needs.new(config)},Worker)
end
function Worker:interrupt()
  self.agent:stop()
  self.task = nil
end
function Worker:travel(kind,x,z,radius,description,subject)
  local ok = self.agent:move_to({x,0,z},radius)
  self.task = {kind=kind,x=x,z=z,radius=radius,description=description,subject=subject,blocked=not ok,retry=0}
end
function Worker:choose(job)
  self.waiting=nil
  local ledger = self.logistics
  local cargo = ledger.cargo:count("metal")
  if self.resting then
    local home=self.network.nodes[self.network.root]
    self:travel("rest",home.x,home.z,8,"returning to habitat")
  elseif cargo>0 then
    if ledger.cargo_job and ledger.cargo_job.state~="cancelled" then
      local target=ledger.cargo_job
      self:travel("deliver",target.x,target.z,math.sqrt(target.width^2+target.depth^2)/2+3,"hauling metal to site",target)
    else
      local p=Entity.world_position(self.config.depot)
      if p then self:travel("return",p[1],p[3],4.5,"returning recovered metal") else self.waiting="Depot unavailable" end
    end
  elseif #ledger.piles>0 then
    local pile=ledger.piles[1]
    self:travel("recover",pile.x,pile.z,2,"collecting recoverable metal",pile)
  elseif job then
    if job.materials:count("metal")<job.cost then
      local p=Entity.world_position(self.config.depot)
      if p then self:travel("pickup",p[1],p[3],4.5,"collecting metal from depot",job) else self.waiting="Depot unavailable" end
    else
      self:travel("work",job.x,job.z,math.sqrt(job.width^2+job.depth^2)/2+3,"walking to work site",job)
    end
  end
end
function Worker:update(dt,job)
  if not Entity.exists(self.config.engineer) or not Entity.is_enabled(self.config.engineer) then
    self.agent:update(dt)
    return false,"Engineer unavailable · jobs paused"
  end
  local recovering=self.resting and self.at_home and self.resources:habitable()
  self.needs:update(dt,self.task and self.task.kind=="work",recovering)
  if self.needs:urgent() and not self.resting then
    self.resting=true;self.at_home=false;self:interrupt()
  end
  if not self.task then self:choose(job) end
  local task=self.task
  if not task then return false,self.waiting or "Engineer · idle | select a planned site to cancel" end
  local state=self.agent:update(dt)
  if task.blocked or state=="blocked" or state=="unreachable" then
    task.retry=task.retry+dt
    if task.retry>=2 then
      task.retry=0;task.blocked=not self.agent:move_to({task.x,0,task.z},task.radius)
    end
    return false,"Engineer · route blocked, waiting for a clear path"
  end
  if state~="arrived" then return false,"Engineer · "..task.description end
  if task.kind=="rest" then
    self.at_home=true
    local ready,reason=self.needs:recover(dt,self.resources,self.logistics)
    if ready then self.resting=false;self.at_home=false;self:interrupt() end
    return false,"Engineer · "..reason
  elseif task.kind=="work" then
    local target=task.subject
    target.state="building"
    target.progress=target.progress+dt/(target.kind=="link" and 2 or self.config.build_seconds)
    return target.progress>=1,string.format("Engineer · %s %d%%",target.kind=="link" and "connecting" or "building",math.min(100,math.floor(target.progress*100)))
  end
  task.elapsed=(task.elapsed or 0)+dt
  if task.elapsed<0.4 then return false,"Engineer · "..task.description end
  if task.kind=="pickup" then
    if self.logistics:pickup(task.subject)==0 then return false,"Depot unavailable · waiting for materials" end
    task.subject.state="hauling"
  elseif task.kind=="deliver" then self.logistics:deliver(task.subject)
  elseif task.kind=="return" then
    if self.logistics:return_cargo()==0 then return false,"Depot unavailable · holding recovered metal" end
  elseif task.kind=="recover" then
    self.logistics:recover(task.subject);self.surface:invalidate()
  end
  self:interrupt()
  return false,"Engineer · "..task.description
end
return Worker
