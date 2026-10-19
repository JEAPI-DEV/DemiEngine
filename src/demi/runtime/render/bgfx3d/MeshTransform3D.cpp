#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"

namespace demi::runtime::render {

std::array<float, 16>
composeMeshTransform3D(const WorldTransform3D &transform,
                       const Vec3 meshSize) {
  auto meshTransform = transform;
  meshTransform.scale = {transform.scale.x * meshSize.x,
                         transform.scale.y * meshSize.y,
                         transform.scale.z * meshSize.z};
  const auto basis = transformBasis3D(meshTransform);
  return {basis.right.x,
          basis.right.y,
          basis.right.z,
          0.0F,
          basis.up.x,
          basis.up.y,
          basis.up.z,
          0.0F,
          basis.forward.x,
          basis.forward.y,
          basis.forward.z,
          0.0F,
          transform.position.x,
          transform.position.y,
          transform.position.z,
          1.0F};
}

} // namespace demi::runtime::render
