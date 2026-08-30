#pragma once

#include "demi/assets/AssetGroup.h"
#include "demi/runtime/debug/DebugOverlayConfig.h"
#include "demi/runtime/network/NetworkSystem.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace demi::runtime {

struct RuntimeInputDebugSnapshot {
  std::vector<std::string> keysDown;
  std::vector<std::string> mouseButtonsDown;
  std::size_t gamepads = 0;
  std::size_t touches = 0;
  Vec2 mousePosition;
  Vec2 mouseDelta;
  Vec2 mouseScroll;
  std::string textEntered;
};

struct RuntimePhysicsDebugSnapshot {
  std::size_t rigidbodies2D = 0;
  std::size_t rigidbodies3D = 0;
  std::size_t colliders2D = 0;
  std::size_t colliders3D = 0;
  std::size_t contacts2D = 0;
  std::size_t contacts3D = 0;
};

struct RuntimeNavigationDebugSnapshot {
  bool available = false;
  int width = 0;
  int height = 0;
  float cellSize = 0.0F;
  std::size_t blockers = 0;
  std::size_t weightedCells = 0;
};

struct RuntimeNetworkDebugSnapshot {
  bool available = false;
  NetworkMode mode = NetworkMode::Offline;
  bool connected = false;
  bool secure = false;
  std::uint32_t latencyMilliseconds = 0;
  std::string securityError;
};

struct RuntimeDebugSnapshot {
  std::size_t entities = 0;
  std::string focusedEntityId;
  RuntimeInputDebugSnapshot input;
  RuntimePhysicsDebugSnapshot physics;
  RuntimeNavigationDebugSnapshot navigation;
  RuntimeNetworkDebugSnapshot network;
  assets::AssetMemoryReport assets;
  DebugOverlayConfig overlays;
};

} // namespace demi::runtime
