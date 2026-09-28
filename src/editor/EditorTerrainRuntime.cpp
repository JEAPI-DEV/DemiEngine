#include "editor/EditorTerrainRuntime.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainSurface.h"
#include <stdexcept>

namespace demi::editor {
namespace {
class HeightFieldSurface final : public EditorTerrainSurface {
public:
  explicit HeightFieldSurface(std::shared_ptr<const runtime::HeightField> value)
      : field(std::move(value)) {}
  std::shared_ptr<const runtime::HeightField> field;

  std::optional<runtime::Vec3> raycast(runtime::Vec3 origin,
                                       runtime::Vec3 direction) const override {
    return runtime::raycastTerrain(*field, origin, direction);
  }

  std::optional<float> height(runtime::Vec2 position) const override {
    return runtime::sampleTerrainHeight(*field, position);
  }

  nlohmann::json protection(runtime::Vec2 center, float radius, float strength,
                            float falloff) const override {
    runtime::TerrainRecipe recipe;
    recipe.size = field->size;
    recipe.cellsX = field->cellsX;
    recipe.cellsZ = field->cellsZ;
    recipe.edits.push_back(runtime::createProtectionEdit(*field, center, radius,
                                                         strength, falloff));
    return recipe.toJson()["edits"].back();
  }
};
} // namespace

nlohmann::json defaultEditorTerrainRecipe() {
  return runtime::TerrainRecipe::defaults();
}

EditorTerrainSurfacePtr
generateEditorTerrain(const nlohmann::json &recipe, std::stop_token stop,
                      const std::function<void(float)> &progress,
                      std::string &error) {
  auto field = runtime::TerrainGenerator::generate(
      runtime::TerrainRecipe::parse(recipe), stop, progress);
  if (!field) {
    error = "Terrain generation cancelled.";
    return {};
  }
  return std::make_shared<HeightFieldSurface>(
      std::make_shared<const runtime::HeightField>(std::move(*field)));
}

EditorTerrainSurfacePtr currentEditorTerrain(const runtime::World &world,
                                             std::string_view entityId) {
  const auto *entity = runtime::findEntity(world, std::string(entityId));
  const auto *terrain =
      entity ? entity->component<runtime::Terrain3DComponent>() : nullptr;
  if (!terrain)
    return {};
  auto field = terrain->generated;
  if (!field)
    field = runtime::findTerrain(terrain->recipe);
  return field ? std::make_shared<HeightFieldSurface>(std::move(field))
               : nullptr;
}

void publishEditorTerrain(const nlohmann::json &recipe,
                          EditorTerrainSurfacePtr surface) {
  const auto generated =
      std::dynamic_pointer_cast<const HeightFieldSurface>(surface);
  if (!generated)
    throw std::invalid_argument("Cannot publish an unknown terrain surface.");
  runtime::publishTerrain(recipe, generated->field);
}
} // namespace demi::editor
