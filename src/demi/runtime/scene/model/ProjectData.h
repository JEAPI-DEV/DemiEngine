#pragma once

#include "demi/runtime/debug/DebugOverlayConfig.h"
#include "demi/runtime/input/InputActionMap.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace demi::runtime {

struct LuaActionHandler {
  std::string action;
  std::string functionName;
};
struct LuaEventHandler {
  std::string eventName;
  std::string functionName;
};

struct SceneEntry {
  std::string id;
  std::filesystem::path path;
};

struct PhysicsLayer2D {
  std::string name;
  std::vector<std::string> collidesWith;
};

struct SimulationConfig {
  float fixedTimestep = 1.0F / 60.0F;
  std::uint64_t randomSeed = 1;
};

struct DisplayConfig {
  bool vsync = true;
};

struct PerformanceBudgets {
  float maximumFrameMilliseconds = 16.67F;
  int maximumDrawCalls = 500;
  int maximumResidentAssets = 256;
  int maximumVisibleInstances = 1000;
  int maximumUniqueMeshes = 128;
  int maximumTriangles = 250000;
  int maximumTextureMemoryMegabytes = 128;
  int maximumLights = 8;
  int maximumShadowLights = 1;
  int maximumTransparentDraws = 128;
};

struct ProjectData {
  std::filesystem::path projectPath;
  std::filesystem::path projectDirectory;
  std::string name;
  std::string mainScene;
  std::string scriptEntry;
  std::string networkContract;
  std::vector<std::string> scriptModules;
  std::vector<SceneEntry> scenes;
  std::vector<std::string> preloadedAssets;
  input::InputActionMap inputActions;
  std::vector<PhysicsLayer2D> physicsLayers2D;
  SimulationConfig simulation;
  DisplayConfig display;
  PerformanceBudgets performanceBudgets;
  DebugOverlayConfig debug;
};

} // namespace demi::runtime
