#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace demi::runtime {
struct World;
struct HeightField;
struct TerrainPatch;
} // namespace demi::runtime
namespace demi::editor {

// Adapter to the shared runtime generator. Implementations own immutable core
// results; all queries use terrain-local coordinates and the rendered surface.
class EditorTerrainSurface {
public:
  virtual ~EditorTerrainSurface() = default;
  virtual std::optional<runtime::Vec3>
  raycast(runtime::Vec3 origin, runtime::Vec3 direction) const = 0;
  virtual std::optional<float> height(runtime::Vec2 position) const = 0;
  virtual nlohmann::json protection(runtime::Vec2 center, float radius,
                                    float strength, float falloff) const = 0;
  virtual std::shared_ptr<const runtime::HeightField> heightField() const {
    return {};
  }
};
using EditorTerrainSurfacePtr = std::shared_ptr<const EditorTerrainSurface>;

struct EditorTerrainUpdate {
  EditorTerrainSurfacePtr surface;
  std::shared_ptr<const runtime::TerrainPatch> patch;
};

std::optional<EditorTerrainUpdate>
updateEditorTerrain(const nlohmann::json &before, const nlohmann::json &after,
                    EditorTerrainSurfacePtr previous, std::stop_token stop,
                    const std::function<void(float)> &progress,
                    std::string &error);
std::shared_ptr<const runtime::TerrainPatch>
mergeEditorTerrainPatches(std::shared_ptr<const runtime::TerrainPatch> first,
                          std::shared_ptr<const runtime::TerrainPatch> next);
EditorTerrainSurfacePtr
applyEditorTerrainPatch(EditorTerrainSurfacePtr previous,
                        const runtime::TerrainPatch &patch, bool forward,
                        std::string &error);
bool installEditorTerrain(runtime::World &world, std::string_view owner,
                          const nlohmann::json &recipe,
                          const EditorTerrainUpdate &update,
                          std::string &error);

nlohmann::json defaultEditorTerrainRecipe();
EditorTerrainSurfacePtr
generateEditorTerrain(const nlohmann::json &recipe, std::stop_token stop,
                      const std::function<void(float)> &progress,
                      std::string &error);
EditorTerrainSurfacePtr currentEditorTerrain(const runtime::World &world,
                                             std::string_view entityId);
// Retain the surface through the subsequent workspace rebuild (weak cache).
void publishEditorTerrain(const nlohmann::json &recipe,
                          EditorTerrainSurfacePtr surface);

} // namespace demi::editor
