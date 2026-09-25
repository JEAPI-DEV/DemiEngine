#include "demi/runtime/scripting/bindings/mesh/LuaMeshConstructionBindings.h"

#include "demi/runtime/geometry/VoxelMeshBuilder.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include <algorithm>
#include <array>
#include <limits>
#include <sol/sol.hpp>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime {

namespace {
using geometry::ProceduralMeshBuilder;
using geometry::VoxelMeshWorld;

std::vector<geometry::VoxelBlock> readBlocks(const sol::table &table) {
  std::vector<geometry::VoxelBlock> blocks;
  blocks.reserve(table.size());
  for (const auto &entry : table) {
    if (!entry.second.is<sol::table>()) {
      continue;
    }
    const auto value = entry.second.as<sol::table>();
    blocks.push_back({value.get_or("x", 0), value.get_or("y", 0),
                      value.get_or("z", 0), value.get_or("block", 0)});
  }
  return blocks;
}

geometry::VoxelTileMap readTiles(const sol::table &table) {
  geometry::VoxelTileMap tiles;
  for (const auto &entry : table) {
    if (!entry.first.is<int>() || !entry.second.is<sol::table>()) {
      continue;
    }
    const auto value = entry.second.as<sol::table>();
    const int side = value.get_or("side", 0);
    tiles.emplace(entry.first.as<int>(),
                  geometry::VoxelTiles{side, value.get_or("top", side),
                                       value.get_or("bottom", side)});
  }
  return tiles;
}

void addVoxelBlocks(ProceduralMeshBuilder &builder, const sol::table &blocks,
                    const sol::table &occupancy, const sol::table &tiles,
                    int atlasColumns, int occupancyStride) {
  std::unordered_set<int> occupied;
  for (const auto &entry : occupancy) {
    if (entry.first.is<int>() && entry.second.is<bool>() &&
        entry.second.as<bool>()) {
      occupied.insert(entry.first.as<int>());
    }
  }
  builder.addVoxelBlocks(readBlocks(blocks), occupied, readTiles(tiles),
                         atlasColumns, occupancyStride);
}

void addVoxelHeightfield(ProceduralMeshBuilder &builder,
                         const sol::table &heights, const sol::table &topBlocks,
                         const sol::table &fillBlocks,
                         const sol::table &baseBlocks,
                         const sol::table &rockyColumns,
                         const sol::table &tiles, int atlasColumns,
                         int chunkSize, int sectionMinimumY, int sectionHeight,
                         int fillDepth) {
  if (chunkSize <= 0 || atlasColumns <= 0 || sectionHeight <= 0) {
    return;
  }
  const auto count = geometry::checkedVoxelCellCount(
      static_cast<std::int64_t>(chunkSize) + 2, 1,
      static_cast<std::int64_t>(chunkSize) + 2);
  std::vector<geometry::VoxelColumn> columns(count);
  for (std::size_t index = 0; index < columns.size(); ++index) {
    const int luaIndex = static_cast<int>(index + 1);
    const int top = topBlocks.get_or(luaIndex, 0);
    const int base = baseBlocks.get_or(luaIndex, top);
    columns[index] = {heights.get_or(luaIndex, -1), top,
                      fillBlocks.get_or(luaIndex, base), base,
                      rockyColumns.get_or(luaIndex, false)};
  }
  builder.addVoxelHeightfield(columns, readTiles(tiles), atlasColumns,
                              chunkSize, sectionMinimumY, sectionHeight,
                              fillDepth);
}
} // namespace

void LuaMeshConstructionBindingModule::install(LuaScriptHost &host,
                                               lua_State *state) const {
  sol::state_view lua(state);
  lua.new_usertype<ProceduralMeshBuilder>(
      "ProceduralMeshBuilder", "clear", &ProceduralMeshBuilder::clear,
      "reserve", &ProceduralMeshBuilder::reserve, "vertex_count",
      &ProceduralMeshBuilder::vertexCount, "add_vertex",
      &ProceduralMeshBuilder::addVertex, "add_quad",
      &ProceduralMeshBuilder::addQuad, "add_voxel_blocks", &addVoxelBlocks,
      "add_voxel_heightfield", &addVoxelHeightfield);

  lua.new_usertype<VoxelMeshWorld>(
      "VoxelWorldHandle", "clear", &VoxelMeshWorld::clear, "set_section",
      [](VoxelMeshWorld &world, int x, int y, int z, const sol::table &blocks) {
        world.setSection(x, y, z, readBlocks(blocks));
      },
      "erase_section", &VoxelMeshWorld::eraseSection, "build_section_mesh",
      [](const VoxelMeshWorld &world, int x, int y, int z,
         const sol::table &tiles, int atlasColumns) {
        return world.buildSectionMesh(x, y, z, readTiles(tiles), atlasColumns);
      });

  sol::table voxelWorld = lua.create_named_table("VoxelWorld");
  // Instances retain their registered metatables; construction uses modules.
  lua["ProceduralMeshBuilder"] = sol::nil;
  lua["VoxelWorldHandle"] = sol::nil;
  voxelWorld.set_function("create",
                          [](const int chunkSize, const int sectionHeight) {
                            return VoxelMeshWorld{chunkSize, sectionHeight};
                          });

  sol::table proceduralMesh = lua.create_named_table("ProceduralMesh");
  proceduralMesh.set_function("create", [](sol::optional<int> capacity) {
    ProceduralMeshBuilder builder;
    builder.reserve(capacity.value_or(0));
    return builder;
  });
  proceduralMesh.set_function("apply", [&host](
                                           const std::string &entityId,
                                           ProceduralMeshBuilder &builder,
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
    return host.setEntityMeshRenderer(
        entityId, std::move(texture), std::move(material),
        std::move(renderLayer), builder.vertices, builder.normals, builder.uvs);
  });
}

} // namespace demi::runtime
