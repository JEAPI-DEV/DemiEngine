#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Transform2DComponent {
  static constexpr std::string_view typeName = "Transform2D";
  static constexpr bool exposedToLua = true;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor::entityReference("parent"),
      ComponentFieldDescriptor{"position", ComponentFieldType::Vec2, false,
                               true, {}, 0.0, false, true},
      ComponentFieldDescriptor{"rotation", ComponentFieldType::Number, false,
                               true, {}, 0.0, false, true}.withHelp("Local rotation in radians relative to the parent."),
      ComponentFieldDescriptor{"scale", ComponentFieldType::Vec2, false, true,
                               {}, 0.0, false, true}};
  static constexpr ComponentEditorMetadata editor{"2D", "Transform 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string parent;
  Vec2 position;
  float rotation = 0.0F;
  Vec2 scale = {1.0F, 1.0F};
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<Transform2DComponent>::member<
          &Transform2DComponent::parent>("parent"),
      RuntimeFieldBinding<Transform2DComponent>::member<
          &Transform2DComponent::position>("position"),
      RuntimeFieldBinding<Transform2DComponent>::member<
          &Transform2DComponent::rotation>("rotation"),
      RuntimeFieldBinding<Transform2DComponent>::member<
          &Transform2DComponent::scale>("scale")};
};

} // namespace demi::runtime
