#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

enum class VoxelFace : std::size_t {
  Right = 0,
  Left = 1,
  Top = 2,
  Bottom = 3,
  Front = 4,
  Back = 5,
};

struct VoxelAtlasSource {
  std::filesystem::path path;
  float tintR = 1.0F;
  float tintG = 1.0F;
  float tintB = 1.0F;
  float tintA = 1.0F;
};

struct VoxelBlockDefinition {
  std::uint16_t id = 0;
  std::string name;
  bool solid = false;
  std::array<int, 6> tiles{};
};

struct VoxelBlockSet {
  std::string id;
  int tileSize = 16;
  std::vector<VoxelAtlasSource> atlasSources;
  std::vector<VoxelBlockDefinition> blocks;
  std::unordered_map<std::string, std::uint16_t> idByName;
  std::unordered_map<std::uint16_t, std::size_t> indexById;

  [[nodiscard]] const VoxelBlockDefinition* blockById(std::uint16_t id) const;
  [[nodiscard]] std::uint16_t blockIdByName(const std::string& name) const;
  [[nodiscard]] bool isSolid(std::uint16_t id) const;
  [[nodiscard]] int tileForFace(std::uint16_t id, VoxelFace face) const;
};

[[nodiscard]] std::optional<VoxelBlockSet> loadVoxelBlockSet(const std::filesystem::path& path, const std::string& assetId, std::string& error);

} // namespace demi::runtime
