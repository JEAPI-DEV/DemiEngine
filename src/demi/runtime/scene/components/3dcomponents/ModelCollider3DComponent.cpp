#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"

#include "demi/assets/ColliderShapeAsset.h"
#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <stdexcept>

namespace demi::runtime {

void ModelCollider3DComponent::parse(const nlohmann::json &json,
                                     Entity &entity) {
  ModelCollider3DComponent component;
  component.asset = scene_loading::stringOr(json, "asset");
  component.isTrigger =
      scene_loading::boolField(json, "is_trigger").value_or(false);
  component.layer = scene_loading::stringOr(json, "layer");
  if (json.contains("inline_geometry")) {
    if (!component.asset.empty())
      throw std::invalid_argument(
          "ModelCollider3D accepts asset or inline_geometry, not both");
    std::string error;
    const auto shape =
        assets::parseColliderShapeAsset(json["inline_geometry"], error);
    if (!shape || shape->parts.empty())
      throw std::invalid_argument(
          "Inline collider requires compound geometry: " + error);
    auto prepared =
        std::make_shared<ColliderAsset3D>(colliderAssetFromShape3D(*shape));
    prepared->revision =
        std::hash<std::string>{}(json["inline_geometry"].dump());
    component.inlineGeometry = std::move(prepared);
  }
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
