#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Transform3DComponent {
  static constexpr std::string_view typeName = "Transform3D";
  static constexpr bool exposedToLua = true;
  static constexpr bool trackedMutation = true;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor::entityReference("parent"),
      ComponentFieldDescriptor{"position", ComponentFieldType::Vec3, false,
                               true, {}, 0.0, false, true}.withHelp("Local position relative to the parent, or world position for an unparented entity."),
      ComponentFieldDescriptor{"rotation", ComponentFieldType::Vec3, false,
                               true, {}, 0.0, false, true}.withHelp("Local Euler rotation in radians around X, Y and Z. Parent rotation is composed at runtime."),
      ComponentFieldDescriptor{"scale", ComponentFieldType::Vec3, false, true,
                               {}, 0.0, false, true}};
  static constexpr ComponentEditorMetadata editor{"3D", "Transform"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string parent;
  Vec3 position;
  Vec3 rotation;
  Vec3 scale = {1.0F, 1.0F, 1.0F};
  [[nodiscard]] bool operator==(const Transform3DComponent &other) const {
    const auto sameVector = [](Vec3 left, Vec3 right) {
      return left.x == right.x && left.y == right.y && left.z == right.z;
    };
    return parent == other.parent && sameVector(position, other.position) &&
           sameVector(rotation, other.rotation) && sameVector(scale, other.scale);
  }

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
