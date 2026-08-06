#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct PhysicsContact3D {
  std::string entityId;
  std::string otherEntityId;
  std::string otherLayer;
  std::string phase = "stay";
  Vec3 point;
  Vec3 normal;
  float penetration = 0.0F;
  bool isTrigger = false;
};

struct PhysicsQueryHit3D {
  std::string entityId;
  std::string layer;
  Vec3 point;
  Vec3 normal;
  float distance = 0.0F;
  float fraction = 0.0F;
  bool isTrigger = false;
};

using PhysicsRaycastHit3D = PhysicsQueryHit3D;

struct CharacterMoveResult3D {
  Vec3 appliedMotion;
  Vec3 remainingMotion;
  Vec3 groundNormal = {0.0F, 1.0F, 0.0F};
  bool grounded = false;
  bool hitCeiling = false;
  bool hitWall = false;
  bool stepped = false;
  std::string groundEntity;
};

struct CameraRay3D {
  Vec3 origin;
  Vec3 direction;
};

} // namespace demi::runtime
