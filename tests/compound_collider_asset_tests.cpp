#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/ColliderShapeAsset.h"
#include "demi/runtime/physics/ColliderAssetLoader3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/bgfx3d/DebugGeometry3D.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/schema/Validation.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace demi;
using namespace demi::runtime;
using Json = nlohmann::json;
namespace {
void expect(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
std::string partAt(PhysicsWorld3D &physics, Vec3 origin) {
  auto hit = physics.raycast(origin, {0, 0, -1}, 10);
  expect(hit.has_value(), "Expected a native compound hit");
  return hit->colliderPartId;
}
void write(const std::filesystem::path &path, const Json &value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2);
  expect(bool(output), "Fixture write failed");
}
Json box(std::string id, Vec3 min, Vec3 max) {
  Json points = Json::array();
  for (float x : {min.x, max.x})
    for (float y : {min.y, max.y})
      for (float z : {min.z, max.z})
        points.push_back({x, y, z});
  return {{"id", id}, {"points", points}};
}
Json arch() {
  return {
      {"format_version", 1},
      {"shape", "compound"},
      {"parts", Json::array({box("left", {-2, 0, -0.5F}, {-1, 3, 0.5F}),
                             box("right", {1, 0, -0.5F}, {2, 3, 0.5F}),
                             box("lintel", {-2, 3, -0.5F}, {2, 4, 0.5F})})}};
}
void parsing() {
  std::string error;
  const auto parsed = assets::parseColliderShapeAsset(arch(), error);
  expect(parsed && parsed->points.empty() && parsed->parts.size() == 3 &&
             parsed->minimum[0] == -2 && parsed->maximum[1] == 4,
         "Compound bounds/parts invalid");
  for (int test = 0; test < 9; ++test) {
    auto bad = arch();
    switch (test) {
    case 0:
      bad["parts"] = Json::array();
      break;
    case 1:
      bad["parts"][1]["id"] = "left";
      break;
    case 2:
      bad["parts"][0]["id"] = "../left";
      break;
    case 3:
      bad["parts"][0]["id"] = "";
      break;
    case 4:
      bad["parts"][0]["parts"] = Json::array();
      break;
    case 5:
      bad["parts"][0]["points"] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
      break;
    case 6:
      bad["points"] = bad["parts"][0]["points"];
      break;
    case 7:
      bad["format_version"] = 1.5;
      break;
    case 8:
      bad["parts"][0]["id"] = std::string(129, 'x');
      break;
    }
    expect(!assets::parseColliderShapeAsset(bad, error) && !error.empty(),
           "Malformed compound accepted");
  }
}
void pipeline(const std::filesystem::path &root) {
  const auto project = root / "project";
  const auto source = root / "arch.collider.json";
  write(source, arch());
  write(
      project / "demi.project.json",
      {{"format_version", 1},
       {"name", "Compound probe"},
       {"main_scene", "scene://test/main"},
       {"scenes",
        {{{"id", "scene://test/main"}, {"path", "scenes/main.scene.json"}}}}});
  write(project / "scenes/main.scene.json",
        {{"format_version", 1},
         {"id", "scene://test/main"},
         {"entities",
          {{{"id", "arch"},
            {"components",
             {{"Transform3D", Json::object()},
              {"ModelCollider3D", {{"asset", "asset://colliders/arch"}}},
              {"Rigidbody3D",
               {{"body_type", "dynamic"},
                {"mass", 20},
                {"use_gravity", false},
                {"linear_damping", 0}}}}}}}}});
  const auto imported = assets::importAsset({.projectDirectory = project,
                                             .source = source,
                                             .id = "asset://colliders/arch"});
  expect(!hasErrors(imported.diagnostics), "Compound import failed");
  expect(!hasErrors(validatePath(project).diagnostics),
         "Compound project validation failed");
  auto manifest = loadAssetManifest(imported.manifestPath);
  expect(bool(manifest), "Missing compound manifest");
  expect(!manifest->generatedOutputPath, "Collider import created an unnecessary cache mirror");
  {
    Json legacy;
    std::ifstream input(imported.manifestPath);
    input >> legacy;
    legacy["generated_output"] = "../../../generated/legacy.collider.json";
    write(project / "generated/legacy.collider.json", arch());
    write(imported.manifestPath, legacy);
    expect(!hasErrors(assets::reimportAsset(imported.manifestPath)), "Legacy collider reimport failed");
    manifest = loadAssetManifest(imported.manifestPath);
    expect(manifest && !manifest->generatedOutputPath &&
           std::filesystem::exists(project / "generated/legacy.collider.json"),
           "Legacy mirror reference was not retired safely");
    auto bad = arch();
    bad["parts"][0]["points"] = Json::array();
    write(manifest->sourcePath, bad);
    expect(hasErrors(assets::reimportAsset(imported.manifestPath)), "Malformed compound reimport accepted");
    write(manifest->sourcePath, arch());
    expect(!hasErrors(assets::reimportAsset(imported.manifestPath)), "Valid collider restore failed");
  }
  std::string error;
  auto loaded = loadProject(project / "demi.project.json", error);
  expect(bool(loaded), error.c_str());
  auto &world = loaded->world;
  auto loader = createColliderAssetLoader3D(world);
  std::atomic_bool cancelled{false};
  auto decoded = loader->readAndDecode(*manifest, cancelled, error);
  expect(decoded && loader->upload(*manifest, *decoded, error),
         "Compound upload failed");
  expect(!raycast3D(world, {0, 1, 5}, {0, 0, -1}, 10),
         "Cold query fabricated a hit against compound bounds");
  auto &physics = ensurePhysicsWorld3D(world);
  RuntimeProfiler::setEnabled(true);
  RuntimeProfiler::beginFrame();
  physics.step(world, 1.0F / 60);
  bool oneBody = false;
  for (const auto &entry : RuntimeProfiler::frameEntries())
    if (entry.name == "Physics3D.bodies")
      oneBody = entry.gauge == 1;
  expect(oneBody, "Compound incorrectly created multiple bodies");
  expect(!physics.raycast({0, 1, 5}, {0, 0, -1}, 10),
         "Compound hole filled by bounding box");
  expect(physics.overlapBox({0, 1, 0}, {0.2F, 0.2F, 0.2F}).empty(),
         "Compound overlap used bounds");
  const auto left = physics.raycast({-1.5F, 1, 5}, {0, 0, -1}, 10);
  expect(left && left->entityId == "arch" && left->colliderPartId == "left",
         "Missing raycast part identity");
  expect(partAt(physics, {1.5F, 1, 5}) == "right", "Wrong right part identity");
  const auto lines =
      render::buildDebugGeometry3D(world, {.forceColliders = true});
  expect(!lines.empty(), "Missing compound debug geometry");
  for (const auto &line : lines) {
    float x = (line.start.x + line.end.x) * 0.5F;
    float y = (line.start.y + line.end.y) * 0.5F;
    expect(std::abs(x) >= 1 || y >= 3,
           "Debug geometry filled compound opening");
  }
  expect(physics.addImpulse("arch", {20, 0, 0}), "Compound impulse failed");
  const auto velocity = physics.velocity("arch");
  expect(velocity && std::abs(velocity->x - 1) < 0.001F,
         "Authored mass was not applied to the whole compound");
  expect(physics.setVelocity("arch", {}), "Could not reset velocity");

  auto reordered = arch();
  std::swap(reordered["parts"][0], reordered["parts"][2]);
  write(manifest->sourcePath, reordered);
  expect(!hasErrors(assets::reimportAsset(imported.manifestPath)),
         "Compound reimport failed");
  manifest = loadAssetManifest(imported.manifestPath);
  decoded = loader->readAndDecode(*manifest, cancelled, error);
  expect(decoded && loader->upload(*manifest, *decoded, error),
         "Compound reload failed");
  expect(partAt(physics, {-1.5F, 1, 5}) == "left",
         "Reload changed old native part identity before physics step");
  physics.step(world, 1.0F / 60);
  expect(partAt(physics, {-1.5F, 1, 5}) == "left",
         "Reload lost stable part identity");
  auto single = arch();
  single["parts"] = Json::array({single["parts"][0]});
  write(manifest->sourcePath, single);
  expect(!hasErrors(assets::reimportAsset(imported.manifestPath)),
         "Single-part reload failed");
  manifest = loadAssetManifest(imported.manifestPath);
  decoded = loader->readAndDecode(*manifest, cancelled, error);
  expect(decoded && loader->upload(*manifest, *decoded, error),
         "Single-part upload failed");
  physics.step(world, 1.0F / 60);
  expect(partAt(physics, {-1.5F, 1, 5}) == "left", "Single-part identity lost");
  expect(!physics.raycast({1.5F, 1, 5}, {0, 0, -1}, 10),
         "Removed part still collides");
  auto *transform = world.entities.front().component<Transform3DComponent>();
  transform->scale = {-1, 2, 1};
  physics.step(world, 1.0F / 60);
  expect(partAt(physics, {1.5F, 2, 5}) == "left",
         "Signed/nonuniform compound scale incorrect");

  loader->unload(manifest->id);
  physics.step(world, 1.0F / 60);
  expect(physics.raycast({1.5F, 2, 5}, {0, 0, -1}, 10).has_value(),
         "Unload removed live compound collision");
  world.entities.clear();
  physics.step(world, 1.0F / 60);
  expect(world.colliderAssets3D.empty(),
         "Compound snapshot leaked after entity removal");
  RuntimeProfiler::setEnabled(false);
  const auto cooked = root / "cooked";
  expect(!hasErrors(
             assets::cookProject({.projectFile = project / "demi.project.json",
                                  .outputDirectory = cooked,
                                  .platform = "linux"})),
         "Compound cook failed");
  expect(!hasErrors(validatePath(cooked).diagnostics),
         "Cooked compound invalid");
  expect(!std::filesystem::exists(cooked / "generated"),
         "Collider cook included authoring cache instead of runtime assets only");
}
} // namespace
int main() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-compound-asset-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(root))
    return 1;
  try {
    parsing();
    pipeline(root);
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << " (fixtures: " << root << ")\n";
    return 1;
  }
  std::filesystem::remove_all(root);
}
