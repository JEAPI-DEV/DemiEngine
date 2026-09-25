local Test = require("demi.test")
local Entity = require("demi.entity")
local Destruction = require("demi.physics.destruction3d")
local Physics = require("demi.physics.query3d")
local Body = require("demi.physics.rigidbody3d")
local Transform = require("demi.transform3d")
local Hud = require("demi.hud")
local Prefab = require("demi.prefab")

local function stats() return Entity.get("player","GameplayData","values") end
local function reset_scene()
  Test.touch("reset")
  for attempt=1,60 do
    Test.wait(0.05)
    if Destruction.state("a/assembly").revision==0 and stats().contacts==0 then
      Test.wait(0.05)
      return
    end
  end
  Test.expect(false,"Scene reset did not finish")
end

return { tests = {{ name="weapons, streamed damage checkpoints and debris cleanup", func=function()
  Test.wait(0.35)
  local placements=Prefab.placements("world_stream")
  Test.expect(#placements==2 and placements[1].id=="a" and placements[2].id=="b",
    "Streaming must read the authored prefab placement entities")
  Test.expect(not Entity.exists("a/__preview/assembly"),"Editor preview geometry leaked into Play")
  Test.expect(type(stats().contacts)=="number","Player script must initialize its weapon state")
  Test.expect(Destruction.state("a/assembly").bodies==1,"Doorway A must attach")
  Test.expect(Destruction.state("b/wall/assembly").bodies==1,"Doorway B must attach")
  Test.expect(not Entity.find("Masonry cell shard "),
    "Intact masonry must not create live brick shard entities")
  Test.touch("hammer")
  Test.wait(0.06)
  Test.expect(stats().phase~="idle","Hammer HUD action must start an attack")
  Test.expect(stats().contacts==0,"Hammer must not damage during windup")
  -- Package tests check exact phase durations. Here wait for fixed-step contact
  -- and commit, which can trail render/test time during first-use preparation.
  for attempt=1,50 do
    Test.wait(0.02)
    if stats().contacts>=1 and Destruction.state("a/assembly").revision>=1 then break end
  end
  Test.wait(0.05)
  Test.expect(stats().contacts==1,"Hammer must contact once at the end of its swing")
  Test.expect(Destruction.state("a/assembly").revision==1,"Hammer must use native spatial damage")
  Test.expect(Hud.get_text("status"):find("Impact applied",1,true)~=nil,
    "Hammer HUD must confirm native completion instead of remaining queued")
  Test.wait(0.4)
  Test.expect(stats().contacts==1,"Idle/recovery must not keep dealing damage")
  Test.expect(Destruction.state("b/wall/assembly").revision==0,"Other prefab must stay intact")

  reset_scene()
  local door=Entity.find("Steel door shard ")
  Test.expect(door and Entity.parent(door)=="a/assembly","First steel door must be a single generated visual: "..tostring(door).." parent="..tostring(door and Entity.parent(door)))
  local vertices=Entity.get(door,"MeshRenderer","vertices")
  Test.expect(vertices and #vertices==36,"Steel door must remain one intact box mesh")
  Test.touch("rocket")
  Test.wait(0.06)
  Test.expect(stats().shots==1 and stats().detonations==0 and stats().active_rockets==1,
    "Rocket must have travel time and must not detonate on the intervening trigger")
  Test.expect(#Entity.query({tags={"weapon_rocket"}})==1,"Flying rocket must have a real scene visual")
  Test.wait(0.6)
  Test.expect(stats().detonations==1 and stats().active_rockets==0,"Rocket must detonate exactly once and release its visual")
  Test.expect(#Entity.query({tags={"weapon_rocket"}})==0,"Detonated rocket leaked a visual")
  local state=Destruction.state("a/assembly")
  Test.expect(state.status=="applied" and state.bodies>1,"Blast must fracture the concrete: "..state.error)
  -- Further shots are allowed for this authored resistance, but remain real
  -- travelling projectiles. No test manually changes bonds or physical owners.
  for shot=1,2 do
    local owner=Entity.parent(door)
    local body=Body.state(owner)
    if body and body.body_type=="dynamic" then break end
    Test.touch("rocket"); Test.wait(0.8)
  end
  local owner=Entity.parent(door)
  local body=Body.state(owner)
  Test.expect(owner~="a/assembly" and body and body.body_type=="dynamic",
    "Steel door must release with physical ownership: owner="..tostring(owner).." type="..tostring(body and body.body_type))
  Test.expect(body.use_gravity,"Released steel door must retain gravity")
  Test.expect(#Entity.get(door,"MeshRenderer","vertices")==36,"Steel door was fragmented or lost geometry")
  local collision=false
  for _,hit in ipairs(Physics.overlap_sphere_all(-2.8,1,0,12)) do
    if hit.entity_id==owner then collision=true end
  end
  Test.expect(collision,"Released steel door must retain native collision")
  Test.expect(Destruction.state("b/wall/assembly").revision==0,"Out-of-range second doorway received damage")
  reset_scene()
  Test.expect(Destruction.state("a/assembly").bodies==1 and #Entity.query({tags={"weapon_rocket"}})==0,
    "Reset must restore the prefab and retire projectile state")

  -- Isolate hinge release from the rubble pile: both attachments broken must
  -- leave a collidable door which a normal hammer impulse can move.
  door=Entity.find("Steel door shard ")
  for _,y in ipairs({0.65,1.75}) do
    local hit=Physics.raycast(-3.53,y,1,0,0,-1,2)
    Test.expect(hit and hit.collider_part_id,"Hinge must have a real collider part")
    local ok,issue=Destruction.damage_part(hit.entity_id,hit.collider_part_id,100)
    Test.expect(ok,issue)
  end
  Test.wait(0.1)
  owner=Entity.parent(door)
  body=Body.state(owner)
  Test.expect(body and body.body_type=="dynamic","Broken hinges must release the door")
  Test.expect(math.abs(body.mass-31.92)<0.5,"Door must use its own effective density, not masonry density")
  local before=Entity.world_position(door)
  local _,_,beforeUpZ=Transform.up(owner)
  local _,_,beforeForwardZ=Transform.forward(owner)
  local hit=Physics.raycast(-2.8,1.2,1,0,0,-1,2)
  Test.expect(hit and hit.entity_id==owner,"Released door must still be hittable")
  local ok,issue=Destruction.impact({entity=owner,position=hit.point,
    radius=0.15,impulse=220,direction={0,0,-1}})
  Test.expect(ok,issue)
  Test.wait(0.35)
  local after=Entity.world_position(door)
  local _,_,afterUpZ=Transform.up(owner)
  local _,_,afterForwardZ=Transform.forward(owner)
  -- The shard geometry uses assembly-space vertices, not a centered pivot.
  before[3]=before[3]+beforeUpZ*1.05+beforeForwardZ*0.02
  after[3]=after[3]+afterUpZ*1.05+afterForwardZ*0.02
  Test.expect(after[3]<before[3]-0.25,"A hammer must knock the unhinged door out of the frame")
  -- Streaming must remove actual native/world ownership, then restore damage.
  local saved,saveError=Destruction.checkpoint("a/assembly")
  Test.expect(saved~=nil,saveError)
  Test.expect(Destruction.retire_debris("a/assembly"),"Could not queue opt-in debris cleanup")
  Test.wait(0.1)
  Test.expect(not Entity.exists(door),"Debris cleanup retained the loose door visual")
  local remaining=Destruction.state("a/assembly").bodies
  Transform.set_position("player",0,1,100)
  for attempt=1,80 do
    Test.wait(0.05)
    if not Entity.exists("a/assembly") and not Entity.exists("b/wall/assembly") then break end
  end
  Test.expect(not Entity.exists("a/assembly") and not Entity.exists("b/wall/assembly"),"Distant walls did not unload")
  Test.expect(Destruction.state("a/assembly").status=="unattached","Unloaded wall retained native ownership")
  Transform.set_position("player",-4.175,.92,2.3)
  for attempt=1,100 do
    Test.wait(0.05)
    if Destruction.state("a/assembly").status=="applied" and Destruction.state("b/wall/assembly").status=="ready" then break end
  end
  local returned=Destruction.state("a/assembly")
  Test.expect(returned.status=="applied", "Checkpoint restoration failed: "..returned.error)
  Test.expect(returned.bodies==remaining and not Entity.exists(door),"Streaming resurrected missing debris or reset damage")
  Test.expect(Destruction.state("b/wall/assembly").revision==0,"Pristine wall did not return pristine")
  local right="b/wall/assembly"
  local brick=Physics.raycast(1.425,1.6,1,0,0,-1,2)
  Test.expect(brick and brick.collider_part_id,"Right masonry was not hittable")
  local mapping=Entity.get(right,"Destructible3D","parts")
  local visual=mapping and mapping[brick.collider_part_id]
  Test.expect(visual~=nil,"Right brick has no authored visual mapping")
  local accepted,issue=Destruction.damage_part(right,brick.collider_part_id,100)
  Test.expect(accepted,issue)
  Test.wait(0.15)
  Test.expect(Entity.exists(visual),"Real detached brick disappeared before its fade")
  local detached=Entity.parent(visual)
  Test.expect(detached and not Body.state(detached),"Fading brick retained a physics body")
  Test.wait(3.1)
  Test.expect(not Entity.exists(visual),"Faded brick was not removed")
  local doorHit=Physics.raycast(2.8,1.2,1,0,0,-1,2)
  Test.expect(doorHit~=nil,"Right door incorrectly disappeared with masonry")
end }} }
