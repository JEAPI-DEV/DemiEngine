#pragma once

#include "demi/assets/GltfDocument.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace demi::assets::gltf {

struct ModelLodAttribute {
  std::string type;
  std::size_t components = 0;
  std::vector<float> values;
};

struct ModelLodPrimitive {
  std::size_t mesh = 0;
  std::size_t primitive = 0;
  std::vector<std::uint32_t> indices;
  std::map<std::string, ModelLodAttribute> attributes;
};

// The document's buffers must be loaded. Output is replaced only on success.
[[nodiscard]] bool
writeModelLodGlb(const cgltf_data &document,
                 const std::filesystem::path &source,
                 const std::vector<ModelLodPrimitive> &primitives,
                 std::vector<std::byte> &output, std::string &error);

} // namespace demi::assets::gltf
