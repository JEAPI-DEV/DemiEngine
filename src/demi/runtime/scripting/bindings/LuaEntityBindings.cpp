#include "demi/runtime/scripting/bindings/LuaEntityBindings.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <algorithm>
#include <sol/sol.hpp>
#include <vector>

namespace demi::runtime {

namespace {

struct LuaProceduralMeshBuilder {
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;

  void clear() {
    vertices.clear();
    normals.clear();
    uvs.clear();
  }

  void reserve(const int vertexCount) {
    const auto count = static_cast<std::size_t>(std::max(vertexCount, 0));
    vertices.reserve(count);
    normals.reserve(count);
    uvs.reserve(count);
  }

  [[nodiscard]] int vertexCount() const {
    return static_cast<int>(vertices.size());
  }

  void addVertex(const float x, const float y, const float z, const float nx, const float ny, const float nz, const float u, const float v) {
    vertices.push_back(Vec3{x, y, z});
    normals.push_back(Vec3{nx, ny, nz});
    uvs.push_back(Vec2{u, v});
  }

  void addQuad(const float nx,
               const float ny,
               const float nz,
               const float x1,
               const float y1,
               const float z1,
               const float u1,
               const float v1,
               const float x2,
               const float y2,
               const float z2,
               const float u2,
               const float v2,
               const float x3,
               const float y3,
               const float z3,
               const float u3,
               const float v3,
               const float x4,
               const float y4,
               const float z4,
               const float u4,
               const float v4) {
    addVertex(x1, y1, z1, nx, ny, nz, u1, v1);
    addVertex(x2, y2, z2, nx, ny, nz, u2, v2);
    addVertex(x3, y3, z3, nx, ny, nz, u3, v3);
    addVertex(x1, y1, z1, nx, ny, nz, u1, v1);
    addVertex(x3, y3, z3, nx, ny, nz, u3, v3);
    addVertex(x4, y4, z4, nx, ny, nz, u4, v4);
  }
};

} // namespace

void LuaEntityBindingModule::install(LuaScriptHost& host, lua_State* state) const {
  sol::state_view lua(state);

  lua.new_usertype<LuaProceduralMeshBuilder>(
      "ProceduralMeshBuilder",
      "clear",
      &LuaProceduralMeshBuilder::clear,
      "reserve",
      &LuaProceduralMeshBuilder::reserve,
      "vertex_count",
      &LuaProceduralMeshBuilder::vertexCount,
      "add_vertex",
      &LuaProceduralMeshBuilder::addVertex,
      "add_quad",
      &LuaProceduralMeshBuilder::addQuad);

  sol::table proceduralMesh = lua.create_named_table("ProceduralMesh");
  proceduralMesh.set_function("create", [](sol::optional<int> capacity) {
    LuaProceduralMeshBuilder builder;
    builder.reserve(capacity.value_or(0));
    return builder;
  });
  proceduralMesh.set_function("apply", [&host](const std::string& entityId, LuaProceduralMeshBuilder& builder, sol::optional<std::string> texture) {
    ProfileScope scope("ProceduralMesh.apply");
    RuntimeProfiler::addBytes("ProceduralMesh.apply.copy_to_component",
                              (builder.vertices.size() * sizeof(Vec3)) + (builder.normals.size() * sizeof(Vec3)) + (builder.uvs.size() * sizeof(Vec2)));
    return host.setEntityMeshRenderer(entityId, texture.value_or(std::string{}), builder.vertices, builder.normals, builder.uvs);
  });

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
