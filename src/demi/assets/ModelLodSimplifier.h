#pragma once

#include "demi/assets/ModelLodGltfWriter.h"

namespace demi::assets {

struct ModelLodSimplification {
  std::vector<gltf::ModelLodPrimitive> primitives;
  std::size_t sourceTriangles = 0;
  std::size_t triangles = 0;
  float achievedError = 0;
};

[[nodiscard]] bool simplifyStaticModelLod(const cgltf_data &document,
                                          float ratio, float maxError,
                                          ModelLodSimplification &output,
                                          std::string &error);

} // namespace demi::assets
