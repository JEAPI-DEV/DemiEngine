#include "demi/runtime/render/bgfx3d/WorldTextProjection3D.h"

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/WorldText3DComponent.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace demi::runtime::render {
namespace {

float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 normalized(const Vec3 value) {
  const float length = std::sqrt(dot(value, value));
  return length > 0.00001F
             ? Vec3{value.x / length, value.y / length, value.z / length}
             : Vec3{};
}

Vec3 cross(const Vec3 left, const Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

} // namespace

ui::UiDocument projectWorldText3D(const World &world,
                                  const BgfxCameraFrame3D &frame) {
  ui::UiDocument document;
  document.canvasSize = {static_cast<float>(frame.viewportWidth),
                         static_cast<float>(frame.viewportHeight)};
  const Vec3 forward = normalized(frame.forward);
  const Vec3 right = normalized(cross(forward, frame.up));
  const Vec3 up = normalized(cross(right, forward));
  const float aspect =
      static_cast<float>(std::max<std::uint16_t>(frame.viewportWidth, 1)) /
      std::max<std::uint16_t>(frame.viewportHeight, 1);
  const float tangent =
      std::tan(frame.camera.fov * std::numbers::pi_v<float> / 360.0F);
  for (const Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    const auto *text = entity.component<WorldText3DComponent>();
    const auto transform = resolveWorldTransform3D(world, entity);
    if (text == nullptr || !transform || text->text.empty())
      continue;
    if (!frame.camera.renderMask.empty() && !text->renderMask.empty() &&
        frame.camera.renderMask != text->renderMask)
      continue;
    const Vec3 delta{transform->position.x - frame.position.x,
                     transform->position.y - frame.position.y,
                     transform->position.z - frame.position.z};
    const float distance = std::sqrt(dot(delta, delta));
    if (text->maxDistance > 0.0F && distance > text->maxDistance)
      continue;
    const float depth = dot(delta, forward);
    if (depth <= frame.camera.nearClip || depth >= frame.camera.farClip)
      continue;
    float ndcX = 0.0F;
    float ndcY = 0.0F;
    float pixelsPerUnit = 1.0F;
    if (frame.camera.perspective) {
      const float vertical = std::max(depth * tangent, 0.0001F);
      ndcX = dot(delta, right) / (vertical * aspect);
      ndcY = dot(delta, up) / vertical;
      pixelsPerUnit = frame.viewportHeight / (vertical * 2.0F);
    } else {
      const float halfHeight = std::max(frame.camera.orthographicSize, 0.0001F);
      ndcX = dot(delta, right) / (halfHeight * aspect);
      ndcY = dot(delta, up) / halfHeight;
      pixelsPerUnit = frame.viewportHeight / (halfHeight * 2.0F);
    }
    if (std::abs(ndcX) > 1.2F || std::abs(ndcY) > 1.2F)
      continue;
    const float fontPixels =
        std::clamp(text->fontSize * pixelsPerUnit, 8.0F, 256.0F);
    const float width = std::max(
        fontPixels * 0.6F * static_cast<float>(text->text.size()), fontPixels);
    const float x = (ndcX * 0.5F + 0.5F) * frame.viewportWidth - width * 0.5F;
    const float y = (0.5F - ndcY * 0.5F) * frame.viewportHeight - fontPixels;
    ui::UiNode node;
    node.id = "world_text:" + entity.id;
    node.type = "label";
    node.text = text->text;
    node.resolved = {x, y, width, fontPixels * 1.25F};
    node.color = text->color;
    node.textColor = text->color;
    node.fontSize = fontPixels;
    document.nodes.push_back(std::move(node));
  }
  return document;
}

} // namespace demi::runtime::render
