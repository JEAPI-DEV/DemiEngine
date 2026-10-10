local Entity = require("demi.entity")
local Hud = require("demi.hud")
local Prefab = require("demi.prefab")
local Ground = require("demi.navigation.ground")
local Jobs={};Jobs.__index=Jobs
function Jobs.new(config,network,terrain)
  local p=Entity.world_position(terrain.id)
  local surface=Ground.create({terrain=terrain.id,origin={p[1],p[3]},columns=config.navigation_cells,rows=config.navigation_cells,
    cell_size=2,agent_radius=0.5,ignored_entity=config.engineer})
  return setmetatable({config=config,network=network,terrain=terrain,surface=surface,
    agent=surface:agent(config.engineer,{speed=config.walk_speed}),queue={},metal=config.starting_metal,next_id=1,next_link=1,revision=-1},Jobs)
end
function Jobs:status(text)
  if self.last_status~=text then Hud.set_text("job_status",text);self.last_status=text end
end
function Jobs:publish()
  Hud.set_text("metal",string.format("Metal: %.0f",self.metal))
end
function Jobs:enqueue_build(kind,item,site)
  if self.metal<item.cost then return nil,"Not enough metal" end
  local number=self.next_id;local id="built_"..number;local marker="site_"..number
  local options={id=marker,position={site.x,site.y,site.z},overrides={base={components={
    MeshRenderer={size={item.width,0.25,item.depth}},BoxCollider3D={size={item.width,0.5,item.depth},offset={0,0.25,0}}
  }}}}
  if not Prefab.instantiate("prefab://utilities/site",options) then return nil,"Could not create construction site" end
  local node={id=id.."/base",site_id=marker.."/base",kind=kind,width=item.width,depth=item.depth,x=site.x,y=site.y,z=site.z,complete=false}
  self.network.nodes[node.id]=node;self.network.revision=self.network.revision+1
  local job={id=id,marker=marker,node=node,kind="building",item=item,site=site,cost=item.cost,x=site.x,z=site.z,
    width=item.width,depth=item.depth,progress=0,state="queued"}
  self.queue[#self.queue+1]=job;self.next_id=number+1;self.metal=self.metal-item.cost;self:publish()
  return job,"Construction queued · engineer will walk to the site"
end
function Jobs:enqueue_link(a,b)
  local points,error=self.network:route(a,b)
  if not points then return nil,error end
  if self.metal<1 then return nil,"Not enough metal" end
  local id="connection_"..self.next_link
  if not self.network:draw_link(id,points,true) then return nil,"Could not create connection" end
  local edge={id=id,a=a,b=b,complete=false,points=points}
  self.network.edges[#self.network.edges+1]=edge
  local mid=points[math.ceil(#points/2)]
  local job={id=id,edge=edge,kind="link",cost=1,x=mid[1],z=mid[3],width=1,depth=1,progress=0,state="queued"}
  self.queue[#self.queue+1]=job;self.next_link=self.next_link+1;self.metal=self.metal-1;self:publish()
  return job,"Connection queued · utilities stay offline until it is finished"
end
function Jobs:cancel(job)
  if not job or job.state=="complete" or job.state=="cancelled" then return false end
  if self.active==job then self.agent:stop();self.active=nil end
  if job.kind=="building" then
    Prefab.release(job.marker);self.network.nodes[job.node.id]=nil;self.network.revision=self.network.revision+1
  else self.network:remove(job.edge) end
  self.surface:invalidate()
  job.state="cancelled";self.metal=self.metal+job.cost;self:publish();return true
end
function Jobs:find_site(id)
  for _,job in ipairs(self.queue) do
    if job.marker and id==job.marker.."/base" and job.state~="cancelled" and job.state~="complete" then return job end
  end
end
function Jobs:update_obstacles()
  if self.revision==self.network.revision then return end
  local boxes={}
  for _,node in pairs(self.network.nodes) do boxes[#boxes+1]={min_x=node.x-node.width/2,max_x=node.x+node.width/2,min_z=node.z-node.depth/2,max_z=node.z+node.depth/2} end
  self.surface:set_obstacles(boxes);self.revision=self.network.revision
end
function Jobs:finish(job)
  if job.kind=="building" then
    local item,site=job.item,job.site
    local id=Prefab.instantiate(item.prefab,{id=job.id,position={site.x,site.y,site.z},overrides={foundation={components={Transform3D={
      position={0,-site.relief/2,0},scale={item.width,site.relief+0.35,item.depth}
    }}}}})
    if not id then job.state="failed";return false end
    Prefab.release(job.marker);job.node.complete=true
  else
    if not self.network.nodes[job.edge.a] or not self.network.nodes[job.edge.b] then job.state="failed";return false end
    job.edge.complete=true
    for _,id in ipairs(Entity.children(job.edge.id)) do Entity.set_field(id,"MeshRenderer","color",{0.2,0.65,0.7,1}) end
  end
  job.state="complete";self.agent:stop();self.active=nil;return true
end
function Jobs:update(dt)
  self:update_obstacles()
  if not self.surface:update(96) then self:status("Engineer · surveying terrain");return end
  local worker=self.config.engineer
  if not Entity.exists(worker) or not Entity.is_enabled(worker) then
    self.agent:update(dt);self:status("Engineer unavailable · jobs paused");return
  end
  if not self.active then
    for _,job in ipairs(self.queue) do
      if job.state=="queued" then
        self.active=job
        local ok=self.agent:move_to({job.x,0,job.z},math.sqrt((job.width or 1)^2+(job.depth or 1)^2)/2+3)
        job.state=ok and "walking" or "blocked";job.retry=0
        break
      end
    end
  end
  local job=self.active
  if not job then self:status("Engineer · idle | select a planned site to cancel and refund");return end
  if job.state=="failed" then self:status("Construction failed · select the site and cancel for refund");return end
  local state=self.agent:update(dt)
  if job.state=="blocked" or state=="blocked" or state=="unreachable" then
    job.state="blocked";job.retry=(job.retry or 0)+dt
    self:status("Engineer · route blocked, waiting for a clear path")
    if job.retry>=2 then
      job.retry=0
      if self.agent:move_to({job.x,0,job.z},math.sqrt(job.width^2+job.depth^2)/2+3) then job.state="walking" end
    end
    return
  end
  if state=="arrived" then
    job.state="building";job.progress=job.progress+dt/(job.kind=="link" and 2 or self.config.build_seconds)
    self:status(string.format("Engineer · %s %d%%",job.kind=="link" and "connecting" or "building",math.min(100,math.floor(job.progress*100))))
    if job.progress>=1 then self:finish(job) end
  else self:status("Engineer · walking to "..(job.kind=="link" and "connection" or job.item.name)) end
end
return Jobs
