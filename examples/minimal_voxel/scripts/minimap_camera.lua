local MinimapCamera = {}

function MinimapCamera:on_update(_dt)
  local x, _, z = Transform3D.get_position("ent_camera")
  if x ~= nil and z ~= nil then
    Transform3D.set_position(self.entity_id, x, 150.0, z)
  end
end

return MinimapCamera
