#include "demi/runtime/math/VectorMath.h"
#include "demi/runtime/render/bgfx3d/ShadowCascadeLayout3D.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
float projected(const std::array<float, 4> &row, demi::runtime::Vec3 point) {
  return row[0] * point.x + row[1] * point.y + row[2] * point.z + row[3];
}
} // namespace

int main() {
  using namespace demi::runtime;
  using namespace demi::runtime::render;
  try {
    SceneLighting3D lighting;
    BgfxCameraFrame3D camera;
    camera.viewportWidth = 1920;
    camera.viewportHeight = 1080;
    camera.position = {0, 1.1F, 4};
    camera.forward = {0, -0.55F, -4};
    camera.camera.fov = 38;
    std::string error;
    auto layout = makeShadowCascadeLayout3D(lighting, camera, error);
    require(layout.has_value(), error.c_str());
    require(layout->count == 4, "Default should use four cascades");
    require(layout->splits[0] < 4 && layout->splits[3] == 80,
            "Near/detail split or far coverage incorrect");
    require(2 * layout->cascades[0].radius / 1024 < .01F,
            "Near cascade wastes close-up precision");
    for (int index = 0; index < layout->count; ++index) {
      const auto &cascade = layout->cascades[index];
      const auto center =
          math::add(cascade.eye,
                    math::scale(cascade.direction, cascade.depthRange * .5F));
      require(std::abs(projected(cascade.x, center) - .5F) < .0001F,
              "Light-space X mapping");
      require(std::abs(projected(cascade.y, center) - .5F) < .0001F,
              "Light-space Y mapping");
      require(std::abs(projected(cascade.z, center) - .5F) < .0001F,
              "Light-space depth mapping");
      if (index > 0)
        require(cascade.receiverNear < layout->splits[index - 1],
                "Cascade blend coverage does not overlap");
    }
    camera.position.x += .000001F;
    auto moved = makeShadowCascadeLayout3D(lighting, camera, error);
    require(moved.has_value(), error.c_str());
    require(std::abs(moved->cascades[0].x[3] - layout->cascades[0].x[3]) <
                1e-6F,
            "Subtexel movement causes shadow shimmer");
    for (int count : {1, 2, 3, 4}) {
      lighting.shadowCascades = count;
      auto setting = makeShadowCascadeLayout3D(lighting, camera, error);
      require(setting && setting->count == count, "Cascade selection ignored");
    }
    camera.camera.perspective = false;
    lighting.shadowBlend = 1.F;
    auto fullyBlended = makeShadowCascadeLayout3D(lighting, camera, error);
    require(fullyBlended && std::abs(fullyBlended->cascades[1].receiverNear - camera.camera.nearClip) < .0001F,
            "Full cascade blending should be supported");
    auto ortho = makeShadowCascadeLayout3D(lighting, camera, error);
    require(ortho && std::abs(ortho->splits[0] - 20.0375F) < .001F,
            "Orthographic splits must be uniform");
    lighting.shadowDistance = .01F;
    require(makeShadowCascadeLayout3D(lighting, camera, error)->count == 0,
            "Near-clipped coverage should disable pass");
    lighting.shadowDistance = 80;
    lighting.shadowCascades = 5;
    require(!makeShadowCascadeLayout3D(lighting, camera, error),
            "Unsupported shader mode accepted");
    lighting.shadowCascades = 4;
    camera.viewportHeight = 0;
    require(!makeShadowCascadeLayout3D(lighting, camera, error),
            "Empty camera viewport accepted");
    std::cout << "Shadow cascade layout tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
