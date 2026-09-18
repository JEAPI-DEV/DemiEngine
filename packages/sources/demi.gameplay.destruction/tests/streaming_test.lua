local Test=require("demi.test")
local Streaming=require("demi.gameplay.destruction.streaming")
local function fixture(definitions,options)
  local calls={spawned=0,released=0,restored=0,retired=0,states={}}
  local services={
    spawn=function(e) calls.spawned=calls.spawned+1;calls.states[e.id]={status="ready",revision=0};return true end,
    release=function(e) calls.released=calls.released+1;calls.states[e.id]=nil;return true end,
    state=function(e) return calls.states[e.id] or {status="unattached",revision=0} end,
    checkpoint=function(e) return {damage=calls.states[e.id].revision} end,
    restore=function(e,s) calls.restored=calls.restored+1;calls.states[e.id]={status="applied",revision=s.damage};return true end,
    retire=function(e) calls.retired=calls.retired+1;local s=calls.states[e.id];s.revision=s.revision+1;return true end,
  }
  return Streaming.new(services,definitions,options),calls
end
Test.case("100000 wall records do not instantiate 100000 prefabs",function()
  local definitions={}
  for i=1,100000 do definitions[i]={id="wall"..i,prefab="prefab://wall",position={i*5,0,0}} end
  local stream,calls=fixture(definitions,{max_active=4,spawn_budget=1,scan_budget=36})
  Test.equal(calls.spawned,0)
  for i=1,20 do stream:update(.1,{0,0,0});Test.truthy(stream.last_scanned<=36);Test.truthy(stream.active_count<=4) end
  Test.equal(calls.spawned,4)
  for i=1,20 do stream:update(.1,{50000,0,0});Test.truthy(stream.last_scanned<=36);Test.truthy(stream.active_count<=4) end
  Test.truthy(calls.released>=4);Test.truthy(calls.spawned<=8)
end)
Test.case("damage survives distance unloading and optional save roundtrip",function()
  local definitions={{id="a",prefab="prefab://wall",position={0,0,0}}}
  local stream,calls=fixture(definitions)
  stream:update(.1,{0,0,0});stream:update(.1,{0,0,0})
  calls.states.a={status="applied",revision=3}
  stream:update(.1,{100,0,0});Test.equal(stream.active_count,0)
  local save=assert(stream:export_state());Test.equal(save.walls.a.checkpoint.damage,3)
  local other,restored=fixture(definitions);Test.truthy(other:import_state(save))
  for i=1,4 do other:update(.1,{0,0,0}) end
  Test.equal(restored.restored,1);Test.equal(restored.states.a.revision,3)
  save.walls.a.checkpoint.damage=10
  Test.equal(restored.states.a.revision,3)
end)
Test.case("pending damage is not discarded on unload",function()
  local stream,calls=fixture({{id="a",prefab="prefab://wall",position={0,0,0}}})
  stream:update(.1,{0,0,0});stream:update(.1,{0,0,0})
  calls.states.a.status="queued"
  stream:update(.1,{100,0,0});Test.equal(calls.released,0)
  Test.equal(stream:export_state(),nil)
  calls.states.a={status="applied",revision=1}
  stream:update(.1,{100,0,0});Test.equal(calls.released,1)
end)
Test.case("lifetime cleanup is opt-in and does not repeat without new damage",function()
  local stream,calls=fixture({{id="a",prefab="prefab://wall",position={0,0,0}}},{debris_lifetime=1})
  stream:update(.1,{0,0,0});stream:update(.1,{0,0,0});calls.states.a={status="applied",revision=1}
  for i=1,30 do stream:update(.1,{0,0,0}) end
  Test.equal(calls.retired,1)
end)
Test.case("closer walls replace farther ready walls when the active budget is full",function()
  local stream,calls=fixture({
    {id="a",prefab="prefab://wall",position={0,0,0}},
    {id="b",prefab="prefab://wall",position={15,0,0}},
  },{max_active=1})
  stream:update(.1,{0,0,0});stream:update(.1,{0,0,0})
  stream:update(.1,{10,0,0})
  Test.equal(calls.released,1);Test.truthy(stream.active.b);Test.equal(stream.active_count,1)
end)
Test.case("an unattached activation times out instead of leaking a capacity slot",function()
  local stream,calls=fixture({{id="a",prefab="prefab://wall",position={0,0,0}}},{activation_timeout=.5})
  stream:update(.1,{0,0,0});calls.states.a=nil
  for i=1,10 do stream:update(.1,{0,0,0}) end
  Test.equal(calls.released,1);Test.equal(stream.active_count,0)
end)
Test.case("placement rotation and scale are preserved and stale checkpoints rejected",function()
  local definitions={{id="a",prefab="prefab://wall",root="assembly",position={2,0,0},rotation={0,1,0},scale={2,1,1}}}
  local stream,calls=fixture(definitions)
  stream:update(.1,{0,0,0});stream:update(.1,{0,0,0})
  calls.states.a={status="applied",revision=1}
  local saved=assert(stream:export_state())
  Test.equal(saved.walls.a.rotation[2],1);Test.equal(saved.walls.a.scale[1],2)
  local other=fixture(definitions);Test.truthy(other:import_state(saved))
  definitions[1].rotation={0,2,0}
  local rotated=fixture(definitions);Test.equal(rotated:import_state(saved),false)
  definitions[1].rotation={0,1,0};definitions[1].scale={1,1,1}
  local scaled=fixture(definitions);Test.equal(scaled:import_state(saved),false)
  definitions[1].scale={0,1,1}
  Test.equal(pcall(function() fixture(definitions) end),false)
end)
