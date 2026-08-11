#include "demi/assets/ModelInspector.h"

#include "demi/assets/GltfSkinnedModel.h"
#include "demi/assets/ModelImportProfile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace demi::assets {
namespace {

using Json = nlohmann::json;

void diagnose(Diagnostics &diagnostics, const Severity severity,
              const std::string &code, const std::string &message,
              const std::filesystem::path &path,
              const std::string &suggestion = {}) {
  diagnostics.push_back({.severity = severity,
                         .code = code,
                         .message = message,
                         .path = path.string(),
                         .suggestion = suggestion});
}

std::optional<Json> sourceDocument(const std::filesystem::path &path,
                                   Diagnostics &diagnostics) {
  try {
    if (path.extension() == ".gltf") {
      std::ifstream input(path);
      return Json::parse(input);
    }
    if (path.extension() != ".glb")
      return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                    {});
    const auto value = [&bytes](const std::size_t offset) {
      std::uint32_t result = 0;
      if (offset + sizeof(result) <= bytes.size())
        std::memcpy(&result, bytes.data() + offset, sizeof(result));
      return result;
    };
    constexpr std::uint32_t GltfMagic = 0x46546c67U;
    constexpr std::uint32_t JsonChunk = 0x4e4f534aU;
    if (bytes.size() < 20U || value(0) != GltfMagic || value(4) != 2U ||
        value(12) > bytes.size() - 20U || value(16) != JsonChunk)
      throw std::runtime_error("Invalid GLB header or JSON chunk.");
    return Json::parse(bytes.begin() + 20,
                       bytes.begin() + 20 + value(12));
  } catch (const std::exception &exception) {
    diagnose(diagnostics, Severity::Error, "MODEL_INSPECT_PARSE_FAILED",
             exception.what(), path);
    return std::nullopt;
  }
}

bool selected(const std::set<std::string> &sections,
              const std::string &section) {
  return sections.empty() || sections.contains(section) ||
         sections.contains("all");
}

std::size_t accessorCount(const Json &document, const Json &reference) {
  if (!reference.is_number_integer() || !document.contains("accessors"))
    return 0;
  const int index = reference.get<int>();
  return index >= 0 && static_cast<std::size_t>(index) <
                           document["accessors"].size()
             ? document["accessors"][index].value("count", 0U)
             : 0U;
}

float accessorMaximum(const Json &document, const Json &reference) {
  if (!reference.is_number_integer() || !document.contains("accessors"))
    return 0.0F;
  const int index = reference.get<int>();
  if (index < 0 ||
      static_cast<std::size_t>(index) >= document["accessors"].size())
    return 0.0F;
  const Json &accessor = document["accessors"][index];
  return accessor.contains("max") && accessor["max"].is_array() &&
                 !accessor["max"].empty()
             ? accessor["max"][0].get<float>()
             : 0.0F;
}

std::string name(const Json &value, const std::string &prefix,
                 const std::size_t index) {
  return value.value("name", prefix + std::to_string(index));
}

} // namespace

ModelInspectionReport inspectModel(const ModelInspectionRequest &request) {
  ModelInspectionReport report;
  if (request.asset == nullptr || request.asset->type != "Model3D") {
    diagnose(report.diagnostics, Severity::Error, "MODEL_INSPECT_ASSET_INVALID",
             "Model inspection requires a Model3D asset manifest.",
             request.asset == nullptr ? std::filesystem::path{}
                                      : request.asset->manifestPath);
    return report;
  }
  const AssetManifest &asset = *request.asset;
  const Json settings = Json::parse(asset.settingsJson, nullptr, false);
  const auto profile = parseModelImportProfile(
      settings.is_object() ? settings : Json::object(), &report.diagnostics,
      asset.manifestPath.string());
  if (!profile)
    return report;
  const auto document = sourceDocument(asset.sourcePath, report.diagnostics);
  if (!document)
    return report;

  report.document = {{"format_version", 1},
                     {"asset", asset.id},
                     {"source", asset.sourcePath.generic_string()},
                     {"normalized_space",
                      {{"up", "+y"}, {"forward", "+z"}, {"unit", "meter"}}},
                     {"import_profile", modelImportProfileJson(*profile)}};
  std::size_t triangleCount = 0;
  std::size_t primitiveCount = 0;
  std::size_t textureCount = 0;
  std::size_t jointCount = 0;

  Json nodes = Json::array();
  const Json &sourceNodes = document->value("nodes", Json::array());
  std::vector<int> parents(sourceNodes.size(), -1);
  for (std::size_t parent = 0; parent < sourceNodes.size(); ++parent)
    for (const Json &child : sourceNodes[parent].value("children", Json::array()))
      if (child.is_number_integer() && child.get<int>() >= 0 &&
          static_cast<std::size_t>(child.get<int>()) < parents.size())
        parents[child.get<int>()] = static_cast<int>(parent);
  for (std::size_t index = 0; index < sourceNodes.size(); ++index) {
    const Json &node = sourceNodes[index];
    Json item{{"index", index},
              {"source_name", name(node, "node_", index)},
              {"parent", parents[index]},
              {"mesh", node.value("mesh", -1)},
              {"skin", node.value("skin", -1)}};
    if (node.contains("translation"))
      item["translation"] = node["translation"];
    if (node.contains("rotation"))
      item["rotation"] = node["rotation"];
    if (node.contains("scale")) {
      item["scale"] = node["scale"];
      if (node["scale"].is_array() && node["scale"].size() == 3) {
        const float x = node["scale"][0].get<float>();
        const float y = node["scale"][1].get<float>();
        const float z = node["scale"][2].get<float>();
        if (x < 0.0F || y < 0.0F || z < 0.0F)
          diagnose(report.diagnostics, Severity::Warning,
                   "MODEL_NEGATIVE_SCALE",
                   "Node has mirrored/negative scale: " +
                       item["source_name"].get<std::string>(),
                   asset.sourcePath,
                   "Confirm winding, normals, and collider behavior.");
        if (std::abs(x - y) > 0.0001F || std::abs(x - z) > 0.0001F)
          diagnose(report.diagnostics, Severity::Warning,
                   "MODEL_NON_UNIFORM_SCALE",
                   "Node has non-uniform scale: " +
                       item["source_name"].get<std::string>(),
                   asset.sourcePath,
                   "Bake scale before generating a mesh collider.");
      }
    }
    nodes.push_back(std::move(item));
  }
  if (selected(request.sections, "nodes"))
    report.document["nodes"] = std::move(nodes);

  Json meshes = Json::array();
  const Json &sourceMeshes = document->value("meshes", Json::array());
  for (std::size_t meshIndex = 0; meshIndex < sourceMeshes.size();
       ++meshIndex) {
    const Json &mesh = sourceMeshes[meshIndex];
    Json primitives = Json::array();
    std::size_t meshTriangles = 0;
    for (const Json &primitive : mesh.value("primitives", Json::array())) {
      ++primitiveCount;
      const int mode = primitive.value("mode", 4);
      const Json attributes = primitive.value("attributes", Json::object());
      const std::size_t indices = primitive.contains("indices")
                                      ? accessorCount(*document,
                                                      primitive["indices"])
                                      : accessorCount(
                                            *document,
                                            attributes.value("POSITION", -1));
      const std::size_t triangles = mode == 4 ? indices / 3U : 0U;
      meshTriangles += triangles;
      triangleCount += triangles;
      Json attributeNames = Json::array();
      for (const auto &[attribute, accessor] : attributes.items()) {
        static_cast<void>(accessor);
        attributeNames.push_back(attribute);
      }
      primitives.push_back({{"mode", mode},
                            {"triangles", triangles},
                            {"material", primitive.value("material", -1)},
                            {"attributes", attributeNames}});
      if (mode != 4)
        diagnose(report.diagnostics, Severity::Warning,
                 "MODEL_PRIMITIVE_MODE_UNSUPPORTED",
                 "A primitive is not a triangle list and will be skipped.",
                 asset.sourcePath);
      if (!attributes.contains("NORMAL"))
        diagnose(report.diagnostics, Severity::Warning,
                 "MODEL_NORMALS_MISSING", "A primitive has no normals.",
                 asset.sourcePath,
                 "Generate normals during authoring or use an unlit material.");
      if (!attributes.contains("TANGENT"))
        diagnose(report.diagnostics, Severity::Warning,
                 "MODEL_TANGENTS_MISSING", "A primitive has no tangents.",
                 asset.sourcePath,
                 "Tangents are required by normal-mapped materials.");
      if (!attributes.contains("TEXCOORD_0"))
        diagnose(report.diagnostics, Severity::Warning,
                 "MODEL_UV_MISSING", "A primitive has no TEXCOORD_0 data.",
                 asset.sourcePath);
    }
    meshes.push_back({{"index", meshIndex},
                      {"source_name", name(mesh, "mesh_", meshIndex)},
                      {"triangles", meshTriangles},
                      {"primitives", primitives}});
  }
  if (selected(request.sections, "meshes"))
    report.document["meshes"] = std::move(meshes);

  Json materials = Json::array();
  std::map<std::string, int> materialNames;
  const Json &sourceMaterials = document->value("materials", Json::array());
  for (std::size_t index = 0; index < sourceMaterials.size(); ++index) {
    const Json &material = sourceMaterials[index];
    const std::string sourceName = name(material, "material_", index);
    ++materialNames[sourceName];
    materials.push_back({{"index", index},
                         {"source_name", sourceName},
                         {"alpha_mode", material.value("alphaMode", "OPAQUE")},
                         {"double_sided", material.value("doubleSided", false)}});
  }
  for (const auto &[materialName, count] : materialNames)
    if (count > 1)
      diagnose(report.diagnostics, Severity::Warning,
               "MODEL_DUPLICATE_MATERIAL_NAME",
               "Duplicate material name: " + materialName, asset.sourcePath);
  if (selected(request.sections, "materials"))
    report.document["materials"] = std::move(materials);

  Json textures = Json::array();
  const Json &images = document->value("images", Json::array());
  textureCount = images.size();
  for (std::size_t index = 0; index < images.size(); ++index) {
    const Json &image = images[index];
    Json item{{"index", index},
              {"source_name", name(image, "image_", index)},
              {"embedded", image.contains("bufferView") ||
                               image.value("uri", "").starts_with("data:")}};
    if (image.contains("uri"))
      item["uri"] = image["uri"];
    textures.push_back(std::move(item));
  }
  if (selected(request.sections, "textures"))
    report.document["textures"] = std::move(textures);

  Json skeletons = Json::array();
  const Json &skins = document->value("skins", Json::array());
  for (std::size_t index = 0; index < skins.size(); ++index) {
    const Json &skin = skins[index];
    const std::size_t joints = skin.value("joints", Json::array()).size();
    jointCount = std::max(jointCount, joints);
    skeletons.push_back({{"index", index},
                         {"source_name", name(skin, "skin_", index)},
                         {"joints", joints},
                         {"has_inverse_bind_matrices",
                          skin.contains("inverseBindMatrices")}});
    if (!skin.contains("inverseBindMatrices"))
      diagnose(report.diagnostics, Severity::Warning,
               "MODEL_INVERSE_BIND_MATRICES_MISSING",
               "A skin has no inverse bind matrices.", asset.sourcePath);
  }
  if (selected(request.sections, "skeletons"))
    report.document["skeletons"] = std::move(skeletons);

  Json animations = Json::array();
  std::set<std::string> clipNames;
  const Json &sourceAnimations = document->value("animations", Json::array());
  for (std::size_t index = 0; index < sourceAnimations.size(); ++index) {
    const Json &animation = sourceAnimations[index];
    const std::string clipName = name(animation, "clip_", index);
    float duration = 0.0F;
    for (const Json &sampler : animation.value("samplers", Json::array()))
      if (sampler.contains("input"))
        duration = std::max(duration,
                            accessorMaximum(*document, sampler["input"]));
    animations.push_back({{"index", index},
                          {"source_name", clipName},
                          {"duration", duration},
                          {"channels",
                           animation.value("channels", Json::array()).size()}});
    if (!clipNames.insert(clipName).second)
      diagnose(report.diagnostics, Severity::Warning,
               "MODEL_DUPLICATE_CLIP_NAME",
               "Duplicate animation clip name: " + clipName,
               asset.sourcePath);
    if (duration <= 0.0F)
      diagnose(report.diagnostics, Severity::Warning,
               "MODEL_ZERO_DURATION_CLIP",
               "Animation clip has zero duration: " + clipName,
               asset.sourcePath);
  }
  if (selected(request.sections, "animations"))
    report.document["animations"] = std::move(animations);

  std::string geometryError;
  if (auto geometry = loadGltfSkinnedModel3D(asset.sourcePath, *profile,
                                              geometryError)) {
    std::vector<runtime::Vec3> positions;
    if (geometry->bindPosePositions(positions, geometryError) &&
        !positions.empty()) {
      runtime::Vec3 minimum{std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max()};
      runtime::Vec3 maximum{std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest()};
      for (const runtime::Vec3 point : positions) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
      }
      if (selected(request.sections, "bounds"))
        report.document["bounds"] =
            {{"minimum", {minimum.x, minimum.y, minimum.z}},
             {"maximum", {maximum.x, maximum.y, maximum.z}},
             {"size", {maximum.x - minimum.x, maximum.y - minimum.y,
                       maximum.z - minimum.z}}};
    }
  } else {
    diagnose(report.diagnostics, Severity::Error,
             "MODEL_GEOMETRY_INSPECTION_FAILED", geometryError,
             asset.sourcePath);
  }

  report.document["metrics"] = {{"nodes", sourceNodes.size()},
                                  {"meshes", sourceMeshes.size()},
                                  {"primitives", primitiveCount},
                                  {"triangles", triangleCount},
                                  {"materials", sourceMaterials.size()},
                                  {"textures", textureCount},
                                  {"skins", skins.size()},
                                  {"maximum_joints", jointCount},
                                  {"animations", sourceAnimations.size()}};
  if (triangleCount > 100000U)
    diagnose(report.diagnostics, Severity::Warning,
             "MODEL_MOBILE_TRIANGLE_BUDGET_EXCEEDED",
             "Model exceeds the default 100,000-triangle mobile budget.",
             asset.sourcePath);
  if (jointCount > 128U)
    diagnose(report.diagnostics, Severity::Warning,
             "MODEL_MOBILE_JOINT_BUDGET_EXCEEDED",
             "A skeleton exceeds the default 128-joint mobile budget.",
             asset.sourcePath);
  return report;
}

} // namespace demi::assets
