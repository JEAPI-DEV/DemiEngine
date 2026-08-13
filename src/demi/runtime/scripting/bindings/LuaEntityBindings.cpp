#include "demi/runtime/scripting/bindings/LuaEntityBindings.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

  void addVertex(const float x, const float y, const float z, const float nx,
                 const float ny, const float nz, const float u, const float v) {
    vertices.push_back(Vec3{x, y, z});
    normals.push_back(Vec3{nx, ny, nz});
    uvs.push_back(Vec2{u, v});
  }

  void addQuad(const float nx, const float ny, const float nz, const float x1,
               const float y1, const float z1, const float u1, const float v1,
               const float x2, const float y2, const float z2, const float u2,
               const float v2, const float x3, const float y3, const float z3,
               const float u3, const float v3, const float x4, const float y4,
               const float z4, const float u4, const float v4) {
    addVertex(x1, y1, z1, nx, ny, nz, u1, v1);
    addVertex(x2, y2, z2, nx, ny, nz, u2, v2);
    addVertex(x3, y3, z3, nx, ny, nz, u3, v3);
    addVertex(x1, y1, z1, nx, ny, nz, u1, v1);
    addVertex(x3, y3, z3, nx, ny, nz, u3, v3);
    addVertex(x4, y4, z4, nx, ny, nz, u4, v4);
  }

  void addVoxelBlocks(const sol::table &blocks, const sol::table &occupancy,
                      const sol::table &blockTiles, const int atlasColumns,
                      const int occupancyStride) {
    if (atlasColumns <= 0 || occupancyStride <= 0) {
      return;
    }
    const float tileWidth = 1.0F / static_cast<float>(atlasColumns);
    std::unordered_set<int> occupiedCells;
    occupiedCells.reserve(occupancy.size());
    for (const auto &pair : occupancy) {
      if (pair.first.is<int>() && pair.second.is<bool>() &&
          pair.second.as<bool>())
        occupiedCells.insert(pair.first.as<int>());
    }

    struct VoxelTiles {
      int side = 0;
      int top = 0;
      int bottom = 0;
    };
    std::unordered_map<int, VoxelTiles> tilesByBlock;
    tilesByBlock.reserve(blockTiles.size());
    for (const auto &pair : blockTiles) {
      if (!pair.first.is<int>() || !pair.second.is<sol::table>())
        continue;
      const sol::table tiles = pair.second.as<sol::table>();
      const int side = tiles.get_or("side", 0);
      tilesByBlock.emplace(
          pair.first.as<int>(),
          VoxelTiles{.side = side,
                     .top = tiles.get_or("top", side),
                     .bottom = tiles.get_or("bottom", side)});
    }

    constexpr std::array<std::array<int, 3>, 6> directions{{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    }};
    for (const auto &pair : blocks) {
      if (!pair.second.is<sol::table>())
        continue;
      const sol::table block = pair.second.as<sol::table>();
      const int x = block.get_or("x", 0);
      const int y = block.get_or("y", 0);
      const int z = block.get_or("z", 0);
      const int blockId = block.get_or("block", 0);
      const auto tiles = tilesByBlock.find(blockId);
      if (tiles == tilesByBlock.end())
        continue;
      for (std::size_t face = 0; face < directions.size(); ++face) {
        const auto &[nx, ny, nz] = directions[face];
        const int neighborKey =
            voxelOccupancyKey(x + nx, y + ny, z + nz, occupancyStride);
        if (occupiedCells.contains(neighborKey))
          continue;
        const int tile = face == 2   ? tiles->second.top
                         : face == 3 ? tiles->second.bottom
                                     : tiles->second.side;
        const float u0 = static_cast<float>(tile) * tileWidth;
        addVoxelFace(x, y, z, nx, ny, nz, u0, u0 + tileWidth);
      }
    }
  }

  // Bulk heightfield meshing avoids a Lua/C++ call for every emitted face.
  // The input includes a one-column border, allowing side visibility to be
  // decided without callbacks into mutable script state.
  void addVoxelHeightfield(const sol::table &heights,
                           const sol::table &topBlocks,
                           const sol::table &fillBlocks,
                           const sol::table &baseBlocks,
                           const sol::table &rockyColumns,
                           const sol::table &blockTiles,
                           const int atlasColumns, const int chunkSize,
                           const int sectionMinimumY, const int sectionHeight,
                           const int fillDepth) {
    if (atlasColumns <= 0 || chunkSize <= 0 || sectionHeight <= 0)
      return;
    const int stride = chunkSize + 2;
    const float tileWidth = 1.0F / static_cast<float>(atlasColumns);
    struct VoxelTiles {
      int side = 0;
      int top = 0;
      int bottom = 0;
    };
    std::unordered_map<int, VoxelTiles> tilesByBlock;
    tilesByBlock.reserve(blockTiles.size());
    for (const auto &pair : blockTiles) {
      if (!pair.first.is<int>() || !pair.second.is<sol::table>())
        continue;
      const sol::table tiles = pair.second.as<sol::table>();
      const int side = tiles.get_or("side", 0);
      tilesByBlock.emplace(pair.first.as<int>(),
                           VoxelTiles{.side = side,
                                      .top = tiles.get_or("top", side),
                                      .bottom = tiles.get_or("bottom", side)});
    }
    const auto index = [stride](const int x, const int z) {
      return ((z + 1) * stride) + x + 2;
    };
    const auto heightAt = [&](const int x, const int z) {
      return heights.get_or(index(x, z), -1);
    };
    const auto blockAtHeight = [&](const int fieldIndex, const int y,
                                   const int height) {
      const int top = topBlocks.get_or(fieldIndex, 0);
      const int base = baseBlocks.get_or(fieldIndex, top);
      if (y == height)
        return top;
      if (y == height - 1 && rockyColumns.get_or(fieldIndex, false))
        return base;
      if (y >= height - fillDepth)
        return fillBlocks.get_or(fieldIndex, base);
      return base;
    };
    const int sectionMaximumY = sectionMinimumY + sectionHeight - 1;
    constexpr std::array<std::array<int, 2>, 4> SideDirections{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    }};
    constexpr std::array<std::array<int, 3>, 4> SideNormals{{
        {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1},
    }};
    for (int z = 0; z < chunkSize; ++z) {
      for (int x = 0; x < chunkSize; ++x) {
        const int fieldIndex = index(x, z);
        const int height = heightAt(x, z);
        if (height >= sectionMinimumY && height <= sectionMaximumY) {
          const int block = blockAtHeight(fieldIndex, height, height);
          const auto tiles = tilesByBlock.find(block);
          if (tiles != tilesByBlock.end()) {
            const float u0 = static_cast<float>(tiles->second.top) * tileWidth;
            addVoxelFace(x, height - sectionMinimumY, z, 0, 1, 0, u0,
                         u0 + tileWidth);
          }
        }
        for (std::size_t side = 0; side < SideDirections.size(); ++side) {
          const int neighborHeight =
              heightAt(x + SideDirections[side][0],
                       z + SideDirections[side][1]);
          const int minimumY = std::max(neighborHeight + 1, sectionMinimumY);
          const int maximumY = std::min(height, sectionMaximumY);
          for (int y = minimumY; y <= maximumY; ++y) {
            const int block = blockAtHeight(fieldIndex, y, height);
            const auto tiles = tilesByBlock.find(block);
            if (tiles == tilesByBlock.end())
              continue;
            const float u0 =
                static_cast<float>(tiles->second.side) * tileWidth;
            addVoxelFace(x, y - sectionMinimumY, z,
                         SideNormals[side][0], SideNormals[side][1],
                         SideNormals[side][2], u0, u0 + tileWidth);
          }
        }
      }
    }
  }

  static int voxelOccupancyKey(const int x, const int y, const int z,
                               const int occupancyStride) {
    return (y * occupancyStride * occupancyStride) +
           ((z + 1) * occupancyStride) + (x + 1);
  }

  void addVoxelFace(const int x, const int y, const int z, const int nx,
                    const int ny, const int nz, const float u0,
                    const float u1) {
    if (nx == 1) {
      addQuad(1.0F, 0.0F, 0.0F, x + 1.0F, y + 0.0F, z + 1.0F, u0, 1.0F,
              x + 1.0F, y + 0.0F, z + 0.0F, u1, 1.0F, x + 1.0F, y + 1.0F,
              z + 0.0F, u1, 0.0F, x + 1.0F, y + 1.0F, z + 1.0F, u0, 0.0F);
    } else if (nx == -1) {
      addQuad(-1.0F, 0.0F, 0.0F, x + 0.0F, y + 0.0F, z + 0.0F, u0, 1.0F,
              x + 0.0F, y + 0.0F, z + 1.0F, u1, 1.0F, x + 0.0F, y + 1.0F,
              z + 1.0F, u1, 0.0F, x + 0.0F, y + 1.0F, z + 0.0F, u0, 0.0F);
    } else if (ny == 1) {
      addQuad(0.0F, 1.0F, 0.0F, x + 0.0F, y + 1.0F, z + 1.0F, u0, 1.0F,
              x + 1.0F, y + 1.0F, z + 1.0F, u1, 1.0F, x + 1.0F, y + 1.0F,
              z + 0.0F, u1, 0.0F, x + 0.0F, y + 1.0F, z + 0.0F, u0, 0.0F);
    } else if (ny == -1) {
      addQuad(0.0F, -1.0F, 0.0F, x + 0.0F, y + 0.0F, z + 0.0F, u0, 1.0F,
              x + 1.0F, y + 0.0F, z + 0.0F, u1, 1.0F, x + 1.0F, y + 0.0F,
              z + 1.0F, u1, 0.0F, x + 0.0F, y + 0.0F, z + 1.0F, u0, 0.0F);
    } else if (nz == 1) {
      addQuad(0.0F, 0.0F, 1.0F, x + 0.0F, y + 0.0F, z + 1.0F, u0, 1.0F,
              x + 1.0F, y + 0.0F, z + 1.0F, u1, 1.0F, x + 1.0F, y + 1.0F,
              z + 1.0F, u1, 0.0F, x + 0.0F, y + 1.0F, z + 1.0F, u0, 0.0F);
    } else if (nz == -1) {
      addQuad(0.0F, 0.0F, -1.0F, x + 1.0F, y + 0.0F, z + 0.0F, u0, 1.0F,
              x + 0.0F, y + 0.0F, z + 0.0F, u1, 1.0F, x + 0.0F, y + 1.0F,
              z + 0.0F, u1, 0.0F, x + 1.0F, y + 1.0F, z + 0.0F, u0, 0.0F);
    }
  }
};

struct LuaVoxelWorld {
  int chunkSize = 16;
  int sectionHeight = 16;
  std::unordered_map<std::string, std::vector<int>> sections;

  LuaVoxelWorld(const int chunkSizeValue, const int sectionHeightValue)
      : chunkSize(std::max(chunkSizeValue, 1)),
        sectionHeight(std::max(sectionHeightValue, 1)) {}

  [[nodiscard]] std::string sectionKey(const int cx, const int sy,
                                       const int cz) const {
    return std::to_string(cx) + ":" + std::to_string(sy) + ":" +
           std::to_string(cz);
  }

  [[nodiscard]] int sectionIndex(const int x, const int y, const int z) const {
    return (y * chunkSize * chunkSize) + (z * chunkSize) + x;
  }

  void clear() { sections.clear(); }

  void setSection(const int cx, const int sy, const int cz,
                  const sol::table &blocks) {
    std::vector<int> section(
        static_cast<std::size_t>(chunkSize * sectionHeight * chunkSize), 0);
    for (const auto &pair : blocks) {
      const sol::table block = pair.second.as<sol::table>();
      const int x = block.get_or("x", 0);
      const int y = block.get_or("y", 0);
      const int z = block.get_or("z", 0);
      if (x < 0 || x >= chunkSize || y < 0 || y >= sectionHeight || z < 0 ||
          z >= chunkSize) {
        continue;
      }
      section[static_cast<std::size_t>(sectionIndex(x, y, z))] =
          block.get_or("block", 0);
    }
    sections[sectionKey(cx, sy, cz)] = std::move(section);
  }

  void eraseSection(const int cx, const int sy, const int cz) {
    sections.erase(sectionKey(cx, sy, cz));
  }

  [[nodiscard]] int blockAt(const int cx, const int sy, const int cz, int x,
                            int y, int z) const {
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

  [[nodiscard]] LuaProceduralMeshBuilder
  buildSectionMesh(const int cx, const int sy, const int cz,
                   const sol::table &blockTiles, const int atlasColumns) const {
    LuaProceduralMeshBuilder builder;
    const auto it = sections.find(sectionKey(cx, sy, cz));
    if (it == sections.end() || atlasColumns <= 0) {
      return builder;
    }
    const float tileWidth = 1.0F / static_cast<float>(atlasColumns);
    builder.reserve(4096);
    const std::vector<int> &section = it->second;
    constexpr int directions[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                      {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    constexpr const char *tileNames[6] = {"side",   "side", "top",
                                          "bottom", "side", "side"};
    for (int z = 0; z < chunkSize; ++z) {
      for (int y = 0; y < sectionHeight; ++y) {
        for (int x = 0; x < chunkSize; ++x) {
          const int block =
              section[static_cast<std::size_t>(sectionIndex(x, y, z))];
          if (block == 0) {
            continue;
          }
          const sol::object tileObject = blockTiles[block];
          if (!tileObject.valid() ||
              tileObject.get_type() != sol::type::table) {
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
            const int tile =
                tiles.get_or(tileNames[face], tiles.get_or("side", 0));
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

void LuaEntityBindingModule::install(LuaScriptHost &host,
                                     lua_State *state) const {
  sol::state_view lua(state);

  sol::table prefab = lua.create_named_table("Prefab");
  prefab.set_function(
      "instantiate",
      [&host](const std::string &prefabId, const sol::table optionsTable) {
        PrefabInstantiateOptions options;
        options.id = optionsTable.get_or("id", std::string{});
        options.pooled = optionsTable.get_or("pooled", false);
        const sol::object position = optionsTable["position"];
        if (position.is<sol::table>()) {
          const sol::table value = position.as<sol::table>();
          options.position = Vec3{.x = value.get_or(1, 0.0F),
                                  .y = value.get_or(2, 0.0F),
                                  .z = value.get_or(3, 0.0F)};
        }
        const sol::object overrides = optionsTable["overrides"];
        if (overrides.is<sol::table>())
          options.overrides = luaObjectToJson(overrides);
        return host.instantiatePrefab(prefabId, options);
      });
  prefab.set_function("release", [&host](const std::string &instanceId) {
    return host.releasePrefab(instanceId);
  });
  prefab.set_function("pooled_count", [&host](const std::string &prefabId) {
    return host.pooledPrefabCount(prefabId);
  });

  lua.new_usertype<LuaProceduralMeshBuilder>(
      "ProceduralMeshBuilder", "clear", &LuaProceduralMeshBuilder::clear,
      "reserve", &LuaProceduralMeshBuilder::reserve, "vertex_count",
      &LuaProceduralMeshBuilder::vertexCount, "add_vertex",
      &LuaProceduralMeshBuilder::addVertex, "add_quad",
      &LuaProceduralMeshBuilder::addQuad, "add_voxel_blocks",
      &LuaProceduralMeshBuilder::addVoxelBlocks, "add_voxel_heightfield",
      &LuaProceduralMeshBuilder::addVoxelHeightfield);

  lua.new_usertype<LuaVoxelWorld>(
      "VoxelWorldHandle", "clear", &LuaVoxelWorld::clear, "set_section",
      &LuaVoxelWorld::setSection, "erase_section", &LuaVoxelWorld::eraseSection,
      "build_section_mesh", &LuaVoxelWorld::buildSectionMesh);

  sol::table voxelWorld = lua.create_named_table("VoxelWorld");
  voxelWorld.set_function("create",
                          [](const int chunkSize, const int sectionHeight) {
                            return LuaVoxelWorld{chunkSize, sectionHeight};
                          });

  sol::table proceduralMesh = lua.create_named_table("ProceduralMesh");
  proceduralMesh.set_function("create", [](sol::optional<int> capacity) {
    LuaProceduralMeshBuilder builder;
    builder.reserve(capacity.value_or(0));
    return builder;
  });
  proceduralMesh.set_function("apply", [&host](
                                           const std::string &entityId,
                                           LuaProceduralMeshBuilder &builder,
                                           sol::optional<sol::object> options) {
    ProfileScope scope("ProceduralMesh.apply");
    RuntimeProfiler::addBytes("ProceduralMesh.apply.copy_to_component",
                              (builder.vertices.size() * sizeof(Vec3)) +
                                  (builder.normals.size() * sizeof(Vec3)) +
                                  (builder.uvs.size() * sizeof(Vec2)));
    std::string texture;
    std::string material;
    std::string renderLayer;
    if (options && options->is<sol::table>()) {
      const sol::table table = options->as<sol::table>();
      texture = table.get_or("texture", std::string{});
      material = table.get_or("material", std::string{});
      renderLayer = table.get_or("render_layer", std::string{});
    } else if (options && options->is<std::string>()) {
      texture = options->as<std::string>();
    }
    return host.setEntityMeshRenderer(entityId, std::move(texture),
                                      std::move(material),
                                      std::move(renderLayer),
                                      builder.vertices, builder.normals,
                                      builder.uvs);
  });

  sol::table entity = lua.create_named_table("Entity");
  entity.set_function("find", [&host](const std::string &idOrName) {
    return host.findEntityId(idOrName);
  });
  entity.set_function(
      "create", [&host](const std::string &entityId, const sol::table spec) {
        nlohmann::json json = luaObjectToJson(spec);
        json["id"] = entityId;
        std::string error;
        std::optional<Entity> created =
            RuntimeObjectModel::buildEntity(json, error);
        if (!created.has_value()) {
          std::cerr << "Entity.create failed for '" << entityId << "': "
                    << error << '\n';
          return false;
        }
        return host.createEntity(std::move(*created));
      });
  entity.set_function(
      "replace", [&host](const std::string &entityId, const sol::table spec) {
        nlohmann::json json = luaObjectToJson(spec);
        json["id"] = entityId;
        std::string error;
        std::optional<Entity> replacement =
            RuntimeObjectModel::buildEntity(json, error);
        return replacement.has_value() &&
               host.replaceEntity(std::move(*replacement));
      });
  entity.set_function("exists", [&host](const std::string &entityId) {
    return host.entityExists(entityId);
  });
  entity.set_function("clone", [&host](const std::string &sourceId,
                                       const std::string &newId) {
    return host.cloneEntity(sourceId, newId);
  });
  entity.set_function("destroy", [&host](const std::string &entityId) {
    return host.destroyEntity(entityId);
  });
  entity.set_function("destroy_many", [&host](const sol::table entityIds) {
    std::vector<std::string> ids;
    ids.reserve(entityIds.size());
    for (const auto &pair : entityIds) {
      if (pair.second.is<std::string>()) {
        ids.push_back(pair.second.as<std::string>());
      }
    }
    return host.destroyEntities(ids);
  });
  entity.set_function("set_enabled", [&host](const std::string &entityId,
                                             const bool enabled) {
    return host.setEntityEnabled(entityId, enabled);
  });
  entity.set_function("is_enabled", [&host](const std::string &entityId) {
    return host.isEntityEnabled(entityId);
  });
  entity.set_function("add_component", [&host](const std::string &entityId,
                                               const std::string &component,
                                               const sol::table values) {
    return host.addEntityComponent(entityId, component,
                                   luaObjectToJson(values));
  });
  entity.set_function("remove_component", [&host](
                                                const std::string &entityId,
                                                const std::string &component) {
    return host.removeEntityComponent(entityId, component);
  });
  entity.set_function("has_component", [&host](const std::string &entityId,
                                               const std::string &component) {
    return host.hasEntityComponent(entityId, component);
  });
  entity.set_function("get", [state, &host](const std::string &entityId,
                                            const std::string &component,
                                            const std::string &field) {
    const auto value =
        host.entityComponentField(entityId, component, field);
    return value.has_value() ? jsonToLuaObject(state, *value)
                             : sol::make_object(state, sol::nil);
  });
  entity.set_function("set", [&host](const std::string &entityId,
                                     const std::string &component,
                                     const std::string &field,
                                     const sol::object value) {
    return host.setEntityComponentField(entityId, component, field,
                                        luaObjectToJson(value));
  });
  entity.set_function("query", [&host](const sol::table queryTable) {
    EntityQuery query;
    const auto readStrings = [](const sol::object &object) {
      std::vector<std::string> values;
      if (!object.is<sol::table>())
        return values;
      for (const auto &entry : object.as<sol::table>()) {
        if (entry.second.is<std::string>())
          values.push_back(entry.second.as<std::string>());
      }
      return values;
    };
    query.allComponents = readStrings(queryTable["all"]);
    query.tags = readStrings(queryTable["tags"]);
    const sol::object layer = queryTable["layer"];
    if (layer.is<std::string>())
      query.layer = layer.as<std::string>();
    query.includeDisabled = queryTable.get_or("include_disabled", false);
    return host.queryEntities(query);
  });
  entity.set_function("set_parent", [&host](
                                           const std::string &entityId,
                                           const sol::object parent) {
    std::optional<std::string> parentId;
    if (parent.valid() && parent != sol::nil && parent.is<std::string>())
      parentId = parent.as<std::string>();
    return host.setEntityParent(entityId, parentId);
  });
  entity.set_function("parent", [&host](const std::string &entityId) {
    return host.entityParent(entityId);
  });
  entity.set_function("children", [&host](const std::string &entityId) {
    return host.entityChildren(entityId);
  });
  entity.set_function("local_position",
                      [state, &host](const std::string &entityId) {
                        const auto value =
                            host.entityLocalPosition(entityId);
                        return value.has_value()
                                   ? jsonToLuaObject(state, *value)
                                   : sol::make_object(state, sol::nil);
                      });
  entity.set_function("world_position",
                      [state, &host](const std::string &entityId) {
                        const auto value =
                            host.entityWorldPosition(entityId);
                        return value.has_value()
                                   ? jsonToLuaObject(state, *value)
                                   : sol::make_object(state, sol::nil);
                      });
}

} // namespace demi::runtime
