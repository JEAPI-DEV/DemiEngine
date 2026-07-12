#pragma once
#include "demi/runtime/scene/hud/HudElementDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>
namespace demi::runtime {
struct HudTextElement {
  static constexpr std::string_view typeName = "text";
  static void parse(const nlohmann::json &json, const std::string &id,
                    World &world);
  std::string id;
  std::string group;
  int layer = 3;
  std::string text;
  Vec2 position;
  float scale = 3.0F;
  float fontSize = 0.0F;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  bool visible = true;

  void setPosition(Vec2 value) { position = value; }
  void setColor(Color value) { color = value; }
  void setOpacity(float value) { color.a = value; }
};
} // namespace demi::runtime
