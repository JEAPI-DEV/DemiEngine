local Entity = require("demi.entity")
local Hud = require("demi.hud")
local Prefab = require("demi.prefab")
local Ground = require("demi.navigation.ground")
local Logistics = require("colony.logistics")
local Worker = require("colony.worker")
local Jobs={};Jobs.__index=Jobs
function Jobs.new(config,network,terrain,resources)
  local p=Entity.world_position(terrain.id)
  local surface=Ground.create({terrain=terrain.id,origin={p[1],p[3]},columns=config.navigation_cells,rows=config.navigation_cells,
    cell_size=2,agent_radius=0.5,ignored_entity=config.engineer})
  local logistics=Logistics.new(config)
  local self=setmetatable({config=config,network=network,terrain=terrain,surface=surface,logistics=logistics,
    queue={},next_id=1,next_link=1,revision=-1,display_elapsed=0},Jobs)
  self.worker=Worker.new(config,surface,resources,logistics,network)
  return self
end
function Jobs:available_metal() return self.logistics:available() end
function Jobs:status(text)
  if self.last_status~=text then Hud.set_text("job_status",text);self.last_status=text end
end
function Jobs:publish()
  self.logistics:publish()
  self.worker.needs:publish()
end
function Jobs:enqueue_build(kind,item,site)
  if self:available_metal()<item.cost then return nil,"Not enough metal" end
  local number=self.next_id;local id="built_"..number;local marker="site_"..number
  local options={id=marker,position={site.x,site.y,site.z},overrides={base={components={
    MeshRenderer={size={item.width,0.25,item.depth}},BoxCollider3D={size={item.width,0.5,item.depth},offset={0,0.25,0}}
  }}}}
  if not Prefab.instantiate("prefab://utilities/site",options) then return nil,"Could not create construction site" end
  local node={id=id.."/base",site_id=marker.."/base",kind=kind,width=item.width,depth=item.depth,x=site.x,y=site.y,z=site.z,complete=false}
  self.network.nodes[node.id]=node;self.network.revision=self.network.revision+1
  local job={id=id,marker=marker,node=node,kind="building",item=item,site=site,cost=item.cost,x=site.x,z=site.z,
    width=item.width,depth=item.depth,progress=0,state="queued"}
  assert(self.logistics:reserve(job))
  self.queue[#self.queue+1]=job;self.next_id=number+1;self:publish()
  return job,"Construction queued · materials must be hauled from the depot"
end
function Jobs:enqueue_link(a,b)
  local points,error=self.network:route(a,b)
  if not points then return nil,error end
  if self:available_metal()<1 then return nil,"Not enough metal" end
  local id="connection_"..self.next_link
  if not self.network:draw_link(id,points,true) then return nil,"Could not create connection" end
  local edge={id=id,a=a,b=b,complete=false,points=points}
  self.network.edges[#self.network.edges+1]=edge
  local mid=points[math.ceil(#points/2)]
  local job={id=id,edge=edge,kind="link",cost=1,x=mid[1],z=mid[3],ground_y=mid[2]-0.3,width=1,depth=1,progress=0,state="queued"}
  assert(self.logistics:reserve(job))
  self.queue[#self.queue+1]=job;self.next_link=self.next_link+1;self:publish()
  return job,"Connection queued · utilities stay offline until it is finished"
end
function Jobs:cancel(job)
  if not job or job.state=="complete" or job.state=="cancelled" then return false end
  local cancelled=self.logistics:cancel(job)
  if not cancelled then return false end
  if self.active==job then self.worker:interrupt();self.active=nil end
  if job.kind=="building" then
    Prefab.release(job.marker);self.network.nodes[job.node.id]=nil;self.network.revision=self.network.revision+1
  else self.network:remove(job.edge) end
  self.surface:invalidate()
  job.state="cancelled";self:publish();return true
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
  local depot=Entity.world_position(self.config.depot)
  if depot then boxes[#boxes+1]={min_x=depot[1]-2,max_x=depot[1]+2,min_z=depot[3]-2,max_z=depot[3]+2} end
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
  self.logistics:consume(job)
  job.state="complete";self.worker:interrupt();self.active=nil;self:publish();return true
end
function Jobs:update(dt)
  self:update_obstacles()
  self.display_elapsed=self.display_elapsed+dt
  if self.display_elapsed>=0.2 then self.display_elapsed=0;self:publish() end
  if not self.surface:update(96) then self:status("Engineer · surveying terrain");return end
  if not self.active then
    for _,job in ipairs(self.queue) do
      if job.state=="queued" then self.active=job;break end
    end
  end
  local job=self.active
  if job and job.state=="failed" then
    self:status("Construction failed · cancel to recover materials");return
  end
  local finished,message=self.worker:update(dt,job)
  self:status(message)
  if finished and job then self:finish(job) end
end
return Jobs
