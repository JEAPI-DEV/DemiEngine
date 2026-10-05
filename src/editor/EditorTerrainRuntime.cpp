#include "editor/EditorTerrainRuntime.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainSurface.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include <stdexcept>

namespace demi::editor {
namespace {
class HeightFieldSurface final : public EditorTerrainSurface {
public:
  explicit HeightFieldSurface(std::shared_ptr<const runtime::HeightField> value)
      : field(std::move(value)) {}
  std::shared_ptr<const runtime::HeightField> field;

  std::shared_ptr<const runtime::HeightField> heightField() const override {
    return field;
  }

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

std::optional<EditorTerrainUpdate>
wrapTerrainUpdate(std::optional<runtime::TerrainUpdate> result) {
  if (!result)
    return std::nullopt;
  return EditorTerrainUpdate{
      std::make_shared<HeightFieldSurface>(std::move(result->field)),
      std::move(result->patch)};
}
} // namespace

std::optional<EditorTerrainUpdate>
updateEditorTerrain(const nlohmann::json &before, const nlohmann::json &after,
                    EditorTerrainSurfacePtr previous, std::stop_token stop,
                    const std::function<void(float)> &progress,
                    std::string &error) {
  if (!previous || !previous->heightField()) {
    error = "Terrain brush requires the current native surface.";
    return std::nullopt;
  }
  return wrapTerrainUpdate(runtime::updateTerrain(
      runtime::TerrainRecipe::parse(before),
      runtime::TerrainRecipe::parse(after), previous->heightField(), stop,
      progress));
}

std::optional<EditorTerrainUpdate>
updateEditorTerrainWithInputs(
    const nlohmann::json &before, const nlohmann::json &after,
    EditorTerrainSurfacePtr previous,
    const runtime::TerrainGenerationInputs &inputs, std::stop_token stop,
    const std::function<void(float)> &progress, std::string &error) {
  if (!previous || !previous->heightField()) {
    error = "Terrain update requires the current native surface.";
    return std::nullopt;
  }
  return wrapTerrainUpdate(runtime::updateTerrainWithInputs(
      runtime::TerrainRecipe::parse(before),
      runtime::TerrainRecipe::parse(after), previous->heightField(), inputs,
      stop, progress));
}

std::shared_ptr<const runtime::TerrainPatch>
mergeEditorTerrainPatches(std::shared_ptr<const runtime::TerrainPatch> first,
                          std::shared_ptr<const runtime::TerrainPatch> next) {
  if (!first)
    return next;
  if (!next)
    return first;
  return runtime::mergeTerrainPatches(*first, *next);
}

EditorTerrainSurfacePtr
applyEditorTerrainPatch(EditorTerrainSurfacePtr previous,
                        const runtime::TerrainPatch &patch, bool forward,
                        std::string &error) {
  try {
    if (!previous || !previous->heightField())
      throw std::invalid_argument(
          "Terrain history requires the current native surface.");
    auto update =
        runtime::applyTerrainPatch(previous->heightField(), patch, forward);
    return std::make_shared<HeightFieldSurface>(std::move(update.field));
  } catch (const std::exception &exception) {
    error = exception.what();
    return {};
  }
}

bool installEditorTerrain(runtime::World &world, std::string_view owner,
                          const nlohmann::json &recipe,
                          const EditorTerrainUpdate &update,
                          std::string &error) {
  if (!update.surface || !update.surface->heightField() || !update.patch) {
    error = "Terrain publication requires a native surface and patch.";
    return false;
  }
  runtime::TerrainUpdate native;
  native.field = update.surface->heightField();
  native.patch = update.patch;
  native.invalidation = update.patch->invalidation;
  try {
    return runtime::updateTerrainWorld(world, owner, recipe, native, error);
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

nlohmann::json defaultEditorTerrainRecipe() {
  return runtime::TerrainRecipe::defaults();
}

EditorTerrainSurfacePtr
generateEditorTerrain(const nlohmann::json &recipe, std::stop_token stop,
                      const std::function<void(float)> &progress,
                      std::string &error) {
  return generateEditorTerrainWithInputs(
      recipe, runtime::TerrainGenerationInputs{}, stop, progress, error);
}

EditorTerrainSurfacePtr
generateEditorTerrainWithInputs(
    const nlohmann::json &recipe,
    const runtime::TerrainGenerationInputs &inputs, std::stop_token stop,
    const std::function<void(float)> &progress, std::string &error) {
  error.clear();
  const auto parsed = runtime::TerrainRecipe::parse(recipe);
  if (parsed.paletteId.empty() && inputs.palette) {
    error = "Editor generation received a palette absent from the recipe.";
    return {};
  }
  if (!parsed.paletteId.empty() &&
      (!inputs.palette || inputs.palette->id != parsed.paletteId ||
       inputs.fingerprint.empty())) {
    error = "Terrain palette must be resolved before editor generation: " +
            parsed.paletteId;
    return {};
  }
  auto field = runtime::TerrainGenerator::generate(
      parsed, inputs.palette.get(), stop, progress, inputs.fingerprint);
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
  if (!field && !terrain->recipe.is_null())
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
  // Key on the palette content, not just its id, so an edited palette
  // cannot leave a stale field reachable in the editor.
  runtime::publishTerrain(recipe, generated->field->inputFingerprint,
                       generated->field);
}
} // namespace demi::editor
