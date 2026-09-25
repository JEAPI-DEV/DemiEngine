#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include <memory>

#include <string>

namespace demi::runtime {

struct ColliderAsset3D;

struct ModelCollider3DComponent {
  static constexpr std::string_view typeName = "ModelCollider3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor::assetReference("asset"),
      ComponentFieldDescriptor{"inline_geometry", ComponentFieldType::Object},
      ComponentFieldDescriptor{"is_trigger", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D",
                                                  "Collider Asset 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string asset;
  bool isTrigger = false;
  std::string layer;
  std::shared_ptr<const ColliderAsset3D> inlineGeometry{};
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<ModelCollider3DComponent>::member<
          &ModelCollider3DComponent::asset>("asset"),
      RuntimeFieldBinding<ModelCollider3DComponent>::member<
          &ModelCollider3DComponent::inlineGeometry>("inline_geometry"),
      RuntimeFieldBinding<ModelCollider3DComponent>::member<
          &ModelCollider3DComponent::isTrigger>("is_trigger"),
      RuntimeFieldBinding<ModelCollider3DComponent>::member<
          &ModelCollider3DComponent::layer>("layer")};
};

} // namespace demi::runtime
