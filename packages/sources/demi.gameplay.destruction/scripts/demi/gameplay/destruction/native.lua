local Native = {}

function Native.streaming()
  local Prefab=require("demi.prefab")
  local Destruction=require("demi.physics.destruction3d")
  local Entity=require("demi.entity")
  local function root(entry) return entry.id.."/"..(entry.root or "assembly") end
  return {
    spawn=function(entry)
      local options={id=entry.id,position=entry.position}
      if entry.rotation or entry.scale then
        options.overrides={[entry.root or "assembly"]={components={Transform3D={
          rotation=entry.rotation or {0,0,0},scale=entry.scale or {1,1,1}}}}}
      end
      return Prefab.instantiate(entry.prefab,options)~=nil
    end,
    release=function(entry) return Prefab.release(entry.id) or not Entity.exists(root(entry)) end,
    state=function(entry) return Destruction.state(root(entry)) end,
    checkpoint=function(entry) return Destruction.checkpoint(root(entry)) end,
    restore=function(entry,saved) return Destruction.restore(root(entry),saved) end,
    retire=function(entry) return Destruction.retire_debris(root(entry)) end,
  }
end

function Native.new(options)
  assert(options and type(options.rocket_prefab)=="string" and options.rocket_prefab:match("^prefab://"),
    "provide a rocket_prefab reference")
  local Physics = require("demi.physics.query3d")
  local Destruction = require("demi.physics.destruction3d")
  local Prefab = require("demi.prefab")
  local Entity = require("demi.entity")
  local Transform = require("demi.transform3d")
  local Vector = require("demi.math.vector3")
  local root = options.rocket_root or "body"
  local function move(id, position, direction)
    local body = id .. "/" .. root
    if not Entity.exists(body) then return end -- deferred prefab insertion
    Transform.set_position(body, position[1], position[2], position[3])
    local target = Vector.add(position, direction)
    Transform.look_at(body, target[1], target[2], target[3])
  end
  return {
    vector = Vector,
    cast = function(origin, direction, distance, radius)
      local overlaps = Physics.overlap_sphere_all(origin[1], origin[2], origin[3], radius, nil, options.owner)
      for _, hit in ipairs(overlaps) do if not hit.is_trigger then return hit end end
      if distance == 0 then return nil end
      return Physics.sphere_cast(origin[1], origin[2], origin[3], radius,
        direction[1], direction[2], direction[3], distance, nil, options.owner, false)
    end,
    impact = function(hit, target)
      -- Keep the stable assembly identity before a split retires the hit body.
      local before = target and Destruction.state(target)
      local ok, issue, affected = Destruction.impact(hit)
      local receipt = before and before.root~="" and {
        root=before.root, revision=before.revision, target=target,
      } or nil
      return ok, issue, affected, receipt
    end,
    spawn = function(id, position, direction)
      return Prefab.instantiate(options.rocket_prefab, {id = id, position = position}) ~= nil
    end,
    move = move,
    release = function(id) return Prefab.release(id) end,
  }
end

return Native
