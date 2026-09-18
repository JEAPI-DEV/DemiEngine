local Test = require("demi.test")
local Weapons = require("demi.gameplay.destruction.weapons")
local V = {}
function V.add(a,b) return {a[1]+b[1],a[2]+b[2],a[3]+b[3]} end
function V.scale(a,s) return {a[1]*s,a[2]*s,a[3]*s} end
function V.length(a) return math.sqrt(a[1]^2+a[2]^2+a[3]^2) end
function V.normalized(a) return V.scale(a,1/V.length(a)) end

local function fixture(options)
  local calls = {hits={}, casts={}, spawned=0, released=0, moved=0}
  local services = {
    vector=V,
    cast=function(o,d,length,radius)
      calls.casts[#calls.casts+1] = length
      if calls.miss then return nil end
      if o[3] <= -2 or (d[3] < 0 and (o[3]+2)/-d[3] <= length) then
        return {entity_id="wall", point={0,0,-2}, normal={0,0,1}}
      end
    end,
    impact=function(hit,target)
      calls.hits[#calls.hits+1]=hit
      calls.target=target
      return true,"",1,{root="assembly",revision=2,target=target}
    end,
    spawn=function() calls.spawned=calls.spawned+1; return not calls.fail_spawn end,
    move=function() calls.moved=calls.moved+1 end,
    release=function() calls.released=calls.released+1 end,
  }
  options = options or {}; options.id_prefix="fixture/"
  return Weapons.new(services, options), calls
end

Test.case("hammer only damages once at contact, within reach", function()
  local w,c=fixture()
  Test.truthy(w:hammer()); Test.equal(w:hammer(),false)
  w:update(0.15,{0,0,0},{0,0,-1}); Test.equal(#c.hits,0)
  w:update(0.12,{0,0,0},{0,0,-1}); Test.equal(#c.hits,0)
  w:update(0.02,{0,0,0},{0,0,-1}); Test.equal(#c.hits,1)
  w:update(2,{0,0,0},{0,0,-1}); Test.equal(#c.hits,1)
  Test.equal(c.hits[1].entity,"wall")
  Test.equal(c.target,"wall")
  local events=w:drain_events()
  Test.equal(events[#events].receipt.root,"assembly")
  Test.equal(events[#events].receipt.revision,2)
  local short, missed=fixture({hammer_reach=1})
  short:hammer(); short:update(2,{0,0,0},{0,0,-1}); Test.equal(#missed.hits,0)
end)

Test.case("rocket travels then sweeps through thin geometry exactly once", function()
  local w,c=fixture()
  Test.truthy(w:fire({0,0,0},{0,0,-2}))
  Test.equal(#c.hits,0); Test.equal(w.ammo,7)
  Test.equal(w:fire({0,0,0},{0,0,-1}),false)
  w:update(0.1,{0,0,0},{0,0,-1}); Test.equal(#c.hits,0); Test.equal(c.moved,1)
  w:update(1,{0,0,0},{0,0,-1}); Test.equal(#c.hits,1); Test.equal(c.released,1)
  Test.equal(c.hits[1].entity,nil); Test.equal(c.hits[1].direction,nil)
  Test.equal(c.target,"wall") -- diagnostic target must not filter radial damage
  w:update(1,{0,0,0},{0,0,-1}); Test.equal(#c.hits,1)
end)

Test.case("expiry clips travel to remaining lifetime and does not explode", function()
  local w,c=fixture({rocket_life=0.2}); c.miss=true
  w:fire({0,0,0},{0,0,-1}); w:update(10,{0,0,0},{0,0,-1})
  Test.near(c.casts[#c.casts],1.6); Test.equal(#c.hits,0); Test.equal(c.released,1)
end)

Test.case("spawn failures, limits, ammo and disposal do not leak state", function()
  local w,c=fixture({ammo=1,max_rockets=1,rocket_cooldown=0}); c.miss=true; c.fail_spawn=true
  Test.equal(w:fire({0,0,0},{0,0,-1}),false); Test.equal(w.ammo,1)
  c.fail_spawn=false; Test.truthy(w:fire({0,0,0},{0,0,-1}))
  Test.equal(w:fire({0,0,0},{0,0,-1}),false); Test.equal(w:reload(),false)
  w:dispose(); Test.equal(c.released,1); Test.equal(#w.rockets,0)
  Test.truthy(w:reload()); Test.equal(w.ammo,1)
end)

Test.case("initial overlaps detonate without creating an orphan visual", function()
  local w,c=fixture()
  Test.truthy(w:fire({0,0,-3},{0,0,-1}))
  Test.equal(#c.hits,1); Test.equal(c.spawned,0); Test.equal(#w.rockets,0)
end)

Test.case("invalid configuration, aim and time fail before emitting effects", function()
  Test.equal(pcall(function() fixture({rocket_speed=0}) end),false)
  Test.equal(pcall(function() fixture({rocket_speeed=12}) end),false)
  local w,c=fixture()
  Test.equal(pcall(function() w:fire(nil,{0,0,-1}) end),false)
  Test.equal(pcall(function() w:fire({0,0,0},{0,0,0}) end),false)
  Test.equal(pcall(function() w:update(-1,{0,0,0},{0,0,-1}) end),false)
  Test.equal(#c.casts,0); Test.equal(c.spawned,0); Test.equal(w.ammo,8)
end)
