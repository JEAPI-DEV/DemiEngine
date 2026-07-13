#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct DistanceJoint2DComponent {
  static constexpr std::string_view typeName = "DistanceJoint2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"other_entity", ComponentFieldType::String},
      ComponentFieldDescriptor{"anchor", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"other_anchor", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"length", ComponentFieldType::Number},
      ComponentFieldDescriptor{"stiffness", ComponentFieldType::Number},
      ComponentFieldDescriptor{"damping", ComponentFieldType::Number},
      ComponentFieldDescriptor{"collide_connected",
                               ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Physics 2D",
                                                  "Distance Joint 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string otherEntity;
  Vec2 anchor;
  Vec2 otherAnchor;
  float length = 1.0F;
  float stiffness = 0.0F;
  float damping = 0.0F;
  bool collideConnected = false;
};

} // namespace demi::runtime
