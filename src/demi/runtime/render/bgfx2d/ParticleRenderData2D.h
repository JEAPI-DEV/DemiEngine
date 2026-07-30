#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime::render {

// Immutable presentation data produced by particle simulation. Keeping this
// value type independent of a graphics backend lets raylib and bgfx render the
// same simulation while the migration is staged.
struct ParticleRenderData2D {
  Vec2 position;
  float size = 0.0F;
  float rotationRadians = 0.0F;
  Color color;
  std::string texture;
  std::string material;
  int sortingOrder = 0;
};

} // namespace demi::runtime::render
