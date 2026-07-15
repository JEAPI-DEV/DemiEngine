#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace demi::assets {

struct GltfPoint3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct GltfTriangle {
  GltfPoint3 a;
  GltfPoint3 b;
  GltfPoint3 c;
};

struct GltfGeometry {
  std::vector<GltfTriangle> triangles;
  GltfPoint3 minimum;
  GltfPoint3 maximum;
};

// Decodes the default glTF scene into transformed triangle geometry. Supports
// external binary buffers and POSITION/index accessors used by static meshes.
[[nodiscard]] std::optional<GltfGeometry>
loadGltfGeometry(const std::filesystem::path &path, std::string &error);

} // namespace demi::assets
