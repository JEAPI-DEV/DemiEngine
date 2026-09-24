#include "demi/assets/MasonryGeneration.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/components/3dcomponents/Masonry3DComponent.h"
#include <stdexcept>

namespace demi::assets {
using J = nlohmann::json;
J masonryIntactVisual(const J &entity) {
  std::string error;
  const auto parsed=runtime::RuntimeObjectModel::buildEntity(entity,error);
  if(!parsed) throw std::runtime_error(error);
  const auto &wall=*parsed->component<runtime::Masonry3DComponent>();
  J result=entity;
  auto &c=result["components"];
  c.erase("Masonry3D");
  c["MeshRenderer"]={{"shape","cube"},{"size",{wall.size.x,wall.size.y,wall.size.z}},{"texture",wall.texture}};
  const bool relief=!wall.heightMap.empty();
  const auto grid=relief?wall.textureGrid:runtime::Vec2{1,1};
  c["SurfaceRelief3D"]={{"height_map",wall.heightMap},{"depth",relief?wall.reliefDepth:0.F},
      {"tiles",{wall.columns,wall.rows}},{"atlas_grid",{grid.x,grid.y}},
      {"uv_offset",{0,(grid.y-1)/grid.y}},{"uv_scale",{1/grid.x,1/grid.y}}};
  if(!relief)c["SurfaceRelief3D"]["segments"]={1,1};
  return result;
}
bool hasMasonryAuthoring(const J &value) {
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
J expandMasonry(const J &entities, bool preview,
                std::set<std::string> *generatedIds) {
  J output = entities;
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
        const auto cellId =
            id + (preview ? "/__masonry_preview/cell_" : "/cell_") +
            std::to_string(row) + "_" + std::to_string(col);
        if (!ids.insert(cellId).second)
          throw std::runtime_error("Generated masonry ID collision: " + cellId);
        if (generatedIds)
          generatedIds->insert(cellId);
        const float width = wall.size.x / wall.columns,
                    height = wall.size.y / wall.rows;
        J mesh = {{"shape", "cube"},
                  {"size", {width, height, wall.size.z}},
                  {"texture", wall.texture}};
        J fracture = {{"pieces", 1},
                      {"density", wall.density},
                      {"bond_health", wall.bondHealth}};
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
