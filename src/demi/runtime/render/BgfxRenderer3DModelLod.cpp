#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::render {

BgfxRenderer3D::ModelLodSelection BgfxRenderer3D::selectModelLod(
    const MeshRendererComponent &mesh, const AnimationPlayer3DComponent *player,
    Vec3 position, Vec3 camera, bool preserveHigh,
    float shadowDistanceFloor) const {
  auto selected = selectModelLodGeometry3D(
      mesh, position, camera, preserveHigh, player ? 0 : shadowDistanceFloor);
  if (selected.isCulled) {
    return selected;
  }
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
