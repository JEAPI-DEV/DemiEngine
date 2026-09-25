local Weapons = require("demi.gameplay.destruction.weapons")
local Native = require("demi.gameplay.destruction.native")
local Input = require("demi.input")
local Application = require("demi.application")
local Camera = require("demi.camera3d")
local Transform = require("demi.transform3d")
local Character = require("demi.physics.character_controller3d")
local Vector = require("demi.math.vector3")
local Entity = require("demi.entity")
local Hud = require("demi.hud")
local Scene = require("demi.scene")
local Destruction = require("demi.physics.destruction3d")
local Body = require("demi.physics.rigidbody3d")

---@demi_component
local Player = {}
---@demi_property number
Player.speed = 3

function Player:on_start()
  self.weapon = Weapons.new(Native.new({owner=self.entity_id,rocket_prefab="prefab://rocket"}),
    {id_prefix=self.entity_id.."/rocket/"})
  self.yaw, self.pitch, self.move = 0, 0, {0,0,0}
  self.hammer_rest={Transform.get_position("hammer_view")}
  self.hammer_rotation={Transform.get_rotation("hammer_view")}
  Application.set_mouse_captured(false)
end

function Player:ray()
  local w,h=Input.viewport_size()
  return Camera.screen_ray("camera",w*0.5,h*0.5,w,h)
end

-- @HandleAction("hammer")
function Player:on_hammer()
  if not self.weapon then return end
  local ok,issue=self.weapon:hammer()
  if not ok then Hud.set_text("status",issue) end
end

-- @HandleAction("rocket")
function Player:on_rocket()
  if not self.weapon then return end
  local ray=self:ray(); if not ray then return end
  local muzzle=Vector.add(ray.origin,Vector.scale(ray.direction,0.6))
  local ok,issue=self.weapon:fire(muzzle,ray.direction)
  if not ok then Hud.set_text("status",issue) end
end

-- @HandleAction("reload")
function Player:on_reload()
  if self.weapon then
    local ok,issue=self.weapon:reload()
    Hud.set_text("status",ok and "Rockets refilled" or issue)
  end
end

-- @HandleAction("reset")
function Player:on_reset() Scene.reload() end

function Player:on_update(dt)
  if Input.pressed("reset") then self:on_reset(); return end
  if Input.pressed("reload") then self:on_reload() end
  if Input.pressed("cursor") then
    Application.set_mouse_captured(not Application.mouse_captured())
    self.ignore_mouse=true
  end
  local captured=Application.mouse_captured()
  if not captured and Input.pressed("hammer") and not Input.ui_pointer_captured() then
    Application.set_mouse_captured(true); self.ignore_mouse=true; return
  end
  if captured then
    if self.ignore_mouse then self.ignore_mouse=false
    else
      local dx,dy=Input.mouse_delta()
      self.yaw=self.yaw-dx*0.002
      self.pitch=math.max(-1.3,math.min(1.3,self.pitch-dy*0.002))
      Transform.set_rotation("camera",self.pitch,self.yaw,0)
    end
    if not Input.ui_pointer_captured() then
      if Input.pressed("hammer") then self:on_hammer() end
      if Input.pressed("rocket") then self:on_rocket() end
    end
  end
  local ray=self:ray()
  if ray and captured then
    local forward=Vector.normalized({ray.direction[1],0,ray.direction[3]})
    local right=Vector.cross(forward,{0,1,0})
    self.move=Vector.scale(Vector.normalized(Vector.add(
      Vector.scale(forward,Input.value("move_forward")),
      Vector.scale(right,Input.value("move_right")))),self.speed)
  else self.move={0,0,0} end
  if not self.weapon then return end
  local phase,t=self.weapon:hammer_phase()
  local angle=phase=="windup" and 0.9*t or phase=="swing" and (0.9-1.7*t)
    or phase=="recovery" and -0.8*(1-t) or 0
  local extension=phase=="swing" and 0.9*t or phase=="recovery" and 0.9*(1-t) or 0
  Transform.set_position("hammer_view",self.hammer_rest[1],self.hammer_rest[2],self.hammer_rest[3]-extension)
  Transform.set_rotation("hammer_view",self.hammer_rotation[1]+angle,self.hammer_rotation[2],self.hammer_rotation[3])
  for _,event in ipairs(self.weapon:drain_events()) do
    if event.kind=="hammer_miss" then
      self.pending_impact=nil
      Hud.set_text("status","Hammer missed — move within reach")
    elseif event.kind=="hammer_contact" or event.kind=="explosion" then
      self.pending_impact=event.accepted and event.receipt or nil
      self.impact_wait=0
      local queued=event.receipt and "Impact queued" or
        "Blast queued for nearby assemblies (completion not tracked)"
      Hud.set_text("status",event.accepted and queued or (event.error or "No destructible hit"))
    elseif event.kind=="rocket_fired" then
      self.pending_impact=nil
      Hud.set_text("status","Rocket in flight")
    end
  end
  if self.pending_impact then
    local receipt=self.pending_impact
    local state=Destruction.state(receipt.root)
    self.impact_wait=self.impact_wait+dt
    if state.status=="failed" then
      Hud.set_text("status","Impact failed: "..state.error)
      self.pending_impact=nil
    elseif state.revision>receipt.revision then
      local body=Body.state(receipt.target)
      local message="Impact applied | structure changed"
      if body then
        if body.body_type=="static" then message="Impact applied | still attached to support"
        else message=string.format("Impact applied | %.0f kg debris | collision/friction can resist motion",body.mass) end
      end
      Hud.set_text("status",message)
      self.pending_impact=nil
    elseif state.status=="unattached" or self.impact_wait>2 then
      Hud.set_text("status",state.status=="unattached" and "Impact target no longer exists" or "Impact still pending — not confirmed applied")
      self.pending_impact=nil
    end
  end
  Hud.set_text("ammo","Rockets: "..self.weapon.ammo.." | Active: "..#self.weapon.rockets)
end

function Player:on_fixed_update(dt)
  if not self.weapon then return end
  Character.set_velocity(self.entity_id,self.move[1],0,self.move[3])
  local ray=self:ray()
  if ray then self.weapon:update(dt,ray.origin,ray.direction) end
  local phase=self.weapon:hammer_phase()
  local key=phase..":"..self.weapon.shots..":"..self.weapon.detonations..":"..self.weapon.contacts..":"..#self.weapon.rockets..":"..self.weapon.ammo
  if key~=self.stats_key then
    self.stats_key=key
    Entity.set_field(self.entity_id,"GameplayData","values",{
      phase=phase, shots=self.weapon.shots, detonations=self.weapon.detonations,
      contacts=self.weapon.contacts, active_rockets=#self.weapon.rockets, ammo=self.weapon.ammo,
    })
  end
end

function Player:on_destroy()
  if self.weapon then self.weapon:dispose() end
  Application.set_mouse_captured(false)
end

return Player
