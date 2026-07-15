#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <raylib.h>

#include <string>
#include <unordered_map>

namespace demi::runtime::renderer3d_detail {

struct Hud3DRenderContext {
  float scaleX = 1.0F;
  float scaleY = 1.0F;
  float textScale = 1.0F;
  const std::unordered_map<std::string, Texture2D> &textures;
  const std::unordered_map<std::string, int> &imageAnimations;
};

void drawHud3DNode(const ui::UiNode &node, const Hud3DRenderContext &context);

} // namespace demi::runtime::renderer3d_detail
