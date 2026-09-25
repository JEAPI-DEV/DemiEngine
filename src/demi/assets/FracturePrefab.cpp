#include "demi/assets/FracturePrefab.h"
#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/ColliderShapeAsset.h"
#include "demi/assets/ConvexFracture.h"
#include "demi/assets/GltfSkinnedModel.h"
#include "demi/runtime/geometry/BoxGeometry3D.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/World.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <span>
#include <stdexcept>

namespace demi::assets {
namespace {
using J = nlohmann::json;
using P = FracturePoint;
std::string digest(const std::string &text) {
  return hashBytes(std::span(
      reinterpret_cast<const unsigned char *>(text.data()), text.size()));
}
void require(bool ok, const std::string &error) {
  if (!ok)
    throw std::runtime_error(error);
}
FractureSolid box(const std::string &id) {
  FractureSolid s{.id = id};
  for (const auto &face : runtime::geometry::boxFaces) {
    FractureFace f;
    for (int i = 0; i < 4; ++i)
      f.vertices.push_back({{face[i].x, face[i].y, face[i].z},
                            {runtime::geometry::boxFaceUvs[i].x,
                             runtime::geometry::boxFaceUvs[i].y}});
    s.faces.push_back(std::move(f));
  }
  return s;
}
J mesh(const FractureSolid &solid, bool interior, J renderer) {
  renderer.erase("model");
  renderer.erase("medium_lod_model");
  renderer.erase("low_lod_model");
  renderer["shape"] = "mesh";
  renderer["size"] = {1, 1, 1};
  renderer["vertices"] = J::array();
  renderer["normals"] = J::array();
  renderer["uvs"] = J::array();
  for (const auto &face : solid.faces) {
    if (face.interior != interior)
      continue;
    const auto n = fractureNormal(face);
    for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i)
      for (const auto index : {std::size_t(0), i, i + 1}) {
        renderer["vertices"].push_back(face.vertices[index].position);
        renderer["normals"].push_back(n);
        renderer["uvs"].push_back(face.vertices[index].uv);
      }
  }
  return renderer;
}
} // namespace
J compileFracturePrefab(const std::filesystem::path &project, const J &entities,
                        const J &settings) {
  require(settings.is_object() && settings.contains("generator_version") &&
              settings["generator_version"].is_number_integer() &&
              settings.value("generator_version", 0) == 1,
          "Fracture recipe requires generator_version 1");
  for (const auto &[key, value] : settings.items())
    require(key == "source" || key == "generator_version" || key == "seed" ||
                key == "mass" || key == "max_bodies" || key == "objects",
            "Unknown fracture setting: " + key);
  require(settings.contains("objects") && settings["objects"].is_object() &&
              !settings["objects"].empty(),
          "Select fracture source objects");
  auto mass = settings.value("mass", 1000.0);
  require(std::isfinite(mass) && mass > 0 && mass < 1e12,
          "Fracture mass must be finite and positive");
  require(!settings.contains("seed") || settings["seed"].is_number_unsigned() ||
              settings["seed"].is_number_integer(),
          "Fracture seed must be an integer");
  const auto seed64 = settings.value("seed", std::int64_t(1));
  require(seed64 >= 0 && seed64 <= UINT32_MAX, "Fracture seed must fit uint32");
  require(!settings.contains("max_bodies") ||
              settings["max_bodies"].is_number_integer(),
          "max_bodies must be an integer");
  const auto maxBodies = settings.value("max_bodies", std::int64_t(64));
  require(maxBodies >= 1 && maxBodies <= INT32_MAX,
          "Fracture max_bodies must be a positive 32-bit integer");
  runtime::World world;
  std::map<std::string, J> authored;
  for (const auto &json : entities) {
    std::string error;
    auto entity = runtime::RuntimeObjectModel::buildEntity(json, error);
    require(bool(entity), error);
    require(authored.emplace(entity->id, json).second,
            "Duplicate prefab entity ID");
    world.entities.push_back(std::move(*entity));
  }
  require(runtime::validateTransform3DHierarchy(world).empty(),
          "Invalid source prefab hierarchy");
  const auto registry = loadAssetRegistry(project);
  struct Chunk {
    FractureSolid solid;
    J renderer;
    J options;
    double health;
    std::string name;
    std::string sourceId;
    J visualTransform;
    J relief;
  };
  std::vector<Chunk> chunks;
  bool hasDensity = false;
  for (const auto &[id, options] : settings["objects"].items()) {
    require(authored.contains(id), "Missing source prefab object: " + id);
    require(options.is_object(), "Fracture object settings must be objects");
    for (const auto &[key, value] : options.items())
      require(key == "pieces" || key == "bond_health" ||
                  key == "anchor_below" || key == "interior_color" ||
                  key == "interior_material" || key == "density" || key == "collider" || key=="debris_lifetime" || key=="debris_fade",
              "Unknown object fracture setting: " + key);
    if (options.contains("density")) {
      const auto density = options["density"].get<double>();
      require(std::isfinite(density) && density >= 0.001 && density <= 1000000,
              "density must be 0.001..1000000 kg/m^3");
      hasDensity = true;
    }
    if (options.contains("interior_color")) {
      const auto &color = options["interior_color"];
      require(color.is_array() && color.size() == 4,
              "interior_color requires four normalized numbers");
      for (const auto &value : color)
        require(value.is_number() && std::isfinite(value.get<double>()) &&
                    value.get<double>() >= 0 && value.get<double>() <= 1,
                "interior_color channels must be in [0,1]");
    }
    if (options.contains("interior_material")) {
      require(options["interior_material"].is_string(),
              "interior_material must be a material asset ID");
      const auto *material =
          findAsset(registry, options["interior_material"].get<std::string>());
      require(material && material->type == "Material",
              "Interior material asset was not found");
    }
    require(!options.contains("pieces") ||
                options["pieces"].is_number_integer(),
            "pieces must be an integer");
    const auto count64 = options.value("pieces", std::int64_t(8));
    require(count64 >= 1 && count64 <= INT32_MAX,
            "Fracture pieces must be a positive 32-bit integer");
    const int count = static_cast<int>(count64);
    const auto collider = options.value("collider", "source");
    require(collider == "source" || (collider == "box" && count == 1),
            "Box proxy fracture requires pieces=1");
    require(count >= 1 && chunks.size() + std::size_t(count) < UINT32_MAX,
            "Fracture requires positive piece counts fitting 32-bit indices");
    auto entity = std::ranges::find(world.entities, id, &runtime::Entity::id);
    const auto *renderer = entity->component<runtime::MeshRendererComponent>();
    const auto transform = runtime::resolveWorldTransform3D(world, *entity);
    require(renderer && transform,
            "Fracture source requires MeshRenderer and Transform3D: " + id);
    const auto &components = authored[id]["components"];
    require(!components.contains("SurfaceRelief3D") || (collider=="box" && count==1),
            "Surface relief fracture requires a single-piece box proxy");
    require(!components.contains("AnimationPlayer3D") &&
                !components.contains("Destructible3D"),
            "Fracture input must be an ordinary static mesh, not an "
            "animated/destructible object");
    const auto hash =
        digest(id); // Identity depends on source ID, not entity array order.
    FractureSolid solid{.id = "p" + hash.substr(hash.find(':') + 1) + "r"};
    if (collider == "box") {
      require(!renderer->model.empty() || !renderer->vertices.empty() || components.contains("SurfaceRelief3D"),
              "Box proxy requires a detailed model or inline mesh");
      solid = box(solid.id);
    } else if (!renderer->vertices.empty()) {
      require(renderer->vertices.size() % 3 == 0 &&
                  renderer->vertices.size() / 3 <= 1024,
              "Inline source mesh must contain at most 1024 triangles");
      require(renderer->uvs.empty() ||
                  renderer->uvs.size() == renderer->vertices.size(),
              "Inline mesh UV count must match vertices");
      for (std::size_t i = 0; i < renderer->vertices.size(); i += 3) {
        FractureFace face;
        for (std::size_t j = 0; j < 3; ++j) {
          const auto p = renderer->vertices[i + j];
          const auto uv =
              renderer->uvs.empty() ? runtime::Vec2{} : renderer->uvs[i + j];
          face.vertices.push_back({{p.x, p.y, p.z}, {uv.x, uv.y}});
        }
        solid.faces.push_back(std::move(face));
      }
    } else if (renderer->model.empty()) {
      require(renderer->shape == "cube",
              "Only box primitives and closed convex glTF/GLB meshes are "
              "supported: " +
                  id);
      solid = box(solid.id);
    } else {
      const auto *asset = findAsset(registry, renderer->model);
      require(asset && asset->type == "Model3D",
              "Source model asset missing: " + renderer->model);
      std::string issue;
      const auto profile = parseModelImportProfile(
          J::parse(asset->settingsJson), nullptr, asset->manifestPath.string());
      require(bool(profile), "Invalid model import profile");
      const auto geometry =
          loadGltfSkinnedModel3D(asset->sourcePath, *profile, issue);
      require(bool(geometry), issue);
      require(geometry->skins.empty(),
              "Skinned fracture source meshes are not supported");
      std::vector<runtime::Vec3> positions;
      require(geometry->bindPosePositions(positions, issue), issue);
      require(!renderer->material.empty() || !renderer->texture.empty() ||
                  components["MeshRenderer"].contains("color"),
              "Imported fracture meshes require an explicit exterior material, "
              "texture or color; embedded material extraction is not supported "
              "yet");
      require(geometry->indices.size() % 3 == 0,
              "Source must contain triangles");
      for (std::size_t i = 0; i < geometry->indices.size(); i += 3) {
        FractureFace face;
        for (int j = 0; j < 3; ++j) {
          const auto index = geometry->indices[i + j];
          require(index < positions.size() && index < geometry->vertices.size(),
                  "Invalid model triangle index");
          const auto &p = positions[index];
          const auto &uv = geometry->vertices[index].uv;
          face.vertices.push_back({{p.x, p.y, p.z}, {uv.x, uv.y}});
        }
        solid.faces.push_back(std::move(face));
      }
    }
    for (auto &face : solid.faces)
      for (auto &v : face.vertices) {
        const auto p = runtime::transformPoint3D(
            *transform, {float(v.position[0] * renderer->size.x),
                         float(v.position[1] * renderer->size.y),
                         float(v.position[2] * renderer->size.z)});
        v.position = {p.x, p.y, p.z};
      }
    const double health = options.value("bond_health", 1.0);
    require(std::isfinite(health) && health > 0 && health <= 1e30,
            "bond_health must be finite and positive");
    std::vector<FractureSolid> pieces;
    try {
      pieces = fractureConvexSolid(
          std::move(solid), count,
          std::uint32_t(seed64) ^
              std::uint32_t(
                  std::stoull(hash.substr(hash.find(':') + 1), nullptr, 16)));
    } catch (const std::exception &error) {
      throw std::runtime_error("Object '" + id + "': " + error.what());
    }
    for (auto &piece : pieces)
      chunks.push_back({std::move(piece), components["MeshRenderer"], options,
                        health, entity->name, id,
                        {{"parent", "body"},
                         {"position", {transform->position.x, transform->position.y, transform->position.z}},
                         {"rotation", {transform->rotation.x, transform->rotation.y, transform->rotation.z}},
                         {"scale", {transform->scale.x, transform->scale.y, transform->scale.z}}},
                        components.value("SurfaceRelief3D",J())});
  }
  std::ranges::sort(chunks, {}, [](const Chunk &c) { return c.solid.id; });
  J parts = J::array(), anchors = J::array(), bonds = J::array(),
    mapping = J::object(), output = J::array(), sources = J::object(), fading=J::object();
  double densityMass = 0;
  for (const auto &chunk : chunks) {
    const auto &id = chunk.solid.id;
    sources[id] = chunk.sourceId;
    sources[id + "_interior"] = chunk.sourceId;
    const auto points = fracturePoints(chunk.solid);
    parts.push_back({{"id", id}, {"points", points}});
    const double density = chunk.options.value("density", 1000.0);
    if (hasDensity)
      parts.back()["density"] = density;
    densityMass += fractureVolume(chunk.solid) * density;
    if (chunk.options.contains("anchor_below")) {
      const double y = chunk.options["anchor_below"].get<double>();
      require(std::isfinite(y), "anchor_below must be finite");
      if (std::ranges::any_of(points, [&](P p) { return p[1] <= y; }))
        anchors.push_back(id);
    }
    mapping[id] = id;
    const float lifetime=chunk.options.value("debris_lifetime",0.F);
    const float fade=chunk.options.value("debris_fade",1.F);
    require(std::isfinite(lifetime) && lifetime>=0 && std::isfinite(fade) && fade>=0,"Invalid fracture debris lifetime/fade");
    if(lifetime>0) fading[id]={{"lifetime",lifetime},{"fade",fade}};
    if (chunk.options.value("collider", "source") == "box") {
      output.push_back({{"id", id}, {"name", chunk.name + " shard "},
                       {"components", {{"Transform3D", chunk.visualTransform},
                                       {"MeshRenderer", chunk.renderer}}}});
      if (!chunk.relief.is_null()) output.back()["components"]["SurfaceRelief3D"]=chunk.relief;
      continue;
    }
    auto exterior = mesh(chunk.solid, false, chunk.renderer);
    J interior = {
        {"color", chunk.options.value("interior_color",
                                      J::array({0.35, 0.33, 0.30, 1}))}};
    if (chunk.options.contains("interior_material"))
      interior["material"] = chunk.options["interior_material"];
    interior = mesh(chunk.solid, true, interior);
    const bool onlyInterior = exterior["vertices"].empty();
    output.push_back(
        {{"id", id},
         {"name", chunk.name + " shard " + id.substr(id.find('r') + 1)},
         {"components",
          {{"Transform3D", {{"parent", "body"}}},
           {"MeshRenderer", onlyInterior ? interior : exterior}}}});
    if (!onlyInterior && !interior["vertices"].empty())
      output.push_back(
          {{"id", id + "_interior"},
           {"components",
            {{"Transform3D", {{"parent", id}}}, {"MeshRenderer", interior}}}});
  }
  for (std::size_t i = 0; i < chunks.size(); ++i)
    for (std::size_t j = i + 1; j < chunks.size(); ++j) {
      if (!fractureSolidsTouch(chunks[i].solid, chunks[j].solid))
        continue;
      require(bonds.size() < UINT32_MAX,
              "Generated bond graph exceeds 32-bit indices");
      const auto bondHash =
          digest(chunks[i].solid.id + ":" + chunks[j].solid.id);
      bonds.push_back(
          {{"id", "b" + bondHash.substr(bondHash.find(':') + 1)},
           {"parts", {chunks[i].solid.id, chunks[j].solid.id}},
           {"health", std::min(chunks[i].health, chunks[j].health)}});
    }
  J collider = {{"format_version", 1},
                {"shape", "compound"},
                {"parts", parts},
                {"fracture", {{"bonds", bonds}, {"anchors", anchors}}}};
  std::string issue;
  require(bool(parseColliderShapeAsset(collider, issue)),
          "Generated fracture graph: " + issue +
              " Check that source objects touch or overlap.");
  if (hasDensity && !settings.contains("mass"))
    mass = densityMass;
  require(std::isfinite(mass) && mass > 0 && mass < 1e12,
          "Density-derived fracture mass is outside supported limits");
  J rootComponents = {
      {"Transform3D", J::object()},
      {"ModelCollider3D", {{"inline_geometry", collider}}},
      {"Rigidbody3D",
       {{"body_type", anchors.empty() ? "dynamic" : "static"}, {"mass", mass}}},
      {"Destructible3D", {{"parts", mapping}, {"max_bodies", maxBodies},{"fading_parts",fading}}}};
  output.insert(output.begin(),
                J{{"id", "body"}, {"components", rootComponents}});
  return {{"entities", output},
          {"generation",
           {{"generator_version", 1},
            {"content_hash", digest(settings.dump() + output.dump())},
            {"chunks", chunks.size()},
            {"sources", sources},
            {"bonds", bonds.size()}}}};
}
} // namespace demi::assets
