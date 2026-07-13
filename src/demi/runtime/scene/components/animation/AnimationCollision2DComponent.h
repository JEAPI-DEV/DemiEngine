#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {

struct AnimationCollisionShape2D {
  Vec2 offset{};
  Vec2 size = {1.0F, 1.0F};
  std::string layer;
};

struct AnimationCollisionWindow2D : AnimationCollisionShape2D {
  float start = 0.0F;
  float end = 0.1F;
  std::string mask;
};

// Named overlap volumes activated by animation state time. The component has
// no combat policy: layers, event meaning, and responses belong to gameplay.
struct AnimationCollision2DComponent {
  static constexpr std::string_view typeName = "AnimationCollision2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"receivers", ComponentFieldType::Object},
      ComponentFieldDescriptor{"windows", ComponentFieldType::Object}};
  static constexpr ComponentEditorMetadata editor{"Animation",
                                                  "Animation Collision 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::unordered_map<std::string, AnimationCollisionShape2D> receivers;
  std::unordered_map<std::string, AnimationCollisionWindow2D> windows;
  std::string activeWindow;
  std::unordered_set<std::string> reportedOverlaps;
};

} // namespace demi::runtime
