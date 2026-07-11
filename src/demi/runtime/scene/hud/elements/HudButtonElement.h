#pragma once
#include "demi/runtime/scene/hud/HudElementDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>
namespace demi::runtime {
struct HudButtonElement {
  static constexpr std::string_view typeName = "button";
  static void parse(const nlohmann::json &json, const std::string &id,
                    World &world);
  std::string id;
  std::string group;
  int layer = 2;
  std::string label;
  Vec2 position;
  Vec2 size = {120.0F, 40.0F};
  float scale = 3.0F;
  float fontSize = 0.0F;
  Color color = {0.16F, 0.18F, 0.32F, 1.0F};
  Color hoverColor = {0.24F, 0.28F, 0.48F, 1.0F};
  Color textColor = {1.0F, 1.0F, 1.0F, 1.0F};
  std::string script;
  std::string action;
  bool visible = true;
  bool hovered = false;

  void setPosition(Vec2 value) { position = value; }
  void setSize(Vec2 value) { size = value; }
  void setColor(Color value) { color = value; }
  void setOpacity(float value) { color.a = hoverColor.a = textColor.a = value; }
};
} // namespace demi::runtime
