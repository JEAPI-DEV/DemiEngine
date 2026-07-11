#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct HudButtonElement {
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
};
struct HudTextElement {
  std::string id;
  std::string group;
  int layer = 3;
  std::string text;
  Vec2 position;
  float scale = 3.0F;
  float fontSize = 0.0F;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  bool visible = true;
};
struct HudRectElement {
  std::string id;
  std::string group;
  int layer = 0;
  Vec2 position;
  Vec2 size;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  bool visible = true;
};
struct HudPanelElement {
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
};
struct HudCircleElement {
  std::string id;
  std::string group;
  int layer = 2;
  Vec2 center;
  float radius = 16.0F;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  bool visible = true;
};
struct HudImageElement {
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
