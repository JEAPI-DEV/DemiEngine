#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Transform3DComponent {
  static constexpr std::string_view typeName = "Transform3D";
  static constexpr bool exposedToLua = true;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor::entityReference("parent"),
      ComponentFieldDescriptor{"position", ComponentFieldType::Vec3, false,
                               true, {}, 0.0, false, true}.withHelp("Local position relative to the parent, or world position for an unparented entity."),
      ComponentFieldDescriptor{"rotation", ComponentFieldType::Vec3, false,
                               true, {}, 0.0, false, true}.withHelp("Local Euler rotation in radians around X, Y and Z. Parent rotation is composed at runtime."),
      ComponentFieldDescriptor{"scale", ComponentFieldType::Vec3, false, true,
                               {}, 0.0, false, true}};
  static constexpr ComponentEditorMetadata editor{"3D", "Transform 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string parent;
  Vec3 position;
  Vec3 rotation;
  Vec3 scale = {1.0F, 1.0F, 1.0F};
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<Transform3DComponent>::member<
          &Transform3DComponent::parent>("parent"),
      RuntimeFieldBinding<Transform3DComponent>::member<
          &Transform3DComponent::position>("position"),
      RuntimeFieldBinding<Transform3DComponent>::member<
          &Transform3DComponent::rotation>("rotation"),
      RuntimeFieldBinding<Transform3DComponent>::member<
          &Transform3DComponent::scale>("scale")};
};

} // namespace demi::runtime
