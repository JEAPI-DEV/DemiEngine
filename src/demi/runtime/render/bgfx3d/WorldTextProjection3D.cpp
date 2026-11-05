#include "demi/runtime/render/bgfx3d/WorldTextProjection3D.h"
#include "demi/runtime/camera/Camera3DMath.h"
#include "demi/runtime/math/VectorMath.h"
#include "demi/runtime/ui/UiLayoutEngine.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/EntityComponentQuery.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/WorldText3DComponent.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::render {

ui::UiDocument projectWorldText3D(const World &world,
                                  const BgfxCameraFrame3D &frame,
                                  EntityComponentQuery *query) {
  ProfileScope scope("Renderer3D.world_text");
  ui::UiDocument document;
  document.canvasSize = {static_cast<float>(frame.viewportWidth),
                         static_cast<float>(frame.viewportHeight)};
  const auto projection =
      cameraProjection3D(frame.camera, frame.position, frame.forward, frame.up,
                         document.canvasSize);
  if (!projection) {
    return document;
  }
  EntityComponentQuery uncached;
  auto &membership = query ? *query : uncached;
  for (const auto index :
       membership.queryAny<WorldText3DComponent>(world.entities)) {
    const Entity &entity = world.entities[index];
    if (!entity.enabled) {
      continue;
    }
    const auto *text = entity.component<WorldText3DComponent>();
    if (text == nullptr || text->text.empty()) {
      continue;
    }
    if (!frame.camera.renderMask.empty() && !text->renderMask.empty() &&
        frame.camera.renderMask != text->renderMask) {
      continue;
    }
    const auto transform = resolveWorldTransform3D(world, entity);
    if (!transform) {
      continue;
    }
    const Vec3 delta = math::subtract(transform->position, frame.position);
    if (text->maxDistance > 0.0F && math::length(delta) > text->maxDistance) {
      continue;
    }
    const auto point = projection->project(transform->position);
    if (!point || point->depth <= frame.camera.nearClip ||
        point->depth >= frame.camera.farClip) {
      continue;
    }
    if (point->screen.x < -0.1F * frame.viewportWidth ||
        point->screen.x > 1.1F * frame.viewportWidth ||
        point->screen.y < -0.1F * frame.viewportHeight ||
        point->screen.y > 1.1F * frame.viewportHeight) {
      continue;
    }
    const float fontPixels =
        std::clamp(text->fontSize * point->pixelsPerUnit, 8.0F, 256.0F);
    const float width = std::max(
        fontPixels * 0.6F * static_cast<float>(text->text.size()), fontPixels);
    const float x = point->screen.x - width * 0.5F;
    const float y = point->screen.y - fontPixels;
    ui::UiNode node;
    node.id = "world_text:" + entity.id;
    node.type = "label";
    node.text = text->text;
    node.layout.position = {x, y};
    node.layout.size = {width, fontPixels * 1.25F};
    node.color = text->color;
    node.textColor = text->color;
    node.fontSize = fontPixels;
    document.nodes.push_back(std::move(node));
  }
  if (!document.nodes.empty()) {
    ui::UiLayoutEngine{}.ensure(document, document.canvasSize);
  }
  return document;
}

} // namespace demi::runtime::render
