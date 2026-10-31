#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"
#include <memory>
#include <optional>
#include <string>

namespace demi::runtime {
struct World;
class RuntimePrefabService;
// Runtime/embedded-Play composition service. Regions keep a complete active
// cover while replacement meshes and collision shapes prepare on workers.
class RegionalTerrainSystem {
public:
  struct Options {
    bool physics = true;
    bool vegetation = true;
  };
  RegionalTerrainSystem();
  explicit RegionalTerrainSystem(Options options);
  [[nodiscard]] bool hasPendingWork() const;
  ~RegionalTerrainSystem();
  RegionalTerrainSystem(const RegionalTerrainSystem &) = delete;
  RegionalTerrainSystem &operator=(const RegionalTerrainSystem &) = delete;
  // Camera positions are resolved through the ordinary hierarchy. The same
  // implementation is used by standalone and embedded runtime loops. Publish
  // at the fixed-step boundary immediately before physics synchronization so
  // newly adopted shapes replace removed bodies before gameplay queries resume.
  [[nodiscard]] bool update(World &world, std::string &error,
                            RuntimePrefabService *prefabs = nullptr,
                            float deltaSeconds = 1.F / 60.F);
  [[nodiscard]] bool update(World &world, Vec3 camera, std::string &error,
                            RuntimePrefabService *prefabs = nullptr,
                            float deltaSeconds = 1.F / 60.F);

  [[nodiscard]] bool updateView(World &world, Vec3 position,
                                std::optional<Vec3> forward, std::string &error,
                                RuntimePrefabService *prefabs = nullptr,
                                float deltaSeconds = 1.F / 60.F);

private:
  struct Impl;
  Options options_;
  std::unique_ptr<Impl> impl_;
};
} // namespace demi::runtime
