#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/assets/ColliderShapeAsset.h"
#include "demi/runtime/physics/ColliderAssetLoader3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include "demi/schema/Validation.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace demi;
using namespace demi::runtime;
using Json = nlohmann::json;

namespace {
void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void write(const std::filesystem::path &path, const Json &value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2);
  expect(bool(output), "Fixture write failed");
}
Json tetrahedron() {
  return {{"format_version", 1},
          {"shape", "convex_hull"},
          {"points", {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
}
void parsing() {
  std::string error;
  auto valid = assets::parseColliderShapeAsset(tetrahedron(), error);
  expect(valid && error.empty() && valid->points.size() == 4,
         "Valid hull rejected");
  for (int test = 0; test < 8; ++test) {
    auto bad = tetrahedron();
    switch (test) {
    case 0:
      bad.erase("format_version");
      break;
    case 1:
      bad["format_version"] = 1.5;
      break;
    case 2:
      bad["shape"] = "triangle_mesh";
      break;
    case 3:
      bad["points"][0] = {1, 2};
      break;
    case 4:
      bad["points"][0][0] = 1e100;
      break;
    case 5:
      bad["points"] = {{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 3, 3}};
      break;
    case 6:
      bad["points"] = {{0, 0, 0}, {1, 0, 1}, {0, 1, 1}, {1, 1, 2}};
      break;
    case 7:
      for (int i = 0; i < 253; ++i)
        bad["points"].push_back({0, 0, 0});
      break;
    }
    expect(!assets::parseColliderShapeAsset(bad, error) && !error.empty(),
           "Malformed/degenerate hull accepted");
  }
}

void solverOptions() {
  expect(Rigidbody3DComponent{}.solverVelocitySteps == 0 &&
             Rigidbody3DComponent{}.solverPositionSteps == 0,
         "Solver defaults changed");
  Entity entity;
  Rigidbody3DComponent::parse(
      {{"solver_velocity_steps", 64}, {"solver_position_steps", 16}}, entity);
  const auto *body = entity.component<Rigidbody3DComponent>();
  expect(body && body->solverVelocitySteps == 64 &&
             body->solverPositionSteps == 16,
         "Solver fields did not parse");
  auto scene =
      Json::parse(R"({"format_version":1,"id":"scene://solver/main","entities":[
    {"id":"body","components":{"Transform3D":{},"BoxCollider3D":{},
     "Rigidbody3D":{"body_type":"dynamic","solver_velocity_steps":64,"solver_position_steps":16}}}]})");
  expect(!hasErrors(validateSceneDocument("solver.scene.json", scene)),
         "Valid solver fields rejected");
  for (const Json bad : {Json(-1), Json(129), Json(1.5), Json("many")}) {
    scene["entities"][0]["components"]["Rigidbody3D"]["solver_velocity_steps"] =
        bad;
    expect(hasErrors(validateSceneDocument("solver.scene.json", scene)),
           "Invalid solver field accepted");
  }
}

void pipelineAndLifetime(const std::filesystem::path &root) {
  const auto project = root / "project";
  const auto source = root / "tetra.collider.json";
  write(source, tetrahedron());
  write(project / "demi.project.json",
        {{"format_version", 1},
         {"name", "Collider test"},
         {"main_scene", "scene://test/main"},
         {"scenes",
          {{{"id", "scene://test/main"}, {"path", "scenes/main.scene.json"}}}},
         {"gameplay_model", {{"projection", "3d"}, {"physics", "jolt"}}}});
  const Json components{
      {"Transform3D", Json::object()},
      {"Rigidbody3D", {{"body_type", "dynamic"}, {"use_gravity", false}}},
      {"ModelCollider3D", {{"asset", "asset://colliders/tetra"}}}};
  const Json entity{{"id", "hull"}, {"components", components}};
  write(project / "scenes/main.scene.json",
        {{"format_version", 1},
         {"id", "scene://test/main"},
         {"entities", Json::array({entity})}});
  const auto imported = assets::importAsset({.projectDirectory = project,
                                             .source = source,
                                             .id = "asset://colliders/tetra"});
  expect(!hasErrors(imported.diagnostics), "Collider import failed");
  auto manifest = loadAssetManifest(imported.manifestPath);
  expect(manifest && manifest->type == "Collider3D" &&
             manifest->importer == "collider-shape",
         "Collider suffix was not inferred");
  expect(classifySourceFile(source) == SourceFileKind::ColliderShape &&
             !hasErrors(validatePath(source).diagnostics),
         "Collider source validation failed");
  expect(!hasErrors(validatePath(project).diagnostics),
         "Dynamic collider asset rejected");

  std::string error;
  auto loaded = loadProject(project / "demi.project.json", error);
  expect(bool(loaded), error.c_str());
  World &world = loaded->world;
  auto loader = createColliderAssetLoader3D(world);
  std::atomic_bool cancelled{false};
  auto decoded = loader->readAndDecode(*manifest, cancelled, error);
  expect(decoded && loader->upload(*manifest, *decoded, error),
         "Collider activation failed");
  auto unused = *manifest;
  unused.id = "asset://colliders/unused";
  expect(loader->upload(unused, *decoded, error),
         "Unused collider activation failed");
  loader->unload(unused.id);
  expect(!world.colliderAssets3D.contains(unused.id),
         "Unused collider was retained on unload");
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60.0F, {});
  expect(!raycast3D(world, {0.8F, 2, 0.8F}, {0, -1, 0}, 4),
         "Convex asset incorrectly used its bounding box");
  auto hit = raycast3D(world, {0.1F, 2, 0.1F}, {0, -1, 0}, 4);
  expect(hit && hit->distance > 1, "Convex asset coordinates/offset incorrect");

  auto taller = tetrahedron();
  taller["points"][2][1] = 2;
  write(manifest->sourcePath, taller);
  expect(!hasErrors(assets::reimportAsset(imported.manifestPath)),
         "Collider reimport failed");
  manifest = loadAssetManifest(imported.manifestPath);
  decoded = loader->readAndDecode(*manifest, cancelled, error);
  expect(decoded && loader->upload(*manifest, *decoded, error),
         "Collider reload failed");
  physics.step(world, 1.0F / 60.0F, {});
  hit = raycast3D(world, {0.1F, 2, 0.1F}, {0, -1, 0}, 4);
  expect(hit && hit->distance < 0.6F,
         "Same-ID reload did not rebuild native hull");

  loader->unload(manifest->id);
  physics.step(world, 1.0F / 60.0F, {});
  expect(world.colliderAssets3D.contains(manifest->id) &&
             raycast3D(world, {0.1F, 2, 0.1F}, {0, -1, 0}, 4).has_value(),
         "Unloading removed collision underneath a live body");
  world.entities.clear();
  physics.step(world, 1.0F / 60.0F, {});
  expect(world.colliderAssets3D.empty(),
         "Unused collider snapshot was not retired");
  cancelled = true;
  expect(!loader->readAndDecode(*manifest, cancelled, error),
         "Cancelled collider load succeeded");

  const auto cooked = root / "cooked";
  expect(!hasErrors(
             assets::cookProject({.projectFile = project / "demi.project.json",
                                  .outputDirectory = cooked,
                                  .platform = "linux"})),
         "Collider cooking failed");
  const auto cookedRegistry = loadAssetRegistry(cooked);
  const auto *cookedAsset = findAsset(cookedRegistry, manifest->id);
  expect(cookedAsset && assets::pathIsInside(cooked, cookedAsset->sourcePath) &&
             assets::loadColliderShapeAsset(cookedAsset->sourcePath, error),
         "Cooked collider source missing/invalid");
  expect(!hasErrors(validatePath(cooked).diagnostics),
         "Cooked collider project invalid");
}
} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-collider-source-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(root))
    return 1;
  try {
    parsing();
    solverOptions();
    pipelineAndLifetime(root);
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << " (fixtures: " << root << ")\n";
    return 1;
  }
  std::filesystem::remove_all(root);
  return 0;
}
