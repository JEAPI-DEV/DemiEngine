#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"

namespace demi::runtime::render {

std::array<float, 16>
composeMeshTransform3D(const WorldTransform3D &transform,
                       const Vec3 meshSize) {
  const Vec3 scale{transform.scale.x * meshSize.x,
                   transform.scale.y * meshSize.y,
                   transform.scale.z * meshSize.z};
  const Vec3 right = transformDirection3D(transform, {scale.x, 0.0F, 0.0F});
  const Vec3 up = transformDirection3D(transform, {0.0F, scale.y, 0.0F});
  const Vec3 forward =
      transformDirection3D(transform, {0.0F, 0.0F, scale.z});
  return {right.x,
          right.y,
          right.z,
          0.0F,
          up.x,
          up.y,
          up.z,
          0.0F,
          forward.x,
          forward.y,
          forward.z,
          0.0F,
          transform.position.x,
          transform.position.y,
          transform.position.z,
          1.0F};
}

} // namespace demi::runtime::render
