#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Camera2DComponent {
  Color clearColor;
  float orthographicSize = 10.0F;
};

struct SpriteComponent {
  std::string texture;
  std::string shape = "rectangle";
  std::string layer;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
};

struct IsoGridComponent {
  Vec2 cellSize = {1.0F, 0.5F};
  int width = 0;
  int height = 0;
};

struct IsoTransformComponent {
  Vec2 tile;
  float height = 0.0F;
  Vec2 footprint = {1.0F, 1.0F};
};

} // namespace demi::runtime
