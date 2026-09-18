#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace demi::runtime {
struct ColliderAsset3D;
struct DestructionGroupCheckpoint3D {
  std::vector<std::string> parts;
  Vec3 position, rotation, velocity, angularVelocity;
  bool retired = false;
};
struct DestructionCheckpoint3D {
  std::string geometryHash;
  std::map<std::string, float> bonds, anchors;
  std::vector<DestructionGroupCheckpoint3D> groups;
};
std::string destructionGeometryHash(const ColliderAsset3D &asset);
nlohmann::json writeDestructionCheckpoint(const DestructionCheckpoint3D &state);
DestructionCheckpoint3D readDestructionCheckpoint(const nlohmann::json &json,
    const ColliderAsset3D &asset, const std::string &geometryHash);
} // namespace demi::runtime
