#include "demi/schema/PrefabPlacementValidation.h"
#include "demi/runtime/scene/composition/EntityHierarchy.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <vector>

namespace demi {
namespace {
struct PrefabRoots {
  std::vector<std::string> ids;
  std::string error;
};

PrefabRoots readRoots(const std::filesystem::path &path) {
  PrefabRoots result;
  try {
    std::ifstream input(path);
    const auto prefab = nlohmann::json::parse(input);
    for (const auto &entity :
         runtime::composition::flattenEntityHierarchy(prefab.at("entities"))) {
      if (!entity.contains("components") || !entity["components"].is_object())
        continue;
      const auto &components = entity["components"];
      const auto transform = components.find("Transform3D");
      if (transform == components.end() || !transform->is_object())
        continue;
      const auto parent = transform->find("parent");
      if (parent == transform->end() || parent->is_null() ||
          (parent->is_string() && parent->get<std::string>().empty()))
        result.ids.push_back(entity.at("id").get<std::string>());
    }
  } catch (const std::exception &error) {
    result.error = error.what();
  }
  return result;
}
} // namespace

void validatePrefabPlacements3D(Diagnostics &diagnostics,
                                const std::filesystem::path &source,
                                const nlohmann::json &document) {
  if (!document.contains("entities") || !document["entities"].is_array())
    return;
  // Multiple placements share one authored prefab; inspect its roots once per
  // validation pass rather than rereading it for every placement/gizmo edit.
  std::map<std::filesystem::path, PrefabRoots> roots;
  for (const auto &entity : document["entities"]) {
    if (!entity.contains("components") || !entity["components"].is_object())
      continue;
    const auto &components = entity["components"];
    const auto placement = components.find("PrefabPlacement3D");
    if (placement == components.end() || !placement->is_object())
      continue;
    const auto invalid = [&](const std::string &message) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "PREFAB_PLACEMENT_INVALID",
           .message = "Entity " + entity.value("id", std::string("<unknown>")) +
                      ": " + message,
           .path = source.string(),
           .suggestion = "Use one Transform3D root and keep simulation "
                         "components in the prefab."});
    };
    if (!components.contains("Transform3D"))
      invalid("PrefabPlacement3D requires Transform3D.");
    else if (components["Transform3D"].is_object()) {
      const auto scale = components["Transform3D"].find("scale");
      if (scale != components["Transform3D"].end() && scale->is_array())
        for (const auto &axis : *scale)
          if (axis.is_number() && axis.get<double>() <= 0)
            invalid("Placement scale must be positive.");
    }
    if (components.contains("Rigidbody3D") ||
        components.contains("Destructible3D"))
      invalid("A placement is a marker, not a simulated or destructible body.");
    const auto reference = placement->find("prefab");
    if (reference == placement->end() || !reference->is_string() ||
        reference->get<std::string>().empty())
      continue;
    if (placement->contains("root") && !(*placement)["root"].is_string())
      continue;
    const auto path = runtime::composition::resolvePrefabReference(
        source, reference->get<std::string>());
    if (!path || !std::filesystem::is_regular_file(*path)) {
      invalid("Prefab reference does not resolve.");
      continue;
    }
    if (!roots.contains(*path))
      roots.emplace(*path, readRoots(*path));
    const auto &info = roots.at(*path);
    const auto root = placement->value("root", std::string("assembly"));
    if (!info.error.empty())
      invalid(info.error);
    else if (info.ids.size() != 1 || info.ids.front() != root)
      invalid("Prefab must have the specified single Transform3D root: " +
              root);
  }
}
} // namespace demi
