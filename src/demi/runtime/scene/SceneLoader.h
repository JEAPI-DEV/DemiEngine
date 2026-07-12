#pragma once

#include "demi/runtime/scene/model/ProjectData.h"
#include "demi/runtime/scene/model/World.h"

#include <optional>

namespace demi::runtime {

struct LoadedProject {
  ProjectData project;
  World world;
};

[[nodiscard]] std::optional<LoadedProject>
loadProject(const std::filesystem::path &projectPath, std::string &error);
[[nodiscard]] std::optional<World> loadScene(const ProjectData &project,
                                             const std::string &sceneId,
                                             std::string &error);

} // namespace demi::runtime
