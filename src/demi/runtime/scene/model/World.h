#pragma once

#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/HudData.h"
#include "demi/runtime/ui/UiModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace demi::runtime {

struct PhysicsContact2D {
  std::string entityId;
  std::string otherEntityId;
  std::string otherLayer;
  Vec2 normal;
  bool isTrigger = false;
};

struct World {
  std::filesystem::path scenePath;
  std::string id;
  std::string name;
  Vec2 hudCanvasSize = {960.0F, 540.0F};
  ui::UiDocument ui;
  std::vector<Entity> entities;
  std::vector<HudRectElement> hudRects;
  std::vector<HudPanelElement> hudPanels;
  std::vector<HudCircleElement> hudCircles;
  std::vector<HudImageElement> hudImages;
  std::vector<HudButtonElement> hudButtons;
  std::vector<HudTextElement> hudText;
  std::vector<DebugLine> debugLines;
  std::vector<PhysicsContact2D> physicsContacts;
};

} // namespace demi::runtime
