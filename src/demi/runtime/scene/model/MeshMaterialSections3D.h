#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace demi::runtime {

struct MeshMaterialSection3D {
  std::string id;
  std::string material;
  std::uint32_t firstIndex = 0;
  std::uint32_t indexCount = 0;
  bool operator==(const MeshMaterialSection3D &) const = default;
};

// Derived draw data for a native inline mesh. Indices reference its existing
// vertices; material changes do not replace vertex or collision geometry.
// This is runtime publication data, not an additional authored component.
struct MeshMaterialSections3D {
  std::shared_ptr<const std::vector<std::uint32_t>> indices;
  std::vector<MeshMaterialSection3D> sections;
  std::uint64_t indexRevision = 0;
  bool operator==(const MeshMaterialSections3D &) const = default;
};

} // namespace demi::runtime
