#include "demi/runtime/scene/SceneAssetReferences.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/ComponentRegistry.h"

#include <set>

namespace demi::runtime {

std::vector<std::string>
collectSceneAssetReferences(const World &world,
                            const std::string_view sceneOwner) {
  std::set<std::string> references;
  for (const Entity &entity : world.entities) {
    if (!sceneOwner.empty() && entity.sceneOwner != sceneOwner)
      continue;
    // Prefer authored component JSON (the exact authored payload) and fall
    // back to serializedComponents (populated for prefab-instantiated and
    // runtime-created entities). Scene-loaded entities only carry authored
    // components, so without this the implicit scene asset group resolves
    // empty and referenced assets never become resident.
    for (const std::shared_ptr<const Component> &authored :
         entity.authoredComponents) {
      if (authored == nullptr)
        continue;
      try {
        const auto values =
            nlohmann::json::parse(authored->json(), nullptr, false);
        if (!values.is_object())
          continue;
        const auto assets = extractAssetReferences(values.dump());
        references.insert(assets.begin(), assets.end());
      } catch (const nlohmann::json::exception &) {
        continue;
      }
    }
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
