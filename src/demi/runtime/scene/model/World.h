#pragma once

#include "demi/runtime/debug/DebugOverlayConfig.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/HudData.h"
#include "demi/runtime/ui/UiModel.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct PhysicsContact2D {
  std::string entityId;
  std::string otherEntityId;
  std::string otherLayer;
  Vec2 normal;
  bool isTrigger = false;
};

struct AnimationEvent2D {
  std::string entityId;
  std::string clip;
  std::string name;
  int frame = 0;
};

struct AnimationCollisionOverlap2D {
  std::string sourceId;
  std::string targetId;
  std::string window;
  std::string receiver;
};

struct GridPlacementPreview {
  bool visible = false;
  bool valid = false;
  Vec2 tile;
  Vec2 footprint = {1.0F, 1.0F};
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
  std::vector<AnimationEvent2D> animationEvents;
  std::vector<AnimationEvent2D> stateAnimationEvents;
  std::vector<AnimationCollisionOverlap2D> animationCollisionOverlaps;
  std::unordered_map<std::string, std::uint16_t> physicsCategoryBits;
  std::unordered_map<std::string, std::uint16_t> physicsMaskBits;
  DebugOverlayConfig debug;
  GridPlacementPreview placementPreview;
};

} // namespace demi::runtime
