#include "demi/runtime/voxel/VoxelBlockSet.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace demi::runtime {

namespace {

using Json = nlohmann::json;

int tileField(const Json& tiles, const char* key, const int fallback) {
  const auto iter = tiles.find(key);
  return iter != tiles.end() && iter->is_number_integer() ? iter->get<int>() : fallback;
}

VoxelAtlasSource atlasSourceFromJson(const std::filesystem::path& basePath, const Json& source) {
  VoxelAtlasSource atlasSource;
  if (source.is_string()) {
    atlasSource.path = basePath / source.get<std::string>();
    return atlasSource;
  }

  if (!source.is_object()) {
    return atlasSource;
  }

  atlasSource.path = basePath / source.value("path", std::string{});
  if (const auto tint = source.find("tint"); tint != source.end() && tint->is_array() && tint->size() >= 3) {
    atlasSource.tintR = (*tint)[0].get<float>();
    atlasSource.tintG = (*tint)[1].get<float>();
    atlasSource.tintB = (*tint)[2].get<float>();
    atlasSource.tintA = tint->size() >= 4 ? (*tint)[3].get<float>() : 1.0F;
  }
  return atlasSource;
}

} // namespace

const VoxelBlockDefinition* VoxelBlockSet::blockById(const std::uint16_t id) const {
  const auto found = indexById.find(id);
  return found == indexById.end() ? nullptr : &blocks[found->second];
}

std::uint16_t VoxelBlockSet::blockIdByName(const std::string& name) const {
  const auto found = idByName.find(name);
  return found == idByName.end() ? 0 : found->second;
}

bool VoxelBlockSet::isSolid(const std::uint16_t id) const {
  const VoxelBlockDefinition* block = blockById(id);
  return block != nullptr && block->solid;
}

int VoxelBlockSet::tileForFace(const std::uint16_t id, const VoxelFace face) const {
  const VoxelBlockDefinition* block = blockById(id);
  return block == nullptr ? 0 : block->tiles[static_cast<std::size_t>(face)];
}

std::optional<VoxelBlockSet> loadVoxelBlockSet(const std::filesystem::path& path, const std::string& assetId, std::string& error) {
  std::ifstream input(path);
  if (!input) {
    error = "Failed to read voxel block set: " + path.string();
    return std::nullopt;
  }

  Json document;
  try {
    document = Json::parse(input);
  } catch (const Json::parse_error& parseError) {
    error = "Failed to parse voxel block set " + path.string() + ": " + parseError.what();
    return std::nullopt;
  }

  VoxelBlockSet blockSet;
  blockSet.id = assetId;
  if (const auto tileSize = document.find("tile_size"); tileSize != document.end() && tileSize->is_number_integer()) {
    blockSet.tileSize = tileSize->get<int>();
  }
  if (const auto atlasSources = document.find("atlas_sources"); atlasSources != document.end() && atlasSources->is_array()) {
    for (const Json& source : *atlasSources) {
      VoxelAtlasSource atlasSource = atlasSourceFromJson(path.parent_path(), source);
      if (!atlasSource.path.empty()) {
        blockSet.atlasSources.push_back(std::move(atlasSource));
      }
    }
  }

  const auto blocks = document.find("blocks");
  if (blocks == document.end() || !blocks->is_array()) {
    error = "Voxel block set is missing a blocks array: " + path.string();
    return std::nullopt;
  }

  for (const Json& blockJson : *blocks) {
    if (!blockJson.is_object()) {
      continue;
    }
    VoxelBlockDefinition block;
    block.id = static_cast<std::uint16_t>(blockJson.value("id", 0));
    block.name = blockJson.value("name", std::string{});
    block.solid = blockJson.value("solid", block.id != 0);
    block.tiles.fill(0);
    if (const auto tile = blockJson.find("tile"); tile != blockJson.end() && tile->is_number_integer()) {
      block.tiles.fill(tile->get<int>());
    }
    if (const auto tiles = blockJson.find("tiles"); tiles != blockJson.end() && tiles->is_object()) {
      const int side = tileField(*tiles, "side", block.tiles[0]);
      block.tiles.fill(side);
      block.tiles[static_cast<std::size_t>(VoxelFace::Right)] = tileField(*tiles, "right", side);
      block.tiles[static_cast<std::size_t>(VoxelFace::Left)] = tileField(*tiles, "left", side);
      block.tiles[static_cast<std::size_t>(VoxelFace::Top)] = tileField(*tiles, "top", side);
      block.tiles[static_cast<std::size_t>(VoxelFace::Bottom)] = tileField(*tiles, "bottom", side);
      block.tiles[static_cast<std::size_t>(VoxelFace::Front)] = tileField(*tiles, "front", side);
      block.tiles[static_cast<std::size_t>(VoxelFace::Back)] = tileField(*tiles, "back", side);
    }

    if (block.name.empty()) {
      block.name = "block_" + std::to_string(block.id);
    }
    blockSet.indexById[block.id] = blockSet.blocks.size();
    blockSet.idByName[block.name] = block.id;
    blockSet.blocks.push_back(std::move(block));
  }

  if (blockSet.blockById(0) == nullptr) {
    blockSet.indexById[0] = blockSet.blocks.size();
    blockSet.idByName["air"] = 0;
    blockSet.blocks.push_back(VoxelBlockDefinition{.id = 0, .name = "air", .solid = false});
  }

  return blockSet;
}

} // namespace demi::runtime
