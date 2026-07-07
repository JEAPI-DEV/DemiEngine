#include "demi/runtime/scripting/bindings/LuaEntityBindings.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <algorithm>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
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

  void addVoxelBlocks(const sol::table& blocks, const sol::table& occupancy, const sol::table& blockTiles, const int atlasColumns, const int occupancyStride) {
    if (atlasColumns <= 0 || occupancyStride <= 0) {
      return;
    }
    const float tileWidth = 1.0F / static_cast<float>(atlasColumns);
    for (const auto& pair : blocks) {
      const sol::table block = pair.second.as<sol::table>();
      const int x = block.get_or("x", 0);
      const int y = block.get_or("y", 0);
      const int z = block.get_or("z", 0);
      const int blockId = block.get_or("block", 0);
      const sol::object tileObject = blockTiles[blockId];
      if (!tileObject.valid() || tileObject.get_type() != sol::type::table) {
        continue;
      }
      const sol::table tiles = tileObject.as<sol::table>();
      addVoxelFaceIfVisible(occupancy, tiles, tileWidth, occupancyStride, x, y, z, 1, 0, 0, "side");
      addVoxelFaceIfVisible(occupancy, tiles, tileWidth, occupancyStride, x, y, z, -1, 0, 0, "side");
      addVoxelFaceIfVisible(occupancy, tiles, tileWidth, occupancyStride, x, y, z, 0, 1, 0, "top");
      addVoxelFaceIfVisible(occupancy, tiles, tileWidth, occupancyStride, x, y, z, 0, -1, 0, "bottom");
      addVoxelFaceIfVisible(occupancy, tiles, tileWidth, occupancyStride, x, y, z, 0, 0, 1, "side");
      addVoxelFaceIfVisible(occupancy, tiles, tileWidth, occupancyStride, x, y, z, 0, 0, -1, "side");
    }
  }

  void addVoxelFaceIfVisible(const sol::table& occupancy,
                             const sol::table& tiles,
                             const float tileWidth,
                             const int occupancyStride,
                             const int x,
                             const int y,
                             const int z,
                             const int nx,
                             const int ny,
                             const int nz,
                             const std::string& tileName) {
    const int neighborKey = voxelOccupancyKey(x + nx, y + ny, z + nz, occupancyStride);
    const sol::object occupied = occupancy[neighborKey];
    if (occupied.valid() && occupied != sol::nil && occupied.as<bool>()) {
      return;
    }
    int tile = tiles.get_or(tileName, tiles.get_or("side", 0));
    const float u0 = static_cast<float>(tile) * tileWidth;
    const float u1 = u0 + tileWidth;
    addVoxelFace(x, y, z, nx, ny, nz, u0, u1);
  }

  static int voxelOccupancyKey(const int x, const int y, const int z, const int occupancyStride) {
    return (y * occupancyStride * occupancyStride) + ((z + 1) * occupancyStride) + (x + 1);
  }

  void addVoxelFace(const int x, const int y, const int z, const int nx, const int ny, const int nz, const float u0, const float u1) {
    if (nx == 1) {
      addQuad(1.0F, 0.0F, 0.0F, x + 1.0F, y + 0.0F, z + 1.0F, u0, 1.0F, x + 1.0F, y + 0.0F, z + 0.0F, u1, 1.0F,
              x + 1.0F, y + 1.0F, z + 0.0F, u1, 0.0F, x + 1.0F, y + 1.0F, z + 1.0F, u0, 0.0F);
    } else if (nx == -1) {
      addQuad(-1.0F, 0.0F, 0.0F, x + 0.0F, y + 0.0F, z + 0.0F, u0, 1.0F, x + 0.0F, y + 0.0F, z + 1.0F, u1, 1.0F,
              x + 0.0F, y + 1.0F, z + 1.0F, u1, 0.0F, x + 0.0F, y + 1.0F, z + 0.0F, u0, 0.0F);
    } else if (ny == 1) {
      addQuad(0.0F, 1.0F, 0.0F, x + 0.0F, y + 1.0F, z + 1.0F, u0, 1.0F, x + 1.0F, y + 1.0F, z + 1.0F, u1, 1.0F,
              x + 1.0F, y + 1.0F, z + 0.0F, u1, 0.0F, x + 0.0F, y + 1.0F, z + 0.0F, u0, 0.0F);
    } else if (ny == -1) {
      addQuad(0.0F, -1.0F, 0.0F, x + 0.0F, y + 0.0F, z + 0.0F, u0, 1.0F, x + 1.0F, y + 0.0F, z + 0.0F, u1, 1.0F,
              x + 1.0F, y + 0.0F, z + 1.0F, u1, 0.0F, x + 0.0F, y + 0.0F, z + 1.0F, u0, 0.0F);
    } else if (nz == 1) {
      addQuad(0.0F, 0.0F, 1.0F, x + 0.0F, y + 0.0F, z + 1.0F, u0, 1.0F, x + 1.0F, y + 0.0F, z + 1.0F, u1, 1.0F,
              x + 1.0F, y + 1.0F, z + 1.0F, u1, 0.0F, x + 0.0F, y + 1.0F, z + 1.0F, u0, 0.0F);
    } else if (nz == -1) {
      addQuad(0.0F, 0.0F, -1.0F, x + 1.0F, y + 0.0F, z + 0.0F, u0, 1.0F, x + 0.0F, y + 0.0F, z + 0.0F, u1, 1.0F,
              x + 0.0F, y + 1.0F, z + 0.0F, u1, 0.0F, x + 1.0F, y + 1.0F, z + 0.0F, u0, 0.0F);
    }
  }
};

struct LuaVoxelWorld {
  int chunkSize = 16;
  int sectionHeight = 16;
  std::unordered_map<std::string, std::vector<int>> sections;

  LuaVoxelWorld(const int chunkSizeValue, const int sectionHeightValue)
      : chunkSize(std::max(chunkSizeValue, 1)), sectionHeight(std::max(sectionHeightValue, 1)) {}

  [[nodiscard]] std::string sectionKey(const int cx, const int sy, const int cz) const {
    return std::to_string(cx) + ":" + std::to_string(sy) + ":" + std::to_string(cz);
  }

  [[nodiscard]] int sectionIndex(const int x, const int y, const int z) const {
    return (y * chunkSize * chunkSize) + (z * chunkSize) + x;
  }

  void clear() {
    sections.clear();
  }

  void setSection(const int cx, const int sy, const int cz, const sol::table& blocks) {
    std::vector<int> section(static_cast<std::size_t>(chunkSize * sectionHeight * chunkSize), 0);
    for (const auto& pair : blocks) {
      const sol::table block = pair.second.as<sol::table>();
      const int x = block.get_or("x", 0);
      const int y = block.get_or("y", 0);
      const int z = block.get_or("z", 0);
      if (x < 0 || x >= chunkSize || y < 0 || y >= sectionHeight || z < 0 || z >= chunkSize) {
        continue;
      }
      section[static_cast<std::size_t>(sectionIndex(x, y, z))] = block.get_or("block", 0);
    }
    sections[sectionKey(cx, sy, cz)] = std::move(section);
  }

  void eraseSection(const int cx, const int sy, const int cz) {
    sections.erase(sectionKey(cx, sy, cz));
  }

  [[nodiscard]] int blockAt(const int cx, const int sy, const int cz, int x, int y, int z) const {
    int sectionCx = cx;
    int sectionSy = sy;
    int sectionCz = cz;
    while (x < 0) {
      x += chunkSize;
      --sectionCx;
    }
    while (x >= chunkSize) {
      x -= chunkSize;
      ++sectionCx;
    }
    while (z < 0) {
      z += chunkSize;
      --sectionCz;
    }
    while (z >= chunkSize) {
      z -= chunkSize;
      ++sectionCz;
    }
    while (y < 0) {
      y += sectionHeight;
      --sectionSy;
    }
    while (y >= sectionHeight) {
      y -= sectionHeight;
      ++sectionSy;
    }
    const auto it = sections.find(sectionKey(sectionCx, sectionSy, sectionCz));
    if (it == sections.end()) {
      return 0;
    }
    return it->second[static_cast<std::size_t>(sectionIndex(x, y, z))];
  }

  [[nodiscard]] LuaProceduralMeshBuilder buildSectionMesh(const int cx, const int sy, const int cz, const sol::table& blockTiles, const int atlasColumns) const {
    LuaProceduralMeshBuilder builder;
    const auto it = sections.find(sectionKey(cx, sy, cz));
    if (it == sections.end() || atlasColumns <= 0) {
      return builder;
    }
    const float tileWidth = 1.0F / static_cast<float>(atlasColumns);
    builder.reserve(4096);
    const std::vector<int>& section = it->second;
    constexpr int directions[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    constexpr const char* tileNames[6] = {"side", "side", "top", "bottom", "side", "side"};
    for (int z = 0; z < chunkSize; ++z) {
      for (int y = 0; y < sectionHeight; ++y) {
        for (int x = 0; x < chunkSize; ++x) {
          const int block = section[static_cast<std::size_t>(sectionIndex(x, y, z))];
          if (block == 0) {
            continue;
          }
          const sol::object tileObject = blockTiles[block];
          if (!tileObject.valid() || tileObject.get_type() != sol::type::table) {
            continue;
          }
          const sol::table tiles = tileObject.as<sol::table>();
          for (int face = 0; face < 6; ++face) {
            const int nx = directions[face][0];
            const int ny = directions[face][1];
            const int nz = directions[face][2];
            if (blockAt(cx, sy, cz, x + nx, y + ny, z + nz) != 0) {
              continue;
            }
            const int tile = tiles.get_or(tileNames[face], tiles.get_or("side", 0));
            const float u0 = static_cast<float>(tile) * tileWidth;
            builder.addVoxelFace(x, y, z, nx, ny, nz, u0, u0 + tileWidth);
          }
        }
      }
    }
    return builder;
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
      &LuaProceduralMeshBuilder::addQuad,
      "add_voxel_blocks",
      &LuaProceduralMeshBuilder::addVoxelBlocks);

  lua.new_usertype<LuaVoxelWorld>("VoxelWorldHandle",
                                  "clear",
                                  &LuaVoxelWorld::clear,
                                  "set_section",
                                  &LuaVoxelWorld::setSection,
                                  "erase_section",
                                  &LuaVoxelWorld::eraseSection,
                                  "build_section_mesh",
                                  &LuaVoxelWorld::buildSectionMesh);

  sol::table voxelWorld = lua.create_named_table("VoxelWorld");
  voxelWorld.set_function("create", [](const int chunkSize, const int sectionHeight) { return LuaVoxelWorld{chunkSize, sectionHeight}; });

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
  entity.set_function("destroy_many", [&host](const sol::table entityIds) {
    std::vector<std::string> ids;
    ids.reserve(entityIds.size());
    for (const auto& pair : entityIds) {
      if (pair.second.is<std::string>()) {
        ids.push_back(pair.second.as<std::string>());
      }
    }
    return host.destroyEntities(ids);
  });
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
