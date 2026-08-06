#pragma once

#include "demi/runtime/debug/DebugOverlayConfig.h"
#include "demi/runtime/physics/Box2DWorldState.h"
#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/physics/Physics3DTypes.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/ui/UiModel.h"
#include "demi/runtime/ui/UiTweenSystem.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime {

class PhysicsWorld3D;

struct PhysicsContact2D {
  std::string entityId;
  std::string otherEntityId;
  std::string otherLayer;
  std::string phase = "stay";
  Vec2 point;
  Vec2 normal;
  float normalImpulse = 0.0F;
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
  std::string activeSceneId;
  std::unordered_set<std::string> loadedSceneIds;
  Vec2 hudCanvasSize = {960.0F, 540.0F};
  ui::UiDocument ui;
  ui::UiTweenSystem uiTweens;
  std::vector<Entity> entities;
  std::vector<DebugLine> debugLines;
  std::vector<PhysicsContact2D> physicsContacts;
  std::vector<PhysicsContact2D> previousPhysicsContacts;
  std::vector<PhysicsContact3D> physicsContacts3D;
  std::vector<PhysicsContact3D> previousPhysicsContacts3D;
  std::vector<AnimationEvent2D> animationEvents;
  std::vector<AnimationEvent2D> stateAnimationEvents;
  std::vector<AnimationCollisionOverlap2D> animationCollisionOverlaps;
  std::unordered_map<std::string, std::uint16_t> physicsCategoryBits;
  std::unordered_map<std::string, std::uint16_t> physicsMaskBits;
  std::unordered_map<std::string, ColliderAsset3D> colliderAssets3D;
  DebugOverlayConfig debug;
  GridPlacementPreview placementPreview;
  bool tilemapCollisionDirty = false;
  std::unique_ptr<Box2DWorldState> box2dState;
  std::shared_ptr<PhysicsWorld3D> physicsWorld3D;
};

} // namespace demi::runtime
