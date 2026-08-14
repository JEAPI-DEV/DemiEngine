#include "demi/runtime/scene/SceneAssetReferences.h"

#include "demi/assets/AssetRegistry.h"

#include <set>

namespace demi::runtime {

std::vector<std::string>
collectSceneAssetReferences(const World &world,
                            const std::string_view sceneOwner) {
  std::set<std::string> references;
  for (const Entity &entity : world.entities) {
    if (!sceneOwner.empty() && entity.sceneOwner != sceneOwner)
      continue;
    for (const auto &[unused, serialized] : entity.serializedComponents) {
      (void)unused;
      const auto assets = extractAssetReferences(serialized);
      references.insert(assets.begin(), assets.end());
    }
  }
  for (const ui::UiNode &node : world.ui.nodes) {
    if (!sceneOwner.empty() && node.sceneOwner != sceneOwner)
      continue;
    for (const std::string *reference :
         {&node.font, &node.texture, &node.animation})
      if (reference->starts_with("asset://"))
        references.insert(*reference);
  }
  return {references.begin(), references.end()};
}

} // namespace demi::runtime
