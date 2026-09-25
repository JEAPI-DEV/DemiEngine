#include "demi/assets/MasonryGeneration.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/components/3dcomponents/Masonry3DComponent.h"
#include <stdexcept>

namespace demi::assets {
using Json = nlohmann::json;
namespace {

struct MasonryCell {
  std::string id;
  runtime::Vec3 position;
};

using ModelCellGroups = std::map<std::string, std::vector<MasonryCell>>;

runtime::Masonry3DComponent readMasonrySettings(const Json &entity) {
  std::string error;
  const auto parsed = runtime::RuntimeObjectModel::buildEntity(entity, error);
  if (!parsed) {
    throw std::runtime_error(error);
  }

  const auto *settings = parsed->component<runtime::Masonry3DComponent>();
  if (!settings) {
    throw std::invalid_argument("Expected an entity with Masonry3D");
  }
  return *settings;
}

ModelCellGroups groupCellsByModel(const runtime::Masonry3DComponent &wall,
                                  const runtime::Vec3 cellSize) {
  std::vector<std::string> modelIds;
  for (const auto &[variantName, modelId] : wall.models) {
    modelIds.push_back(modelId);
  }

  ModelCellGroups groups;
  for (int row = 0; row < wall.rows; ++row) {
    for (int column = 0; column < wall.columns; ++column) {
      const auto cellIndex =
          static_cast<std::size_t>(row) * wall.columns + column;
      const std::string &modelId = modelIds[cellIndex % modelIds.size()];
      const runtime::Vec3 position{
          -wall.size.x * 0.5F + (column + 0.5F) * cellSize.x,
          -wall.size.y * 0.5F + (row + 0.5F) * cellSize.y, 0.0F};
      const std::string cellId =
          "cell_" + std::to_string(row) + "_" + std::to_string(column);
      groups[modelId].push_back({cellId, position});
    }
  }
  return groups;
}

Json makeModelBatch(const Json &sourceEntity, const std::string &modelId,
                    const std::vector<MasonryCell> &cells,
                    const runtime::Vec3 cellSize, const std::string &textureId,
                    const std::size_t batchIndex) {
  const std::string regionId = sourceEntity.at("id").get<std::string>();
  Json batch;

  // The first batch keeps the source region's identity and gameplay components.
  // Additional model variants are render-only children of that region.
  if (batchIndex == 0) {
    batch = sourceEntity;
  } else {
    batch["id"] = regionId + "/__instances/" + std::to_string(batchIndex);
    batch["components"]["Transform3D"]["parent"] = regionId;
  }

  Json transforms = Json::object();
  for (const MasonryCell &cell : cells) {
    transforms[cell.id]["position"] = {cell.position.x, cell.position.y,
                                       cell.position.z};
  }

  auto &components = batch["components"];
  components.erase("Masonry3D");
  components["MeshRenderer"] = {{"model", modelId},
                                {"size", {cellSize.x, cellSize.y, cellSize.z}},
                                {"texture", textureId}};
  components["MeshInstances3D"]["transforms"] = std::move(transforms);

  for (const char *field : {"enabled", "layer", "persistent"}) {
    if (sourceEntity.contains(field)) {
      batch[field] = sourceEntity.at(field);
    }
  }
  return batch;
}

} // namespace

Json masonryIntactVisual(const Json &entity) {
  const auto wall = readMasonrySettings(entity);
  const bool hasRelief = !wall.heightMap.empty();
  const auto atlasGrid = hasRelief ? wall.textureGrid : runtime::Vec2{1, 1};

  Json result = entity;
  auto &components = result["components"];
  components.erase("Masonry3D");
  components["MeshRenderer"] = {
      {"shape", "cube"},
      {"size", {wall.size.x, wall.size.y, wall.size.z}},
      {"texture", wall.texture}};
  components["SurfaceRelief3D"] = {
      {"height_map", wall.heightMap},
      {"depth", hasRelief ? wall.reliefDepth : 0.0F},
      {"tiles", {wall.columns, wall.rows}},
      {"atlas_grid", {atlasGrid.x, atlasGrid.y}},
      {"uv_offset", {0, (atlasGrid.y - 1) / atlasGrid.y}},
      {"uv_scale", {1 / atlasGrid.x, 1 / atlasGrid.y}}};
  if (!hasRelief) {
    components["SurfaceRelief3D"]["segments"] = {1, 1};
  }
  return result;
}
Json buildIntactMasonryModelBatches(const Json &entity) {
  const auto wall = readMasonrySettings(entity);
  if (wall.models.empty()) {
    throw std::invalid_argument("Expected masonry model variants");
  }

  const runtime::Vec3 cellSize{wall.size.x / wall.columns,
                               wall.size.y / wall.rows, wall.size.z};
  const ModelCellGroups groups = groupCellsByModel(wall, cellSize);

  Json batches = Json::array();
  for (const auto &[modelId, cells] : groups) {
    batches.push_back(makeModelBatch(entity, modelId, cells, cellSize,
                                     wall.texture, batches.size()));
  }
  return batches;
}
bool hasMasonryAuthoring(const Json &value) {
  if (value.is_object()) {
    if (value.contains("components") &&
        value["components"].contains("Masonry3D"))
      return true;
    for (const char *field : {"entities", "children"})
      if (value.contains(field) && hasMasonryAuthoring(value[field]))
        return true;
  } else if (value.is_array()) {
    for (const auto &child : value)
      if (hasMasonryAuthoring(child))
        return true;
  }
  return false;
}
Json expandMasonry(const Json &entities, bool preview,
                   std::set<std::string> *generatedIds) {
  Json output = entities;
  std::set<std::string> ids;
  for (const auto &entity : entities)
    ids.insert(entity.at("id").get<std::string>());
  for (std::size_t index = 0; index < entities.size(); ++index) {
    const auto &entity = entities[index];
    if (!entity.contains("components") ||
        !entity["components"].contains("Masonry3D"))
      continue;
    std::string error;
    const auto parsed = runtime::RuntimeObjectModel::buildEntity(entity, error);
    if (!parsed)
      throw std::runtime_error(error);
    const auto &wall = *parsed->component<runtime::Masonry3DComponent>();
    const auto id = entity.at("id").get<std::string>();
    auto &components = output[index]["components"];
    if (!components.contains("Transform3D") ||
        components.contains("MeshRenderer") ||
        components.contains("Fracture3D"))
      throw std::runtime_error("Masonry3D requires Transform3D and generates "
                               "its own render/fracture cells: " +
                               id);
    components.erase("Masonry3D");
    std::vector<std::string> models;
    for (const auto &[name, model] : wall.models)
      models.push_back(model);
    for (int row = 0; row < wall.rows; ++row)
      for (int col = 0; col < wall.columns; ++col) {
        const auto cellId = id +
                            (preview ? "/__masonry_preview/cell_" : "/cell_") +
                            std::to_string(row) + "_" + std::to_string(col);
        if (!ids.insert(cellId).second)
          throw std::runtime_error("Generated masonry ID collision: " + cellId);
        if (generatedIds)
          generatedIds->insert(cellId);
        const float width = wall.size.x / wall.columns,
                    height = wall.size.y / wall.rows;
        Json mesh = {{"shape", "cube"},
                     {"size", {width, height, wall.size.z}},
                     {"texture", wall.texture}};
        Json fracture = {{"pieces", 1},
                         {"density", wall.density},
                         {"bond_health", wall.bondHealth},
                         {"debris_lifetime", wall.debrisLifetime},
                         {"debris_fade", wall.debrisFade}};
        if (!models.empty()) {
          mesh.erase("shape");
          mesh["model"] = models[(row * wall.columns + col) % models.size()];
          fracture["collider"] = "box";
        }
        if (wall.anchorBelow)
          fracture["anchor_below"] = *wall.anchorBelow;
        if (!wall.heightMap.empty())
          fracture["collider"] = "box";
        output.push_back(
            {{"id", cellId},
             {"name", "Masonry cell"},
             {"components",
              {{"Transform3D",
                {{"parent", id},
                 {"position",
                  {-wall.size.x * .5F + (col + .5F) * width,
                   -wall.size.y * .5F + (row + .5F) * height, 0}}}},
               {"MeshRenderer", mesh},
               {"Fracture3D", fracture}}}});
        if (!wall.heightMap.empty()) {
          const int nx = int(wall.textureGrid.x), ny = int(wall.textureGrid.y);
          output.back()["components"]["SurfaceRelief3D"] = {
              {"height_map", wall.heightMap},
              {"depth", wall.reliefDepth},
              {"uv_offset",
               {float(col % nx) / nx, float(ny - 1 - row % ny) / ny}},
              {"uv_scale", {1.F / nx, 1.F / ny}}};
        }
        if (preview)
          output.back()["components"].erase("Fracture3D");
        for (const char *field : {"enabled", "layer"})
          if (entity.contains(field))
            output.back()[field] = entity[field];
      }
  }
  return output;
}
} // namespace demi::assets
