#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/ColliderAssetGenerator.h"
#include "demi/assets/ModelImportProfile.h"
#include "demi/assets/ModelInspector.h"
#include "demi/assets/SceneBudget3D.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <ranges>

using namespace demi;

namespace {

void writeText(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
}

void writeBytes(const std::filesystem::path &path,
                const std::vector<unsigned char> &bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

nlohmann::json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}

bool hasCode(const Diagnostics &diagnostics, const std::string &code) {
  return std::ranges::any_of(diagnostics, [&](const Diagnostic &diagnostic) {
    return diagnostic.code == code;
  });
}

bool close(const float left, const float right) {
  return std::abs(left - right) < 0.0001F;
}

} // namespace

int main() {
  using namespace demi::assets;
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("demi_lightweight_3d_" + std::to_string(nonce));
  const auto project = root / "project";
  const auto source = root / "source";
  writeText(project / "demi.project.json",
            R"({"format_version":1,"name":"3D","scenes":[]})");

  ModelImportProfile centimeters = modelImportPreset("static_prop");
  centimeters.sourceUp = "+z";
  centimeters.sourceForward = "+y";
  centimeters.metersPerUnit = 0.01F;
  Diagnostics profileDiagnostics;
  const auto parsed = parseModelImportProfile(
      modelImportProfileJson(centimeters), &profileDiagnostics, "fixture");
  const auto conversion = parsed ? modelImportConversion(*parsed)
                                 : std::array<float, 16>{};
  if (!parsed || hasErrors(profileDiagnostics) || !close(conversion[0], -0.01F) ||
      !close(conversion[6], 0.01F) || !close(conversion[9], 0.01F)) {
    std::cerr << "Signed-axis/unit conversion was not deterministic.\n";
    return 1;
  }
  auto invalid = modelImportProfileJson(centimeters);
  invalid["source_forward"] = "+z";
  invalid["meters_per_unit"] = -1.0;
  profileDiagnostics.clear();
  if (parseModelImportProfile(invalid, &profileDiagnostics, "fixture") ||
      !hasCode(profileDiagnostics, "MODEL_IMPORT_PROFILE_INVALID")) {
    std::cerr << "Unsafe import profiles were not rejected.\n";
    return 1;
  }

  std::vector<unsigned char> geometry(42U);
  const float positions[]{-1.0F, -1.0F, 0.0F, 1.0F, -1.0F,
                          0.0F,  0.0F,  1.0F, 0.0F};
  const std::uint16_t indices[]{0, 1, 2};
  std::memcpy(geometry.data(), positions, sizeof(positions));
  std::memcpy(geometry.data() + sizeof(positions), indices, sizeof(indices));
  writeBytes(source / "geometry.bin", geometry);
  writeText(source / "model.gltf", R"({
    "asset":{"version":"2.0"},"scene":0,
    "scenes":[{"nodes":[0]}],
    "nodes":[{"name":"Mirrored Root","mesh":0,"scale":[-1,2,1]}],
    "buffers":[{"uri":"geometry.bin","byteLength":42}],
    "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
                   {"buffer":0,"byteOffset":36,"byteLength":6}],
    "meshes":[{"name":"Triangle","primitives":[{
      "attributes":{"POSITION":0},"indices":1}]}],
    "accessors":[
      {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
       "min":[-1,-1,0],"max":[1,1,0]},
      {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]
  })");
  const auto imported = importAsset({.projectDirectory = project,
                                     .source = source / "model.gltf",
                                     .id = "asset://models/triangle",
                                     .modelProfile = centimeters});
  const auto manifest = loadAssetManifest(imported.manifestPath);
  if (hasErrors(imported.diagnostics) || !manifest) {
    std::cerr << "Profiled model import failed.\n";
    return 1;
  }
  const auto importedDocument = readJson(imported.manifestPath);
  if (!close(importedDocument["settings"]["model_import"]["meters_per_unit"]
                 .get<float>(),
             0.01F)) {
    std::cerr << "Import profile was not persisted.\n";
    return 1;
  }
  const auto inspection = inspectModel({.asset = &*manifest});
  if (hasErrors(inspection.diagnostics) ||
      !inspection.document.contains("bounds") ||
      !inspection.document.contains("skeletons") ||
      !hasCode(inspection.diagnostics, "MODEL_NEGATIVE_SCALE") ||
      !hasCode(inspection.diagnostics, "MODEL_NON_UNIFORM_SCALE") ||
      !hasCode(inspection.diagnostics, "MODEL_NORMALS_MISSING") ||
      !hasCode(inspection.diagnostics, "MODEL_UV_MISSING")) {
    std::cerr << "Structured model inspection omitted edge diagnostics.\n";
    return 1;
  }

  for (const auto &[body, shape] :
       std::array<std::pair<const char *, const char *>, 4>{
           {{"static", "triangle_mesh"},
            {"dynamic", "convex_hull"},
            {"trigger", "box"},
            {"character", "capsule"}}}) {
    Diagnostics diagnostics;
    const auto recommendation =
        recommendCollider(imported.manifestPath, body, diagnostics);
    if (!recommendation || recommendation->shape != shape ||
        hasErrors(diagnostics)) {
      std::cerr << "Collider recommendation failed for " << body << ".\n";
      return 1;
    }
  }
  Diagnostics bodyDiagnostics;
  if (recommendCollider(imported.manifestPath, "vehicle", bodyDiagnostics) ||
      !hasCode(bodyDiagnostics, "COLLIDER_BODY_INVALID")) {
    std::cerr << "Unknown collider intent was silently guessed.\n";
    return 1;
  }
  const auto preview = project / "generated/collider.scene.json";
  const auto collider = generateColliderAsset(
      {.projectDirectory = project,
       .modelManifestPath = imported.manifestPath,
       .id = "asset://colliders/triangle",
       .detail = 1.0F,
       .body = "static",
       .previewPath = preview});
  if (hasErrors(collider.diagnostics) || !std::filesystem::exists(preview) ||
      readJson(collider.manifestPath)["generation"]["model_import"]
              ["source_up"] != "+z") {
    std::cerr << "Collider generation/preview lost provenance.\n";
    return 1;
  }
  const auto unsafe = generateColliderAsset(
      {.projectDirectory = project,
       .modelManifestPath = imported.manifestPath,
       .id = "asset://colliders/dynamic",
       .detail = 1.0F,
       .body = "dynamic"});
  const auto invalidDetail = generateColliderAsset(
      {.projectDirectory = project,
       .modelManifestPath = imported.manifestPath,
       .id = "asset://colliders/nan",
       .detail = std::numeric_limits<float>::quiet_NaN()});
  if (!hasCode(unsafe.diagnostics, "COLLIDER_ASSET_BODY_UNSAFE") ||
      !hasCode(invalidDetail.diagnostics, "COLLIDER_DETAIL_INVALID")) {
    std::cerr << "Unsafe collider generation was not rejected.\n";
    return 1;
  }
  writeText(source / "model.gltf", readJson(source / "model.gltf").dump() + "\n");
  writeText(manifest->sourcePath,
            readJson(manifest->sourcePath).dump(1) + "\n");
  if (hasErrors(reimportAsset(imported.manifestPath)) ||
      !hasCode(validateAssetRegistry(loadAssetRegistry(project)),
               "COLLIDER_GENERATION_STALE")) {
    std::cerr << "Collider source changes did not invalidate generated data.\n";
    return 1;
  }

  const auto budgetProject = root / "budget";
  writeText(budgetProject / "demi.project.json", R"({
    "format_version":1,"name":"Budget","main_scene":"scene://main",
    "performance_budgets":{"maximum_visible_instances":1,
      "maximum_unique_meshes":1,"maximum_lights":1,
      "maximum_shadow_lights":1,"maximum_transparent_draws":1},
    "scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]
  })");
  writeText(budgetProject / "scenes/main.scene.json", R"({
    "format_version":1,"id":"scene://main","entities":[
      {"id":"a","components":{"MeshRenderer":{"shape":"cube"},
       "DirectionalLight":{"casts_shadows":true}}},
      {"id":"b","components":{"MeshRenderer":{"shape":"sphere",
       "color":[1,1,1,0.5]},"PointLight":{"casts_shadows":true}}},
      {"id":"c","components":{"ParticleEmitter3D":{}}}
    ]})");
  const auto budget = inspectSceneBudget3D(
      budgetProject / "demi.project.json", "android");
  if (hasErrors(budget.diagnostics) ||
      !hasCode(budget.diagnostics, "BUDGET_VISIBLE_INSTANCES_EXCEEDED") ||
      !hasCode(budget.diagnostics, "BUDGET_LIGHTS_EXCEEDED") ||
      !hasCode(budget.diagnostics, "BUDGET_SHADOW_LIGHTS_EXCEEDED") ||
      !hasCode(budget.diagnostics, "BUDGET_TRANSPARENT_DRAWS_EXCEEDED")) {
    std::cerr << "Mobile scene budgets did not report exceeded limits.\n";
    return 1;
  }

  std::filesystem::remove_all(root);
  return 0;
}
