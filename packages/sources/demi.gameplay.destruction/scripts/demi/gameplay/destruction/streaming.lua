-- Proximity/lifetime policy. Geometry, damage and restoration remain native.
local Streaming = {}
Streaming.__index = Streaming
local function finite(n) return type(n)=="number" and n==n and math.abs(n)<math.huge end
local function position(p)
  assert(type(p)=="table" and #p==3,"position requires [x,y,z]")
  for i=1,3 do assert(finite(p[i]) and math.abs(p[i])<=1e6,"invalid position") end
end
local function copy(v,depth)
  if type(v)~="table" then return v end
  depth=(depth or 0)+1; assert(depth<=32,"state nesting exceeds 32")
  local result={}; for k,item in pairs(v) do result[k]=copy(item,depth) end
  return result
end
local function distance(a,b)
  return (a[1]-b[1])^2+(a[2]-b[2])^2+(a[3]-b[3])^2
end
local function keys(values)
  local result={};for key in pairs(values) do result[#result+1]=key end
  table.sort(result);return result
end
local defaults={load_distance=24,unload_distance=32,max_active=8,spawn_budget=1,
  scan_budget=256,debris_lifetime=0,activation_timeout=5}
function Streaming.new(services,definitions,options)
  for _,name in ipairs({"spawn","release","state","checkpoint","restore","retire"}) do
    assert(type(services[name])=="function","missing streaming service: "..name)
  end
  local settings=copy(defaults)
  for key,value in pairs(options or {}) do
    assert(defaults[key]~=nil and finite(value) and value>=0,"invalid streaming setting: "..key)
    settings[key]=value
  end
  assert(settings.load_distance>0 and settings.unload_distance>settings.load_distance,"unload distance must exceed load distance")
  assert(settings.activation_timeout>0,"activation timeout must be positive")
  for _,key in ipairs({"max_active","spawn_budget","scan_budget"}) do
    assert(settings[key]>=1 and settings[key]%1==0 and settings[key]<=100000,"invalid streaming budget")
  end
  assert(type(definitions)=="table" and #definitions<=1000000,"invalid wall catalogue")
  local self=setmetatable({services=services,settings=settings,entries={},cells={},active={},
    saved={},retry={},clock=0,last_error="",last_scanned=0,active_count=0},Streaming)
  for _,entry in ipairs(definitions) do
    assert(type(entry.id)=="string" and entry.id~="" and not self.entries[entry.id],"duplicate/invalid wall ID")
    assert(type(entry.prefab)=="string" and entry.prefab:match("^prefab://"),"invalid prefab")
    position(entry.position)
    if entry.rotation then position(entry.rotation) end
    if entry.scale then
      position(entry.scale)
      for i=1,3 do assert(entry.scale[i]>0,"placement scale must be positive") end
    end
    entry=copy(entry); self.entries[entry.id]=entry
    local key=math.floor(entry.position[1]/settings.load_distance)..":"..math.floor(entry.position[3]/settings.load_distance)
    local cell=self.cells[key] or {cursor=1,entries={}}; self.cells[key]=cell
    cell.entries[#cell.entries+1]=entry
  end
  return self
end
function Streaming:capture(entry)
  if entry.preserve==false then return nil end
  local state=self.services.state(entry)
  if state.status=="queued" or state.status=="unattached" then return nil,"Wall is not ready for checkpoint: "..entry.id end
  if state.status=="failed" then return nil,state.error end
  if state.revision==0 then return nil end
  local value,error=self.services.checkpoint(entry)
  if not value then return nil,error end
  return {prefab=entry.prefab,root=entry.root,position=copy(entry.position),
    rotation=copy(entry.rotation),scale=copy(entry.scale),checkpoint=value}
end
function Streaming:update(dt,observer)
  assert(finite(dt) and dt>=0,"invalid streaming dt"); position(observer)
  self.clock=self.clock+dt; self.last_scanned=0; self.last_error=""
  local retire={}
  for _,id in ipairs(keys(self.active)) do
    local active=self.active[id]
    local entry=active.entry; local state=self.services.state(entry)
    if (active.phase=="loading" or active.phase=="restoring") and self.clock-active.born>self.settings.activation_timeout then
      self.last_error="Wall activation timed out: "..id;active.phase="failed"
    elseif state.status=="failed" then
      self.last_error=state.error or ("Wall failed: "..id); active.phase="failed"
    elseif active.phase=="loading" and state.status~="unattached" then
      local saved=self.saved[id]
      if saved then
        local ok,error=self.services.restore(entry,copy(saved.checkpoint))
        if ok then active.phase="restoring" else self.last_error=error; active.phase="failed" end
      else active.phase="ready" end
    elseif active.phase=="restoring" and state.status~="queued" then
      if state.status=="applied" then active.phase="ready"
      else self.last_error=state.error; active.phase="failed" end
    end
    if active.phase=="failed" then
      if self.services.release(entry) then retire[#retire+1]=id; self.retry[id]=self.clock+2 end
    elseif active.phase=="ready" then
      if state.revision~=active.revision then active.revision=state.revision; active.age=0 end
      active.age=active.age+dt
      if active.cleaning and state.status=="applied" then active.cleaned=state.revision; active.cleaning=false end
      local lifetime=self.settings.debris_lifetime
      if lifetime>0 and state.revision>0 and active.age>=lifetime and
          active.cleaned~=state.revision and state.status=="applied" then
        local ok,error=self.services.retire(entry)
        if ok then active.cleaning=true else self.last_error=error end
      elseif distance(entry.position,observer)>self.settings.unload_distance^2 then
        local snapshot,error=self:capture(entry)
        if error then self.last_error=error
        elseif self.services.release(entry) then
          self.saved[id]=snapshot; retire[#retire+1]=id
        else self.last_error="Could not release wall: "..id end
      end
    end
  end
  for _,id in ipairs(retire) do self.active[id]=nil; self.active_count=self.active_count-1 end
  local candidates={}
  local cx=math.floor(observer[1]/self.settings.load_distance)
  local cz=math.floor(observer[3]/self.settings.load_distance)
  local allowance=math.max(1,math.floor(self.settings.scan_budget/9))
  for x=cx-1,cx+1 do for z=cz-1,cz+1 do
    local cell=self.cells[x..":"..z]
    if cell then
      for _=1,math.min(allowance,#cell.entries,self.settings.scan_budget-self.last_scanned) do
        local entry=cell.entries[cell.cursor]; cell.cursor=cell.cursor%#cell.entries+1
        self.last_scanned=self.last_scanned+1
        local d=distance(entry.position,observer)
        if not self.active[entry.id] and (self.retry[entry.id] or 0)<=self.clock and d<=self.settings.load_distance^2 then
          candidates[#candidates+1]={entry=entry,distance=d}
        end
      end
    end
  end end
  table.sort(candidates,function(a,b) return a.distance==b.distance and a.entry.id<b.entry.id or a.distance<b.distance end)
  local spawned=0
  for i=1,#candidates do
    if spawned>=self.settings.spawn_budget then break end
    local entry=candidates[i].entry
    if self.active_count>=self.settings.max_active then
      local worst,worst_distance=nil,math.sqrt(candidates[i].distance)+2
      for id,active in pairs(self.active) do
        local d=math.sqrt(distance(active.entry.position,observer))
        if active.phase=="ready" and (d>worst_distance or (d==worst_distance and (not worst or id>worst))) then worst=id;worst_distance=d end
      end
      if worst then
        local snapshot,error=self:capture(self.active[worst].entry)
        if error then self.last_error=error
        elseif self.services.release(self.active[worst].entry) then
          self.saved[worst]=snapshot;self.active[worst]=nil;self.active_count=self.active_count-1
        end
      end
    end
    if self.active_count<self.settings.max_active and self.services.spawn(entry) then
      self.active[entry.id]={entry=entry,phase="loading",age=0,revision=0,born=self.clock}
      self.active_count=self.active_count+1;spawned=spawned+1
    elseif self.active_count<self.settings.max_active then
      self.last_error="Could not activate wall: "..entry.id; self.retry[entry.id]=self.clock+2
      spawned=spawned+1 -- Failed preparation still consumes this update's budget.
    end
  end
end
function Streaming:export_state()
  local walls=copy(self.saved)
  for _,id in ipairs(keys(self.active)) do
    local active=self.active[id]
    if active.phase~="ready" then return nil,"Wall activation is incomplete: "..id end
    local value,error=self:capture(active.entry)
    if error then return nil,error end
    walls[id]=value
  end
  return {format_version=1,walls=walls}
end
function Streaming:import_state(state)
  if self.active_count~=0 then return false,"Load checkpoints before activation" end
  local ok,value=pcall(function()
    assert(state.format_version==1 and type(state.walls)=="table","Invalid streaming save")
    local saved={}
    for id,record in pairs(state.walls) do
      local entry=assert(self.entries[id],"Unknown saved wall: "..tostring(id))
      assert(record.prefab==entry.prefab and type(record.checkpoint)=="table","Saved wall definition changed")
      position(record.position)
      assert(distance(record.position,entry.position)==0,"Saved wall placement changed")
      assert((record.root or "assembly")== (entry.root or "assembly"),"Saved wall root changed")
      local saved_rotation,rotation=record.rotation or {0,0,0},entry.rotation or {0,0,0}
      local saved_scale,scale=record.scale or {1,1,1},entry.scale or {1,1,1}
      position(saved_rotation);position(saved_scale)
      assert(distance(saved_rotation,rotation)==0 and distance(saved_scale,scale)==0,"Saved wall orientation or scale changed")
      saved[id]=copy(record)
    end
    return saved
  end)
  if not ok then return false,tostring(value) end
  self.saved=value; return true
end
function Streaming:dispose()
  for _,id in ipairs(keys(self.active)) do self.services.release(self.active[id].entry) end
  self.active={}; self.active_count=0
end
return Streaming
