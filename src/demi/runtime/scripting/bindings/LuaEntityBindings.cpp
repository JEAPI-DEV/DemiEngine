#include "demi/runtime/scripting/bindings/LuaEntityBindings.h"

#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaEntityBindingModule::install(LuaScriptHost& host, lua_State* state) const {
  sol::state_view lua(state);

  sol::table entity = lua.create_named_table("Entity");
  entity.set_function("find", [&host](const std::string& idOrName) { return host.findEntityId(idOrName); });
  entity.set_function("create", [&host](const std::string& entityId, const sol::table spec) { return host.createEntity(luaParseEntitySpec(entityId, spec)); });
  entity.set_function("destroy", [&host](const std::string& entityId) { return host.destroyEntity(entityId); });
  entity.set_function("set_sprite_color", [&host](const std::string& entityId, float r, float g, float b, sol::optional<float> a) { return host.setEntitySpriteColor(entityId, Color{r, g, b, a.value_or(1.0F)}); });
  entity.set_function("add_position", [&host](const std::string& entityId, float dx, float dy) { (void)host.addEntityPosition(entityId, dx, dy); });
  entity.set_function("set_position", [&host](const std::string& entityId, float x, float y) { return host.setEntityPosition(entityId, x, y); });
  entity.set_function("get_position", [state, &host](const std::string& entityId) { return luaVec2Result(state, host.entityPosition(entityId)); });

  sol::table transform = lua.create_named_table("Transform");
  transform.set_function("get_position", [state, &host](const std::string& entityId) { return luaVec2Result(state, host.entityPosition(entityId)); });
  transform.set_function("set_position", [&host](const std::string& entityId, float x, float y) { return host.setEntityPosition(entityId, x, y); });
  transform.set_function("add_position", [&host](const std::string& entityId, float dx, float dy) { return host.addEntityPosition(entityId, dx, dy); });
  transform.set_function("get_rotation", [&host](const std::string& entityId) { return host.entityRotation(entityId); });
  transform.set_function("set_rotation", [&host](const std::string& entityId, float rotation) { return host.setEntityRotation(entityId, rotation); });
  transform.set_function("get_scale", [state, &host](const std::string& entityId) { return luaVec2Result(state, host.entityScale(entityId)); });
  transform.set_function("set_scale", [&host](const std::string& entityId, float x, float y) { return host.setEntityScale(entityId, x, y); });

  sol::table transform3d = lua.create_named_table("Transform3D");
  transform3d.set_function("get_position", [state, &host](const std::string& entityId) { return luaVec3Result(state, host.entityPosition3D(entityId)); });
  transform3d.set_function("set_position", [&host](const std::string& entityId, float x, float y, float z) { return host.setEntityPosition3D(entityId, x, y, z); });
  transform3d.set_function("add_position", [&host](const std::string& entityId, float dx, float dy, float dz) { return host.addEntityPosition3D(entityId, dx, dy, dz); });
  transform3d.set_function("get_rotation", [state, &host](const std::string& entityId) { return luaVec3Result(state, host.entityRotation3D(entityId)); });
  transform3d.set_function("set_rotation", [&host](const std::string& entityId, float x, float y, float z) { return host.setEntityRotation3D(entityId, x, y, z); });
  transform3d.set_function("get_scale", [state, &host](const std::string& entityId) { return luaVec3Result(state, host.entityScale3D(entityId)); });
  transform3d.set_function("set_scale", [&host](const std::string& entityId, float x, float y, float z) { return host.setEntityScale3D(entityId, x, y, z); });

  sol::table rigidbody = lua.create_named_table("Rigidbody2D");
  rigidbody.set_function("get_velocity", [state, &host](const std::string& entityId) { return luaVec2Result(state, host.getRigidbodyVelocity(entityId)); });
  rigidbody.set_function("set_velocity", [&host](const std::string& entityId, float x, float y) { return host.setRigidbodyVelocity(entityId, x, y); });
  rigidbody.set_function("set_velocity_x", [&host](const std::string& entityId, float x) { return host.setRigidbodyVelocityX(entityId, x); });
  rigidbody.set_function("set_velocity_y", [&host](const std::string& entityId, float y) { return host.setRigidbodyVelocityY(entityId, y); });
  rigidbody.set_function("add_impulse", [&host](const std::string& entityId, float x, float y) { return host.addRigidbodyImpulse(entityId, x, y); });

  sol::table physics = lua.create_named_table("Physics2D");
  physics.set_function("overlap_box", [&host](float x, float y, float width, float height, sol::optional<std::string> ignoredEntityId) { return host.physicsOverlapBox(x, y, width, height, ignoredEntityId.value_or("")); });
  physics.set_function("has_contact", [&host](const std::string& entityId, sol::optional<sol::table> filter) { return host.physicsHasContact(entityId, luaContactFilterFromTable(filter)); });
  physics.set_function("contacts", [state, &host](const std::string& entityId) { return luaContactsTable(state, host.physicsContacts(entityId)); });
}

} // namespace demi::runtime
