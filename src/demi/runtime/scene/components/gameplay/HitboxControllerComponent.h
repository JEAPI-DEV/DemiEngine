#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>
namespace demi::runtime {
struct HitboxControllerComponent {
  static constexpr std::string_view typeName = "HitboxController";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"hurtbox", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"script", ComponentFieldType::String}};
  static constexpr ComponentEditorMetadata editor{"Gameplay",
                                                  "Hitbox Controller"};
  static void parse(const nlohmann::json &json, Entity &entity);
  Vec2 hurtbox = {1.0F, 1.0F};
  std::string script;
};
} // namespace demi::runtime
