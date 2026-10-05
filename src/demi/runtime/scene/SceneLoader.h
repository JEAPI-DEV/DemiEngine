#pragma once

#include "demi/runtime/scene/model/ProjectData.h"
#include "demi/runtime/scene/model/World.h"

#include <nlohmann/json_fwd.hpp>

#include <optional>

namespace demi::runtime {

struct LoadedProject {
  ProjectData project;
  World world;
};

[[nodiscard]] std::optional<LoadedProject>
loadProject(const std::filesystem::path &projectPath, std::string &error);
// The prefab service is owned by the composition root and shared with terrain
// scatter, so a prefab placed by a scene and one placed by a palette use one
// pool. Null is valid: no prefab-backed role is placed.
[[nodiscard]] std::optional<World>
loadScene(const ProjectData &project, const std::string &sceneId,
          std::string &error, class RuntimePrefabService *prefabs = nullptr);
// Builds a scene world from an already-parsed authored document without
// touching disk for the scene file itself. Prefab references and the optional
// HUD are still resolved relative to the scene's registered path. Used by the
// editor to refresh its preview projection after an in-memory authored edit.
// compileFractures=false retains editable source meshes and authoring components;
// gameplay, including embedded Play, must use the default compiled projection.
[[nodiscard]] std::optional<World>
loadSceneDocument(const ProjectData &project, const std::string &sceneId,
                  const nlohmann::json &document, std::string &error,
                  bool compileFractures = true,
                  class RuntimePrefabService *prefabs = nullptr);

} // namespace demi::runtime
