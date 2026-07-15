#include "demi/runtime/render/hud3d/Hud3DNodeRenderers.h"

#include "demi/runtime/render/Renderer3DInternal.h"

namespace demi::runtime::renderer3d_detail {

bool tryDrawHud3DImage(const ui::UiNode &node,
                       const Hud3DRenderContext &context) {
  if (node.type != "image")
    return false;
  if (node.texture.empty())
    return true;

  std::string textureId = node.texture;
  if (!node.animation.empty()) {
    const auto animation = context.imageAnimations.find(node.animation);
    if (animation == context.imageAnimations.end() || animation->second <= 0)
      return true;
    textureId = node.animation + "#" +
                std::to_string(node.animationFrame % animation->second);
  }
  const auto texture = context.textures.find(textureId);
  if (texture == context.textures.end())
    return true;

  const float sourceWidth = node.sourceSize.x != 0.0F
                                ? node.sourceSize.x
                                : static_cast<float>(texture->second.width);
  const float sourceHeight = node.sourceSize.y != 0.0F
                                 ? node.sourceSize.y
                                 : static_cast<float>(texture->second.height);
  const ::Rectangle source{
      .x = node.sourcePosition.x,
      .y = node.sourcePosition.y,
      .width = sourceWidth,
      .height = sourceHeight,
  };
  const ::Rectangle destination{
      .x = node.resolved.x * context.scaleX,
      .y = node.resolved.y * context.scaleY,
      .width = node.resolved.width * context.scaleX,
      .height = node.resolved.height * context.scaleY,
  };
  DrawTexturePro(texture->second, source, destination, {0.0F, 0.0F}, 0.0F,
                 toRlColor(node.color));
  return true;
}

} // namespace demi::runtime::renderer3d_detail
