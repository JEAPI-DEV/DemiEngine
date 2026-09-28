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
}
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
};
using EditorTerrainSurfacePtr = std::shared_ptr<const EditorTerrainSurface>;

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
