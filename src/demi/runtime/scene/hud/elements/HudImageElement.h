#pragma once
#include "demi/runtime/scene/hud/HudElementDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>
namespace demi::runtime {
struct HudImageElement {
  static constexpr std::string_view typeName = "image";
  static void parse(const nlohmann::json &json, const std::string &id,
                    World &world);
  std::string id;
  std::string group;
  int layer = 1;
  std::string texture;
  std::string animation;
  int animationFrame = 0;
  Vec2 position;
  Vec2 size;
  Vec2 sourcePosition;
  Vec2 sourceSize;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  bool visible = true;
};
} // namespace demi::runtime
