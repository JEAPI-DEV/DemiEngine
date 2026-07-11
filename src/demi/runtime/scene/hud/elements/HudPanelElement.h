#pragma once
#include "demi/runtime/scene/hud/HudElementDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>
namespace demi::runtime {
struct HudPanelElement {
  static constexpr std::string_view typeName = "panel";
  static void parse(const nlohmann::json &json, const std::string &id,
                    World &world);
  std::string id;
  std::string group;
  int layer = 2;
  Vec2 position;
  Vec2 size;
  float cornerRadius = 0.0F;
  float borderWidth = 0.0F;
  Color color = {0.06F, 0.07F, 0.18F, 0.70F};
  Color borderColor = {1.0F, 1.0F, 1.0F, 0.20F};
  bool visible = true;

  void setPosition(Vec2 value) { position = value; }
  void setSize(Vec2 value) { size = value; }
  void setRect(Vec2 newPosition, Vec2 newSize) {
    position = newPosition;
    size = newSize;
  }
  void setColor(Color value) { color = value; }
  void setOpacity(float value) { color.a = borderColor.a = value; }
};
} // namespace demi::runtime
