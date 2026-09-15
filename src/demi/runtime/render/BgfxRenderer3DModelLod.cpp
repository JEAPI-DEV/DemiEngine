#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::render {

BgfxRenderer3D::ModelLodSelection BgfxRenderer3D::selectModelLod(
    const MeshRendererComponent &mesh, const AnimationPlayer3DComponent *player,
    Vec3 position, Vec3 camera, bool preserveHigh) const {
  const float x = position.x - camera.x, y = position.y - camera.y,
              z = position.z - camera.z;
  const float squaredDistance = x * x + y * y + z * z;
  const auto reached = [&](float distance) {
    return distance > 0 && squaredDistance >= distance * distance;
  };
  if (reached(mesh.cullDistance))
    return {.model = &mesh.model, .isCulled = true};
  ModelLodSelection selected{.model = &mesh.model};
  if (preserveHigh)
    return selected;
  if (!mesh.lowLodModel.empty() && reached(mesh.lowLodDistance))
    selected = {.model = &mesh.lowLodModel, .level = 2};
  else if (!mesh.mediumLodModel.empty() && reached(mesh.mediumLodDistance))
    selected = {.model = &mesh.mediumLodModel, .level = 1};
  if (!player || selected.level == 0)
    return selected;
  // Bone overrides/layers can name bones absent from reduced rigs. Named clips
  // with equal durations are required to preserve the shared playback timeline.
  if (mesh.model.empty() || player->clipName.empty() ||
      !player->boneSegments.empty() || !player->layers.empty() ||
      player->blendWeight != 1)
    return {.model = &mesh.model};
  const auto high = animatedModels_.find(mesh.model);
  const auto low = animatedModels_.find(*selected.model);
  if (high == animatedModels_.end() || low == animatedModels_.end())
    return {.model = &mesh.model};
  const auto a = std::ranges::find(high->second.clips, player->clipName,
                                   &assets::GltfSkinnedModel3D::Clip::name);
  const auto b = std::ranges::find(low->second.clips, player->clipName,
                                   &assets::GltfSkinnedModel3D::Clip::name);
  if (a == high->second.clips.end() || b == low->second.clips.end() ||
      std::abs(a->duration - b->duration) >
          0.001F * std::max(1.0F, a->duration))
    return {.model = &mesh.model};
  return selected;
}

} // namespace demi::runtime::render
