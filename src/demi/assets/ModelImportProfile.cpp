#include "demi/assets/ModelImportProfile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace demi::assets {
namespace {

using Vector = std::array<float, 3>;

void error(Diagnostics *diagnostics, const std::string &code,
           const std::string &message, const std::string &path) {
  if (diagnostics != nullptr)
    diagnostics->push_back({.severity = Severity::Error,
                            .code = code,
                            .message = message,
                            .path = path});
}

std::optional<Vector> axis(const std::string &name) {
  static const std::unordered_map<std::string, Vector> Axes{
      {"+x", {1, 0, 0}}, {"-x", {-1, 0, 0}}, {"+y", {0, 1, 0}},
      {"-y", {0, -1, 0}}, {"+z", {0, 0, 1}}, {"-z", {0, 0, -1}}};
  const auto found = Axes.find(name);
  return found == Axes.end() ? std::nullopt
                             : std::make_optional(found->second);
}

float dot(const Vector a, const Vector b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vector cross(const Vector a, const Vector b) {
  return {a[1] * b[2] - a[2] * b[1],
          a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

} // namespace

ModelImportProfile modelImportPreset(const std::string &name) {
  ModelImportProfile result;
  result.preset = name;
  if (name == "animated_character") {
    result.importAnimations = true;
    result.optimizeMeshes = false;
  } else if (name == "environment") {
    result.rootNode = "flatten";
  } else if (name == "billboard") {
    result.materialPolicy = "unlit";
    result.optimizeMeshes = false;
  }
  return result;
}

std::optional<ModelImportProfile>
parseModelImportProfile(const nlohmann::json &settings,
                        Diagnostics *diagnostics, const std::string &path) {
  const nlohmann::json *source = &settings;
  if (settings.is_object() && settings.contains("model_import"))
    source = &settings["model_import"];
  if (!source->is_object()) {
    error(diagnostics, "MODEL_IMPORT_PROFILE_INVALID",
          "Model import settings must be an object.", path);
    return std::nullopt;
  }
  const std::string preset = source->value("preset", "static_prop");
  if (preset != "static_prop" && preset != "animated_character" &&
      preset != "environment" && preset != "billboard") {
    error(diagnostics, "MODEL_IMPORT_PRESET_INVALID",
          "Model preset must be static_prop, animated_character, environment, "
          "or billboard.",
          path);
    return std::nullopt;
  }
  ModelImportProfile result = modelImportPreset(preset);
  try {
    result.formatVersion = source->value("format_version", 1);
    result.sourceUp = source->value("source_up", result.sourceUp);
    result.sourceForward =
        source->value("source_forward", result.sourceForward);
    result.metersPerUnit =
        source->value("meters_per_unit", result.metersPerUnit);
    result.rootNode = source->value("root_node", result.rootNode);
    result.materialPolicy =
        source->value("material_policy", result.materialPolicy);
    result.importAnimations =
        source->value("import_animations", result.importAnimations);
    result.optimizeMeshes =
        source->value("optimize_meshes", result.optimizeMeshes);
  } catch (const nlohmann::json::exception &exception) {
    error(diagnostics, "MODEL_IMPORT_PROFILE_INVALID", exception.what(), path);
    return std::nullopt;
  }
  const auto up = axis(result.sourceUp);
  const auto forward = axis(result.sourceForward);
  if (result.formatVersion != 1 || !up || !forward ||
      std::abs(dot(*up, *forward)) > 0.001F ||
      !std::isfinite(result.metersPerUnit) || result.metersPerUnit <= 0.0F ||
      (result.rootNode != "preserve" && result.rootNode != "flatten") ||
      (result.materialPolicy != "import" &&
       result.materialPolicy != "unlit" &&
       result.materialPolicy != "ignore")) {
    error(diagnostics, "MODEL_IMPORT_PROFILE_INVALID",
          "Import profile requires version 1, perpendicular signed axes, a "
          "positive unit scale, a supported root policy, and a supported "
          "material policy.",
          path);
    return std::nullopt;
  }
  return result;
}

std::array<float, 16>
modelImportConversion(const ModelImportProfile &profile) {
  const Vector up = *axis(profile.sourceUp);
  const Vector forward = *axis(profile.sourceForward);
  const Vector right = cross(up, forward);
  const float scale = profile.metersPerUnit;
  // Column-major matrix. Each engine coordinate is the projection onto the
  // authored source basis; this also handles mirrored signed-axis choices.
  return {right[0] * scale,   up[0] * scale,   forward[0] * scale, 0.0F,
          right[1] * scale,   up[1] * scale,   forward[1] * scale, 0.0F,
          right[2] * scale,   up[2] * scale,   forward[2] * scale, 0.0F,
          0.0F,               0.0F,            0.0F,               1.0F};
}

nlohmann::json modelImportProfileJson(const ModelImportProfile &profile) {
  return {{"format_version", profile.formatVersion},
          {"preset", profile.preset},
          {"source_up", profile.sourceUp},
          {"source_forward", profile.sourceForward},
          {"meters_per_unit", profile.metersPerUnit},
          {"root_node", profile.rootNode},
          {"material_policy", profile.materialPolicy},
          {"import_animations", profile.importAnimations},
          {"optimize_meshes", profile.optimizeMeshes}};
}

} // namespace demi::assets
