#pragma once

#include "demi/runtime/scene/ResourceLifetimeRegistry.h"
#include "demi/runtime/scene/model/ProjectData.h"

#include <future>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

enum class ScenePreparationState { Idle, Loading, Ready, Failed };

struct SceneTransition {
  std::string previousActiveScene;
  std::string activeScene;
  std::vector<std::string> unloadingEntities;
  std::vector<std::string> loadedEntities;
  std::vector<std::string> unloadingUiNodes;
  std::vector<std::string> loadedUiNodes;
  bool additive = false;
};

class SceneFlow {
public:
  void configure(ProjectData project);
  [[nodiscard]] bool prepare(std::string sceneId, bool additive);
  void poll();
  [[nodiscard]] ScenePreparationState state() const;
  [[nodiscard]] float progress() const;
  [[nodiscard]] const std::string &error() const;
  [[nodiscard]] bool preparedAdditive() const;
  [[nodiscard]] std::string activationError(const World &world) const;
  [[nodiscard]] std::optional<SceneTransition> activate(
      World &world, ResourceLifetimeRegistry &resources);
  [[nodiscard]] std::optional<SceneTransition> unload(
      World &world, std::string_view sceneId,
      ResourceLifetimeRegistry &resources);
  [[nodiscard]] bool setPersistent(World &world, std::string_view entityId,
                                   bool persistent);

private:
  struct Prepared {
    std::string sceneId;
    bool additive = false;
    std::optional<World> world;
    std::string error;
  };

  ProjectData project_;
  ScenePreparationState state_ = ScenePreparationState::Idle;
  std::future<Prepared> future_;
  std::optional<Prepared> prepared_;
  std::string error_;
};

} // namespace demi::runtime
