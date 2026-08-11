#include "demi/assets/SceneBudget3D.h"

#include "demi/assets/AssetRegistry.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

namespace demi::assets {
namespace {

using Json = nlohmann::json;

struct Counts {
  std::uint64_t visibleInstances = 0;
  std::uint64_t triangles = 0;
  std::uint64_t textureBytes = 0;
  std::uint64_t lights = 0;
  std::uint64_t shadowLights = 0;
  std::uint64_t transparentDraws = 0;
  std::set<std::string> uniqueMeshes;
};

Json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("Could not open JSON document.");
  return Json::parse(input);
}

std::uint32_t bigEndian32(const unsigned char *bytes) {
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) | bytes[3];
}

std::uint64_t estimatedTextureBytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::array<unsigned char, 24> header{};
  input.read(reinterpret_cast<char *>(header.data()), header.size());
  constexpr std::array<unsigned char, 8> PngSignature{
      137, 80, 78, 71, 13, 10, 26, 10};
  if (input.gcount() == static_cast<std::streamsize>(header.size()) &&
      std::equal(PngSignature.begin(), PngSignature.end(), header.begin())) {
    const std::uint64_t width = bigEndian32(header.data() + 16);
    const std::uint64_t height = bigEndian32(header.data() + 20);
    return width * height * 4U;
  }
  std::error_code error;
  const auto bytes = std::filesystem::file_size(path, error);
  return error ? 0U : bytes * 4U;
}

std::uint64_t primitiveTriangles(const std::string &shape) {
  if (shape == "cube" || shape == "plane")
    return shape == "cube" ? 12U : 2U;
  if (shape == "sphere")
    return 512U;
  if (shape == "capsule" || shape == "cylinder")
    return 256U;
  return 0U;
}

void inspectEntity(const Json &entity, Counts &counts) {
  if (entity.value("enabled", true) == false)
    return;
  const Json &components = entity.value("components", Json::object());
  if (const auto mesh = components.find("MeshRenderer");
      mesh != components.end() && mesh->is_object()) {
    ++counts.visibleInstances;
    const std::string model = mesh->value("model", "");
    const std::string shape = mesh->value("shape", "cube");
    const std::string material = mesh->value("material", "");
    counts.uniqueMeshes.insert(model.empty() ? "shape:" + shape + ":" + material
                                             : model + ":" + material);
    if (model.empty())
      counts.triangles += primitiveTriangles(shape);
    const Json color = mesh->value("color", Json::array());
    if (color.is_array() && color.size() >= 4U && color[3].is_number() &&
        color[3].get<float>() < 0.999F)
      ++counts.transparentDraws;
  }
  for (const char *type : {"DirectionalLight", "PointLight", "SpotLight"}) {
    const auto light = components.find(type);
    if (light == components.end())
      continue;
    ++counts.lights;
    if (light->is_object() && light->value("casts_shadows", false))
      ++counts.shadowLights;
  }
  if (components.contains("ParticleEmitter3D"))
    ++counts.transparentDraws;
}

std::uint64_t limit(const Json &budgets, const char *name,
                    const std::uint64_t fallback) {
  const auto value = budgets.find(name);
  return value != budgets.end() && value->is_number_unsigned()
             ? value->get<std::uint64_t>()
             : value != budgets.end() && value->is_number_integer() &&
                       value->get<std::int64_t>() > 0
                   ? static_cast<std::uint64_t>(value->get<std::int64_t>())
                   : fallback;
}

void compare(Diagnostics &diagnostics, const std::filesystem::path &path,
             const char *code, const char *label, const std::uint64_t actual,
             const std::uint64_t maximum) {
  if (actual <= maximum)
    return;
  diagnostics.push_back(
      {.severity = Severity::Warning,
       .code = code,
       .message = std::string(label) + " estimate " + std::to_string(actual) +
                  " exceeds the declared budget " +
                  std::to_string(maximum) + ".",
       .path = path.string(),
       .suggestion = "Inspect the scene in the matching diagnostic render mode "
                     "and reduce or batch the reported resource."});
}

} // namespace

SceneBudget3DReport inspectSceneBudget3D(
    const std::filesystem::path &projectFile, const std::string &platform) {
  SceneBudget3DReport report;
  try {
    const Json project = readJson(projectFile);
    const Json budgets = project.value("performance_budgets", Json::object());
    const bool mobile = platform == "android";
    const Json limits{
        {"visible_instances", limit(budgets, "maximum_visible_instances",
                                    mobile ? 1000U : 5000U)},
        {"unique_meshes", limit(budgets, "maximum_unique_meshes",
                                mobile ? 128U : 512U)},
        {"triangles", limit(budgets, "maximum_triangles",
                            mobile ? 250000U : 2000000U)},
        {"texture_memory_bytes",
         limit(budgets, "maximum_texture_memory_mb", mobile ? 128U : 512U) *
             1024U * 1024U},
        {"lights", limit(budgets, "maximum_lights", mobile ? 8U : 32U)},
        {"shadow_lights",
         limit(budgets, "maximum_shadow_lights", mobile ? 1U : 4U)},
        {"transparent_draws",
         limit(budgets, "maximum_transparent_draws", mobile ? 128U : 1000U)}};
    Counts counts;
    for (const Json &scene : project.value("scenes", Json::array())) {
      const std::filesystem::path path =
          projectFile.parent_path() / scene.value("path", "");
      const Json document = readJson(path);
      for (const Json &entity : document.value("entities", Json::array()))
        inspectEntity(entity, counts);
    }
    for (const AssetManifest &asset :
         loadAssetRegistry(projectFile.parent_path()).assets)
      if (asset.type == "Texture2D")
        counts.textureBytes += estimatedTextureBytes(asset.sourcePath);
    const Json observed{{"visible_instances", counts.visibleInstances},
                        {"unique_meshes", counts.uniqueMeshes.size()},
                        {"triangles", counts.triangles},
                        {"texture_memory_bytes", counts.textureBytes},
                        {"lights", counts.lights},
                        {"shadow_lights", counts.shadowLights},
                        {"transparent_draws", counts.transparentDraws}};
    report.document = {{"format_version", 1},
                       {"platform", platform},
                       {"observed", observed},
                       {"limits", limits},
                       {"notes", Json::array(
                                     {"Model triangle counts are reported by "
                                      "`demi asset inspect`; scene totals count "
                                      "procedural primitives conservatively.",
                                      "Texture memory assumes decoded RGBA8 for "
                                      "PNG and estimates other formats from "
                                      "source size."})}};
    compare(report.diagnostics, projectFile, "BUDGET_VISIBLE_INSTANCES_EXCEEDED",
            "Visible instances", counts.visibleInstances,
            limits["visible_instances"]);
    compare(report.diagnostics, projectFile, "BUDGET_UNIQUE_MESHES_EXCEEDED",
            "Unique meshes", counts.uniqueMeshes.size(),
            limits["unique_meshes"]);
    compare(report.diagnostics, projectFile, "BUDGET_TRIANGLES_EXCEEDED",
            "Procedural triangles", counts.triangles, limits["triangles"]);
    compare(report.diagnostics, projectFile, "BUDGET_TEXTURE_MEMORY_EXCEEDED",
            "Texture memory bytes", counts.textureBytes,
            limits["texture_memory_bytes"]);
    compare(report.diagnostics, projectFile, "BUDGET_LIGHTS_EXCEEDED", "Lights",
            counts.lights, limits["lights"]);
    compare(report.diagnostics, projectFile, "BUDGET_SHADOW_LIGHTS_EXCEEDED",
            "Shadow lights", counts.shadowLights, limits["shadow_lights"]);
    compare(report.diagnostics, projectFile, "BUDGET_TRANSPARENT_DRAWS_EXCEEDED",
            "Transparent draws", counts.transparentDraws,
            limits["transparent_draws"]);
  } catch (const std::exception &exception) {
    report.diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "SCENE_BUDGET_INSPECTION_FAILED",
         .message = exception.what(),
         .path = projectFile.string(),
         .suggestion = "Validate the project and scene JSON before inspecting "
                       "its 3D budget."});
  }
  return report;
}

} // namespace demi::assets
