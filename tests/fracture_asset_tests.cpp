#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/ColliderShapeAsset.h"
#include "demi/runtime/destruction/ColliderFractureFamily3D.h"
#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/physics/ColliderAssetLoader3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/schema/Validation.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
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
void write(const std::filesystem::path &path, const Json &value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2);
  expect(bool(output), "Fixture write failed");
}
Json source() {
  return {{"format_version", 1},
          {"shape", "compound"},
          {"parts",
           {{{"id", "base"},
             {"points", {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}},
            {{"id", "cap"},
             {"points", {{0, 1, 0}, {1, 1, 0}, {0, 2, 0}, {0, 1, 1}}}}}},
          {"fracture",
           {{"anchors", {"base"}},
            {"bonds", {{{"id", "joint"}, {"parts", {"base", "cap"}}}}}}}};
}
void parsing() {
  std::string error;
  const auto parsed = assets::parseColliderShapeAsset(source(), error);
  expect(parsed && parsed->fracture && parsed->fracture->bonds.size() == 1 &&
             parsed->fracture->bonds[0].health == 1,
         "Fracture defaults failed");
  auto reordered = source();
  std::swap(reordered["parts"][0], reordered["parts"][1]);
  std::swap(reordered["fracture"]["bonds"][0]["parts"][0],
            reordered["fracture"]["bonds"][0]["parts"][1]);
  const auto reorderedParsed = assets::parseColliderShapeAsset(reordered, error);
  expect(reorderedParsed && reorderedParsed->fracture == parsed->fracture,
         "Reordered source changed graph identity");
  for (int test = 0; test < 15; ++test) {
    auto bad = source();
    auto &graph = bad["fracture"];
    switch (test) {
    case 0:
      graph = nullptr;
      break;
    case 1:
      graph["bonds"] = Json::array();
      break;
    case 2:
      graph["anchors"] = {"missing"};
      break;
    case 3:
      graph["anchors"] = {"base", "base"};
      break;
    case 4:
      graph["bonds"][0]["parts"] = {"base", "missing"};
      break;
    case 5:
      graph["bonds"][0]["parts"] = {"base", "base"};
      break;
    case 6:
      graph["bonds"][0]["health"] = -1;
      break;
    case 7:
      graph["bonds"][0]["health"] = 1e100;
      break;
    case 8:
      graph["bonds"][0]["id"] = "../bad";
      break;
    case 9:
      graph["extra"] = true;
      break;
    case 10:
      graph["bonds"][0]["extra"] = true;
      break;
    case 11:
      graph["bonds"].push_back(graph["bonds"][0]);
      break;
    case 12:
      graph["bonds"].push_back({{"id", "other"}, {"parts", {"cap", "base"}}});
      break;
    case 13:
      graph["bonds"][0]["health"] = "1";
      break;
    case 14:
      graph["bonds"][0]["parts"] = {"base"};
      break;
    }
    expect(!assets::parseColliderShapeAsset(bad, error) && !error.empty(),
           "Malformed fracture source accepted");
  }
  auto single = source();
  single["parts"].erase(1);
  single["fracture"]["bonds"] = Json::array();
  expect(bool(assets::parseColliderShapeAsset(single, error)),
         "Single anchored chunk rejected");
  auto convex = source();
  convex["shape"] = "convex_hull";
  convex["points"] = convex["parts"][0]["points"];
  convex.erase("parts");
  expect(!assets::parseColliderShapeAsset(convex, error),
         "Fracture allowed on non-compound source");
}

void pipeline(const std::filesystem::path &root) {
  const auto project = root / "project";
  write(project / "demi.project.json",
        {{"format_version", 1},
         {"name", "Fracture pipeline"},
         {"main_scene", "scene://fracture/main"},
         {"assets", {"asset://colliders/fracture"}},
         {"scenes",
          {{{"id", "scene://fracture/main"},
            {"path", "scenes/main.scene.json"}}}}});
  write(project / "scenes/main.scene.json", {{"format_version", 1},
                                             {"id", "scene://fracture/main"},
                                             {"entities", Json::array()}});
  const auto path = root / "assembly.collider.json";
  write(path, source());
  auto imported = assets::importAsset({.projectDirectory = project,
                                       .source = path,
                                       .id = "asset://colliders/fracture"});
  expect(!hasErrors(imported.diagnostics), "Fracture import failed");
  auto manifest = loadAssetManifest(imported.manifestPath);
  expect(manifest && !manifest->generatedOutputPath,
         "Fracture generated an unwanted cache mirror");
  expect(!hasErrors(validatePath(project).diagnostics),
         "Fracture project invalid");
  std::string error;
  auto collider = loadColliderAsset3D(*manifest, error);
  expect(collider && collider->fracture, "Runtime loader dropped graph");
  auto family = createColliderFractureFamily3D(*collider, error);
  expect(bool(family), error.c_str());
  expect(std::abs(family->chunks()[0].volume - 1.0F / 6.0F) < 0.001F &&
             std::abs(family->chunks()[0].centroid[0] - 0.25F) < 0.001F &&
             std::abs(family->chunks()[0].centroid[1] - 0.25F) < 0.001F,
         "Fracture preparation used bounds instead of hull volume/centroid");
  const BondDamage3D hit{"joint", 0.6F};
  auto token = family->stage(std::span(&hit, 1));
  expect(family->commit(token), "First hit failed");
  token = family->stage(std::span(&hit, 1));
  const std::vector<DestructionGroup3D> split{{{"base"}, true},
                                              {{"cap"}, false}};
  expect(family->stagedGroups(token) == split,
         "Imported fracture did not produce stable anchored groups");
  expect(family->discard(token), "Imported fracture cancellation failed");

  // Nested off-world shape preparation must not tear down an existing world.
  {
    PhysicsWorld3D physics;
    auto independent = createColliderFractureFamily3D(*collider, error);
    expect(independent && independent->revision() == 0,
           "Instances shared damage or Jolt lifetime failed");
  }
  World world;
  auto loader = createColliderAssetLoader3D(world);
  std::atomic_bool cancelled{false};
  auto decoded = loader->readAndDecode(*manifest, cancelled, error);
  expect(decoded && loader->upload(*manifest, *decoded, error),
         "Fracture asset-service upload failed");
  expect(world.colliderAssets3D.at(manifest->id).fracture == collider->fracture,
         "Residency lost graph metadata");
  const auto oldHash = manifest->sourceHash;
  auto bad = source();
  bad["fracture"]["anchors"] = {"missing"};
  write(manifest->sourcePath, bad);
  expect(hasErrors(assets::reimportAsset(imported.manifestPath)),
         "Invalid fracture reimport accepted");
  expect(loadAssetManifest(imported.manifestPath)->sourceHash == oldHash,
         "Invalid reimport changed manifest hash");
  expect(hasErrors(validatePath(project).diagnostics),
         "Registry did not validate malformed graph");
  const auto cooked = root / "cooked";
  expect(hasErrors(
             assets::cookProject({.projectFile = project / "demi.project.json",
                                  .outputDirectory = cooked,
                                  .platform = "linux"})),
         "Cook accepted malformed graph");
  auto stronger = source();
  stronger["fracture"]["bonds"][0]["health"] = 5;
  write(manifest->sourcePath, stronger);
  expect(!hasErrors(assets::reimportAsset(imported.manifestPath)),
         "Valid reimport failed");
  manifest = loadAssetManifest(imported.manifestPath);
  expect(manifest->sourceHash != oldHash,
         "Graph edit did not invalidate source hash");
  decoded = loader->readAndDecode(*manifest, cancelled, error);
  expect(decoded && loader->upload(*manifest, *decoded, error),
         "Graph reload failed");
  auto replacement = createColliderFractureFamily3D(
      world.colliderAssets3D.at(manifest->id), error);
  expect(bool(replacement), error.c_str());
  token = replacement->stage(std::span(&hit, 1));
  expect(replacement->stagedGroups(token).size() == 1,
         "Reload ignored stronger bond");
  // Reload/unload must not mutate existing independent family snapshots.
  loader->unload(manifest->id);
  expect(world.colliderAssets3D.empty(),
         "Unused fracture asset failed to unload");
  token = family->stage(std::span(&hit, 1));
  expect(family->stagedGroups(token) == split,
         "Reload changed an existing family's health");
  expect(!hasErrors(
             assets::cookProject({.projectFile = project / "demi.project.json",
                                  .outputDirectory = cooked,
                                  .platform = "linux"})),
         "Fracture cook failed");
  expect(!hasErrors(validatePath(cooked).diagnostics),
         "Cooked fracture invalid");
  expect(!std::filesystem::exists(cooked / "generated"),
         "Cook leaked generated caches");
  const auto cookedManifest = loadAssetManifest(
      cooked / "assets/colliders/fracture/assembly.collider.asset.json");
  expect(cookedManifest && cookedManifest->sourceHash == manifest->sourceHash,
         "Cooked manifest missing or source hash changed");
  const auto cookedCollider = loadColliderAsset3D(*cookedManifest, error);
  expect(cookedCollider && cookedCollider->fracture->bonds[0].health == 5,
         "Cook lost authored bond health");
}
} // namespace
int main() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-fracture-asset-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(root))
    return 1;
  try {
    parsing();
    pipeline(root);
  } catch (const std::exception &error) {
    std::cerr << error.what() << " (fixtures: " << root << ")\n";
    return 1;
  }
  std::filesystem::remove_all(root);
  std::cout << "Fracture asset pipeline passed\n";
}
