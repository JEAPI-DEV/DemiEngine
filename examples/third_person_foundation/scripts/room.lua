local Character = require("demi.gameplay.third_person")
local Orbit = require("demi.gameplay.orbit_camera")
local Melee = require("demi.gameplay.melee")
local Room = {}
local function position(id)
  local x,y,z = Transform3D.get_position(id)
  return {x=x,y=y,z=z}
end
local function face(id,x,z) Transform3D.set_rotation(id,0,math.atan(x,z),0) end

function Room:on_create()
  self.character = Character.new()
  self.camera = Orbit.new()
  self.player_attack = Melee.new({windup=0.22,active=0.15,recovery=0.35,reach=2.4,damage=20})
  self.enemy_attack = Melee.new({windup=0.8,active=0.15,recovery=1.2,reach=2.5,damage=20})
  self.player_health, self.enemy_health = 100,100
  self.cooldown, self.enemy_x, self.enemy_z = 1,0,1
  self.pending = {}
  self.captured = true
  Application.set_mouse_captured(true)
  Debug.log("Third person foundation ready")
end

function Room:on_update()
  if Input.pressed("reset") then Scene.reload(); return end
  if Input.pressed("cursor") then
    self.captured = not self.captured
    Application.set_mouse_captured(self.captured)
  end
  if Input.pressed("lock") then self.locked = not self.locked end
  if self.captured then
    self.pending.roll = self.pending.roll or Input.pressed("roll")
    self.pending.attack = self.pending.attack or Input.pressed("attack")
  end
  local target = position("player/body")
  local dx,dy = Input.mouse_delta()
  self.fx,self.fz = self.camera:look(self.captured and dx or 0,
    self.captured and dy or 0,target,self.locked and position("enemy/body") or nil)
  local eye,focus = self.camera:position(target,function(origin,radius,direction,distance)
    local hit = Physics3D.sphere_cast(origin.x,origin.y,origin.z,radius,
      direction.x,direction.y,direction.z,distance,"world","player/body")
    return hit and hit.distance
  end)
  Transform3D.set_position("camera",eye.x,eye.y,eye.z)
  Transform3D.look_at("camera",focus.x,focus.y,focus.z)
  Hud.set_text("status",string.format("Player %d | Enemy %d | Stamina %.0f | Enemy: %s",
    self.player_health,self.enemy_health,self.character.stamina,self.enemy_attack:phase()))
end

function Room:on_fixed_update(dt)
  if self.player_health <= 0 or self.enemy_health <= 0 then
    CharacterController3D.set_velocity("player/body",0,0,0)
    CharacterController3D.set_velocity("enemy/body",0,0,0)
    return
  end
  -- Advance, process both combatants' intent, then resolve hits. This makes
  -- roll immunity independent of entity/script update ordering.
  self.player_attack:update(dt)
  self.enemy_attack:update(dt)
  local state = self.character:update({
    x=self.captured and Input.value("right") or 0,
    y=self.captured and Input.value("forward") or 0,
    roll=self.pending.roll},self.fx or 0,self.fz or -1,dt,self.player_attack.running)
  if self.pending.attack and state ~= "roll" then self.player_attack:start() end
  self.pending = {}
  local c = self.character
  face("player/body",c.facing_x,c.facing_z)
  CharacterController3D.set_velocity("player/body",self.player_attack.running and 0 or c.vx,
    0,self.player_attack.running and 0 or c.vz)
  local p,e = position("player/body"),position("enemy/body")
  local dx,dz = p.x-e.x,p.z-e.z
  local distance = math.sqrt(dx*dx+dz*dz)
  local nx,nz = dx/math.max(distance,0.001),dz/math.max(distance,0.001)
  if not self.enemy_attack.running then
    self.cooldown = math.max(0,self.cooldown-dt)
    self.enemy_x,self.enemy_z = nx,nz
    face("enemy/body",nx,nz)
    if distance < 2.2 and self.cooldown == 0 then
      self.enemy_attack:start()
      self.cooldown = 0.8
    end
  end
  local speed = not self.enemy_attack.running and distance > 2 and 1.8 or 0
  CharacterController3D.set_velocity("enemy/body",nx*speed,0,nz*speed)
  self.player_health = math.max(0,self.player_health-self.enemy_attack:hit("player",
    distance,nx*self.enemy_x+nz*self.enemy_z,c:invulnerable()))
  self.enemy_health = math.max(0,self.enemy_health-self.player_attack:hit("enemy",
    distance,-nx*c.facing_x-nz*c.facing_z,false))
  for _,entry in ipairs({{"player",self.player_attack},{"enemy",self.enemy_attack}}) do
    local id,attack = entry[1],entry[2]
    local phase = attack:phase()
    local angle = phase=="windup" and -1.2 or phase=="active" and 0.9 or 0
    Transform3D.set_rotation(id.."/hand",angle,0,0)
  end
  local roll_angle = c.roll_left>0 and (1-c.roll_left/c.roll_duration)*math.pi*2 or 0
  Transform3D.set_rotation("player/visual",roll_angle,0,0)
end

function Room:on_destroy() Application.set_mouse_captured(false) end
return Room
