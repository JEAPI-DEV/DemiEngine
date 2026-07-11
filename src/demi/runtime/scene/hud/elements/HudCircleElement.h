#pragma once
#include "demi/runtime/scene/hud/HudElementDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>
namespace demi::runtime {
struct HudCircleElement {
  static constexpr std::string_view typeName = "circle";
  static void parse(const nlohmann::json &json, const std::string &id,
                    World &world);
  std::string id;
  std::string group;
  int layer = 2;
  Vec2 center;
  float radius = 16.0F;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  bool visible = true;
};
} // namespace demi::runtime
