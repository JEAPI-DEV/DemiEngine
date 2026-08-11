#include "demi/assets/ColliderAssetGenerator.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/GltfGeometry.h"
#include "demi/assets/GltfSkinnedModel.h"
#include "demi/assets/ModelImportProfile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>

namespace demi::assets {
namespace {

using Json = nlohmann::json;
using Matrix = std::array<float, 16>;

struct Bounds {
  std::array<float, 3> minimum{std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max()};
  std::array<float, 3> maximum{std::numeric_limits<float>::lowest(),
                               std::numeric_limits<float>::lowest(),
                               std::numeric_limits<float>::lowest()};
};

void addDiagnostic(Diagnostics &diagnostics, const std::string &code,
                   const std::string &message,
                   const std::filesystem::path &path) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = code,
                         .message = message,
                         .path = path.string(),
                         .suggestion = {}});
}

std::string idPath(std::string id) {
  constexpr std::string_view Prefix = "asset://";
  if (id.starts_with(Prefix))
    id.erase(0, Prefix.size());
  std::ranges::replace(id, ':', '_');
  return id;
}

Matrix identityMatrix() {
  return {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
          0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
}

Matrix multiply(const Matrix &left, const Matrix &right) {
  Matrix result{};
  for (int column = 0; column < 4; ++column)
    for (int row = 0; row < 4; ++row)
      for (int index = 0; index < 4; ++index)
        result[column * 4 + row] +=
            left[index * 4 + row] * right[column * 4 + index];
  return result;
}

Matrix nodeMatrix(const Json &node) {
  if (const auto matrix = node.find("matrix");
      matrix != node.end() && matrix->is_array() && matrix->size() == 16) {
    Matrix result{};
    for (std::size_t index = 0; index < result.size(); ++index)
      result[index] = (*matrix)[index].get<float>();
    return result;
  }
  const auto value = [&node](const char *name, const std::size_t index,
                             const float fallback) {
    const auto field = node.find(name);
    return field != node.end() && field->is_array() && field->size() > index
               ? (*field)[index].get<float>()
               : fallback;
  };
  const float x = value("rotation", 0, 0.0F);
  const float y = value("rotation", 1, 0.0F);
  const float z = value("rotation", 2, 0.0F);
  const float w = value("rotation", 3, 1.0F);
  const float sx = value("scale", 0, 1.0F);
  const float sy = value("scale", 1, 1.0F);
  const float sz = value("scale", 2, 1.0F);
  const float tx = value("translation", 0, 0.0F);
  const float ty = value("translation", 1, 0.0F);
  const float tz = value("translation", 2, 0.0F);
  return {
      (1.0F - 2.0F * (y * y + z * z)) * sx,
      (2.0F * (x * y + z * w)) * sx,
      (2.0F * (x * z - y * w)) * sx,
      0.0F,
      (2.0F * (x * y - z * w)) * sy,
      (1.0F - 2.0F * (x * x + z * z)) * sy,
      (2.0F * (y * z + x * w)) * sy,
      0.0F,
      (2.0F * (x * z + y * w)) * sz,
      (2.0F * (y * z - x * w)) * sz,
      (1.0F - 2.0F * (x * x + y * y)) * sz,
      0.0F,
      tx,
      ty,
      tz,
      1.0F,
  };
}

std::array<float, 3> transformPoint(const Matrix &matrix,
                                    const std::array<float, 3> point) {
  return {
      matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] +
          matrix[12],
      matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] +
          matrix[13],
      matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] +
          matrix[14],
  };
}

void expand(Bounds &bounds, const std::array<float, 3> point) {
  for (std::size_t axis = 0; axis < point.size(); ++axis) {
    bounds.minimum[axis] = std::min(bounds.minimum[axis], point[axis]);
    bounds.maximum[axis] = std::max(bounds.maximum[axis], point[axis]);
  }
}

bool validBounds(const Json &accessor) {
  return accessor.value("type", "") == "VEC3" && accessor.contains("min") &&
         accessor["min"].is_array() && accessor["min"].size() == 3 &&
         accessor.contains("max") && accessor["max"].is_array() &&
         accessor["max"].size() == 3;
}

void addPrimitiveBounds(const Json &document, const int meshIndex,
                        const Matrix &transform, Bounds &bounds,
                        bool &foundBounds, Diagnostics &diagnostics,
                        const std::filesystem::path &path) {
  const auto &meshes = document["meshes"];
  if (meshIndex < 0 || static_cast<std::size_t>(meshIndex) >= meshes.size()) {
    addDiagnostic(diagnostics, "COLLIDER_GLTF_MESH_INVALID",
                  "A glTF node references a missing mesh.", path);
    return;
  }
  for (const Json &primitive : meshes[meshIndex].value("primitives", Json{})) {
    const auto attributes = primitive.find("attributes");
    if (attributes == primitive.end() || !attributes->contains("POSITION"))
      continue;
    const int accessorIndex = (*attributes)["POSITION"].get<int>();
    const auto &accessors = document["accessors"];
    if (accessorIndex < 0 ||
        static_cast<std::size_t>(accessorIndex) >= accessors.size() ||
        !validBounds(accessors[accessorIndex])) {
      addDiagnostic(
          diagnostics, "COLLIDER_GLTF_POSITION_BOUNDS_MISSING",
          "Every glTF POSITION accessor used for collider generation must "
          "provide VEC3 min/max bounds.",
          path);
      return;
    }
    const Json &accessor = accessors[accessorIndex];
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    for (std::size_t axis = 0; axis < minimum.size(); ++axis) {
      minimum[axis] = accessor["min"][axis].get<float>();
      maximum[axis] = accessor["max"][axis].get<float>();
    }
    for (const float x : {minimum[0], maximum[0]})
      for (const float y : {minimum[1], maximum[1]})
        for (const float z : {minimum[2], maximum[2]})
          expand(bounds, transformPoint(transform, {x, y, z}));
    foundBounds = true;
  }
}

void visitNode(const Json &document, const int nodeIndex,
               const Matrix &parentTransform, Bounds &bounds, bool &foundBounds,
               Diagnostics &diagnostics, const std::filesystem::path &path) {
  const auto &nodes = document["nodes"];
  if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= nodes.size()) {
    addDiagnostic(diagnostics, "COLLIDER_GLTF_NODE_INVALID",
                  "A glTF scene references a missing node.", path);
    return;
  }
  const Json &node = nodes[nodeIndex];
  const Matrix transform = multiply(parentTransform, nodeMatrix(node));
  if (const auto mesh = node.find("mesh"); mesh != node.end())
    addPrimitiveBounds(document, mesh->get<int>(), transform, bounds,
                       foundBounds, diagnostics, path);
  if (const auto children = node.find("children");
      children != node.end() && children->is_array())
    for (const Json &child : *children)
      visitNode(document, child.get<int>(), transform, bounds, foundBounds,
                diagnostics, path);
}

std::optional<Bounds> gltfBounds(const std::filesystem::path &path,
                                 Diagnostics &diagnostics) {
  Json document;
  try {
    std::ifstream input(path);
    document = Json::parse(input);
  } catch (const Json::exception &error) {
    addDiagnostic(diagnostics, "COLLIDER_GLTF_INVALID", error.what(), path);
    return std::nullopt;
  }
  if (!document.contains("nodes") || !document["nodes"].is_array() ||
      !document.contains("meshes") || !document["meshes"].is_array() ||
      !document.contains("accessors") || !document["accessors"].is_array() ||
      !document.contains("scenes") || !document["scenes"].is_array()) {
    addDiagnostic(diagnostics, "COLLIDER_GLTF_STRUCTURE_INVALID",
                  "glTF collider generation requires nodes, meshes, accessors, "
                  "and scenes arrays.",
                  path);
    return std::nullopt;
  }
  const int sceneIndex = document.value("scene", 0);
  if (sceneIndex < 0 ||
      static_cast<std::size_t>(sceneIndex) >= document["scenes"].size()) {
    addDiagnostic(diagnostics, "COLLIDER_GLTF_SCENE_INVALID",
                  "The default glTF scene is missing.", path);
    return std::nullopt;
  }
  const Json &scene = document["scenes"][sceneIndex];
  if (!scene.contains("nodes") || !scene["nodes"].is_array()) {
    addDiagnostic(diagnostics, "COLLIDER_GLTF_SCENE_INVALID",
                  "The default glTF scene has no root nodes.", path);
    return std::nullopt;
  }
  Bounds result;
  bool foundBounds = false;
  for (const Json &root : scene["nodes"])
    visitNode(document, root.get<int>(), identityMatrix(), result, foundBounds,
              diagnostics, path);
  if (!diagnostics.empty() || !foundBounds) {
    if (diagnostics.empty())
      addDiagnostic(diagnostics, "COLLIDER_GLTF_GEOMETRY_MISSING",
                    "The glTF default scene contains no POSITION geometry.",
                    path);
    return std::nullopt;
  }
  return result;
}

bool writeJson(const std::filesystem::path &path, const Json &document) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error)
    return false;
  std::ofstream output(path);
  output << document.dump(2) << '\n';
  return static_cast<bool>(output);
}

std::optional<ModelImportProfile> profileFor(const AssetManifest &model,
                                             Diagnostics &diagnostics) {
  const Json settings = Json::parse(model.settingsJson, nullptr, false);
  return parseModelImportProfile(settings.is_object() ? settings
                                                       : Json::object(),
                                 &diagnostics, model.manifestPath.string());
}

std::optional<Bounds> normalizedBounds(const AssetManifest &model,
                                       const ModelImportProfile &profile,
                                       Diagnostics &diagnostics) {
  std::string error;
  const auto geometry = loadGltfSkinnedModel3D(model.sourcePath, profile, error);
  std::vector<runtime::Vec3> points;
  if (!geometry || !geometry->bindPosePositions(points, error) ||
      points.empty()) {
    addDiagnostic(diagnostics, "COLLIDER_GLTF_GEOMETRY_INVALID", error,
                  model.sourcePath);
    return std::nullopt;
  }
  Bounds result;
  for (const runtime::Vec3 point : points)
    expand(result, {point.x, point.y, point.z});
  return result;
}

std::optional<Bounds> normalizedAccessorBounds(
    const AssetManifest &model, const ModelImportProfile &profile,
    Diagnostics &diagnostics) {
  if (model.sourcePath.extension() != ".gltf")
    return normalizedBounds(model, profile, diagnostics);
  const auto authored = gltfBounds(model.sourcePath, diagnostics);
  if (!authored)
    return std::nullopt;
  const Matrix conversion = modelImportConversion(profile);
  Bounds result;
  for (const float x : {authored->minimum[0], authored->maximum[0]})
    for (const float y : {authored->minimum[1], authored->maximum[1]})
      for (const float z : {authored->minimum[2], authored->maximum[2]})
        expand(result, transformPoint(conversion, {x, y, z}));
  return result;
}

} // namespace

std::optional<ColliderRecommendation>
recommendCollider(const std::filesystem::path &modelManifestPath,
                  const std::string &body, Diagnostics &diagnostics) {
  if (body != "static" && body != "dynamic" && body != "trigger" &&
      body != "character") {
    addDiagnostic(diagnostics, "COLLIDER_BODY_INVALID",
                  "Collider body must be static, dynamic, trigger, or "
                  "character.",
                  modelManifestPath);
    return std::nullopt;
  }
  Diagnostic diagnostic;
  const auto model = loadAssetManifest(modelManifestPath, &diagnostic);
  if (!model || model->type != "Model3D") {
    if (!model)
      diagnostics.push_back(std::move(diagnostic));
    else
      addDiagnostic(diagnostics, "COLLIDER_SOURCE_UNSUPPORTED",
                    "Collider recommendation requires a Model3D asset.",
                    modelManifestPath);
    return std::nullopt;
  }
  const auto profile = profileFor(*model, diagnostics);
  const auto bounds = profile ? normalizedBounds(*model, *profile, diagnostics)
                              : std::nullopt;
  if (!bounds)
    return std::nullopt;
  std::array<float, 3> size{};
  std::array<float, 3> offset{};
  for (std::size_t axis = 0; axis < size.size(); ++axis) {
    size[axis] = bounds->maximum[axis] - bounds->minimum[axis];
    offset[axis] = (bounds->maximum[axis] + bounds->minimum[axis]) * 0.5F;
  }
  ColliderRecommendation result;
  result.body = body;
  if (body == "character") {
    const float radius = std::max(std::min(size[0], size[2]) * 0.5F, 0.01F);
    result.shape = "capsule";
    result.reason = "Character controllers need a stable convex sweep shape.";
    result.component = {{"CapsuleCollider3D",
                         {{"radius", radius},
                          {"height", std::max(size[1], radius * 2.0F)},
                          {"offset", offset},
                          {"is_trigger", false}}}};
  } else if (body == "dynamic") {
    result.shape = "convex_hull";
    result.reason = "Dynamic rigid bodies cannot use triangle mesh colliders.";
    Json points = Json::array();
    for (const float x : {bounds->minimum[0], bounds->maximum[0]})
      for (const float y : {bounds->minimum[1], bounds->maximum[1]})
        for (const float z : {bounds->minimum[2], bounds->maximum[2]})
          points.push_back({x, y, z});
    result.component = {{"ConvexCollider3D",
                         {{"points", points},
                          {"offset", {0.0F, 0.0F, 0.0F}},
                          {"is_trigger", false}}}};
  } else if (body == "trigger") {
    result.shape = "box";
    result.reason = "Triggers favor a predictable inexpensive volume.";
    result.component = {{"BoxCollider3D",
                         {{"size", size},
                          {"offset", offset},
                          {"is_trigger", true}}}};
  } else {
    result.shape = "triangle_mesh";
    result.detail = 1.0F;
    result.reason = "Static geometry can safely use exact source triangles.";
    result.component = {{"ModelCollider3D",
                         {{"asset", "<generated-collider-asset>"},
                          {"is_trigger", false}}}};
  }
  return result;
}

ColliderAssetGenerationResult
generateColliderAsset(const ColliderAssetGenerationRequest &request) {
  ColliderAssetGenerationResult result;
  if (!std::isfinite(request.detail) || request.detail < 0.0F ||
      request.detail > 1.0F) {
    addDiagnostic(result.diagnostics, "COLLIDER_DETAIL_INVALID",
                  "Collider detail must be between 0 and 1.",
                  request.modelManifestPath);
    return result;
  }
  if (!request.id.starts_with("asset://") || request.id.size() <= 8) {
    addDiagnostic(result.diagnostics, "COLLIDER_ASSET_ID_INVALID",
                  "Collider assets require a non-empty asset:// ID.",
                  request.modelManifestPath);
    return result;
  }
  if (request.body != "static" && request.body != "trigger") {
    addDiagnostic(result.diagnostics, "COLLIDER_ASSET_BODY_UNSAFE",
                  "Generated ModelCollider3D assets are for static geometry "
                  "or triggers. Use the recommended explicit convex component "
                  "for dynamic bodies and characters.",
                  request.modelManifestPath);
    return result;
  }
  Diagnostic diagnostic;
  const auto model = loadAssetManifest(request.modelManifestPath, &diagnostic);
  if (!model) {
    result.diagnostics.push_back(std::move(diagnostic));
    return result;
  }
  if (model->type != "Model3D" ||
      (model->sourcePath.extension() != ".gltf" &&
       model->sourcePath.extension() != ".glb")) {
    addDiagnostic(
        result.diagnostics, "COLLIDER_SOURCE_UNSUPPORTED",
        "Collider generation supports glTF/GLB Model3D assets.",
        model->manifestPath);
    return result;
  }
  const AssetRegistry registry = loadAssetRegistry(request.projectDirectory);
  if (const AssetManifest *existing = findAsset(registry, request.id);
      existing != nullptr && existing->type != "Collider3D") {
    addDiagnostic(result.diagnostics, "COLLIDER_ASSET_CONFLICT",
                  "A collider asset already uses this ID.",
                  request.modelManifestPath);
    return result;
  }
  const auto profile = profileFor(*model, result.diagnostics);
  const auto bounds =
      profile ? (request.detail == 0.0F
                     ? normalizedAccessorBounds(*model, *profile,
                                                result.diagnostics)
                     : normalizedBounds(*model, *profile, result.diagnostics))
              : std::nullopt;
  if (!bounds)
    return result;
  if (request.detail > 0.0F) {
    std::string geometryError;
    if (model->sourcePath.extension() == ".gltf" &&
        !loadGltfGeometry(model->sourcePath, *profile, geometryError)) {
      addDiagnostic(result.diagnostics, "COLLIDER_GLTF_GEOMETRY_INVALID",
                    geometryError, model->sourcePath);
      return result;
    }
  }

  const std::filesystem::path relativeId(idPath(request.id));
  const std::filesystem::path assetDirectory =
      request.projectDirectory / "assets" / relativeId;
  result.manifestPath = assetDirectory / "collider.asset.json";
  std::array<float, 3> size{};
  std::array<float, 3> offset{};
  for (std::size_t axis = 0; axis < size.size(); ++axis) {
    size[axis] = bounds->maximum[axis] - bounds->minimum[axis];
    offset[axis] = (bounds->maximum[axis] + bounds->minimum[axis]) * 0.5F;
  }
  const auto hash = hashFiles(model->sourcePaths);
  if (!hash) {
    addDiagnostic(result.diagnostics, "COLLIDER_ASSET_HASH_FAILED",
                  "Could not hash the glTF source data.", model->sourcePath);
    return result;
  }
  const Json manifest = {
      {"format_version", 1},
      {"id", request.id},
      {"type", "Collider3D"},
      {"importer", "collider-generator"},
      {"importer_version", 1},
      {"source", std::filesystem::relative(model->sourcePath, assetDirectory)
                     .generic_string()},
      {"source_hash", *hash},
      {"dependencies", Json::array({model->id})},
      {"settings", {{"model_import", modelImportProfileJson(*profile)}}},
      {"source_asset", model->id},
      {"shape", request.detail == 0.0F ? "box" : "triangle_mesh"},
      {"size", size},
      {"offset", offset},
      {"detail", request.detail},
      {"generation",
       {{"source_asset", model->id},
        {"source_hash", model->sourceHash},
        {"detail", request.detail},
        {"body", request.body},
        {"model_import", modelImportProfileJson(*profile)}}},
  };
  if (!writeJson(result.manifestPath, manifest)) {
    addDiagnostic(result.diagnostics, "COLLIDER_ASSET_WRITE_FAILED",
                  "Could not write collider asset manifest.",
                  result.manifestPath);
  }
  if (result.diagnostics.empty() && !request.previewPath.empty()) {
    const Json preview = {
        {"format_version", 1},
        {"id", "scene://collider_preview"},
        {"entities",
         Json::array({{{"id", "preview"},
                       {"components",
                        {{"Transform3D", Json::object()},
                         {"MeshRenderer", {{"model", model->id}}},
                         {"ModelCollider3D", {{"asset", request.id}}},
                         {"Rigidbody3D",
                          {{"body_type", "static"},
                           {"use_gravity", false}}}}}}})}};
    if (!writeJson(request.previewPath, preview))
      addDiagnostic(result.diagnostics, "COLLIDER_PREVIEW_WRITE_FAILED",
                    "Could not write collider preview scene.",
                    request.previewPath);
  }
  return result;
}

} // namespace demi::assets
