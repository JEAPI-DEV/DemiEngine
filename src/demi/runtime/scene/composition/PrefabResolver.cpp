#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <algorithm>
#include <fstream>
#include <ranges>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace demi::runtime::composition {
namespace {

using Json = nlohmann::json;

std::optional<std::filesystem::path>
findProjectRoot(const std::filesystem::path &sourcePath) {
  std::filesystem::path cursor = sourcePath.parent_path();
  while (!cursor.empty()) {
    if (std::filesystem::exists(cursor / "demi.project.json")) {
      return cursor;
    }
    const std::filesystem::path parent = cursor.parent_path();
    if (parent == cursor) {
      break;
    }
    cursor = parent;
  }
  return std::nullopt;
}

std::optional<Json> readJson(const std::filesystem::path &path,
                             Diagnostics &diagnostics) {
  std::ifstream input(path);
  if (!input) {
    diagnostics.push_back(
        Diagnostic{.severity = Severity::Error,
                   .code = "PREFAB_READ_FAILED",
                   .message = "Failed to read prefab: " + path.string(),
                   .path = path.string(),
                   .suggestion = "Create the referenced prefab file."});
    return std::nullopt;
  }
  try {
    return Json::parse(input);
  } catch (const Json::parse_error &error) {
    diagnostics.push_back(
        Diagnostic{.severity = Severity::Error,
                   .code = "PREFAB_INVALID_JSON",
                   .message = error.what(),
                   .path = path.string(),
                   .suggestion = "Fix the prefab JSON syntax."});
    return std::nullopt;
  }
}

void remapEntityReferences(
    Json &entity, const std::unordered_map<std::string, std::string> &ids) {
  if (!entity.contains("components") || !entity["components"].is_object()) {
    return;
  }
  for (auto &component : entity["components"].items()) {
    if (!component.value().is_object()) {
      continue;
    }
    auto parent = component.value().find("parent");
    if (parent != component.value().end() && parent->is_string()) {
      const auto replacement = ids.find(parent->get<std::string>());
      if (replacement != ids.end()) {
        *parent = replacement->second;
      }
    }
  }
}

class ExpansionContext {
public:
  explicit ExpansionContext(Diagnostics &diagnostics)
      : diagnostics_(diagnostics) {}

  Json expandInstance(const std::filesystem::path &ownerPath,
                      const Json &instance, const std::string &parentPrefix,
                      const std::string_view collection) {
    Json items = Json::array();
    if (!instance.is_object() || !instance.contains("id") ||
        !instance["id"].is_string() || !instance.contains("prefab") ||
        !instance["prefab"].is_string()) {
      report(ownerPath, "PREFAB_INVALID_INSTANCE",
             "Prefab instances require string id and prefab fields.");
      return items;
    }

    const std::string reference = instance["prefab"].get<std::string>();
    const auto prefabPath = resolvePrefabReference(ownerPath, reference);
    if (!prefabPath.has_value()) {
      report(ownerPath, "PREFAB_INVALID_REFERENCE",
             "Could not resolve prefab reference: " + reference);
      return items;
    }
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(*prefabPath);
    if (active_.contains(canonical)) {
      std::ostringstream chain;
      for (const auto &path : stack_) {
        chain << path.string() << " -> ";
      }
      chain << canonical.string();
      report(ownerPath, "PREFAB_CYCLE",
             "Prefab cycle detected: " + chain.str());
      return items;
    }

    const auto prefab = readJson(canonical, diagnostics_);
    if (!prefab.has_value()) {
      return items;
    }
    if (!prefab->contains("format_version") || !prefab->contains("id") ||
        !(*prefab)["id"].is_string() ||
        !(*prefab)["id"].get<std::string>().starts_with("prefab://")) {
      report(canonical, "PREFAB_INVALID_DOCUMENT",
             "Prefab requires format_version and a prefab:// id.");
      return items;
    }

    active_.insert(canonical);
    stack_.push_back(canonical);
    const std::string prefix =
        parentPrefix.empty()
            ? instance["id"].get<std::string>()
            : parentPrefix + "/" + instance["id"].get<std::string>();

    std::unordered_map<std::string, std::string> ids;
    if (prefab->contains(collection) && (*prefab)[collection].is_array()) {
      for (const Json &item : (*prefab)[collection]) {
        if (item.is_object() && item.contains("id") && item["id"].is_string()) {
          const std::string local = item["id"].get<std::string>();
          ids.emplace(local, prefix + "/" + local);
        }
      }
      for (Json item : (*prefab)[collection]) {
        if (!item.is_object() || !item.contains("id") ||
            !item["id"].is_string()) {
          continue;
        }
        const std::string local = item["id"].get<std::string>();
        item["id"] = ids.at(local);
        if (collection == "entities") {
          remapEntityReferences(item, ids);
        }
        items.push_back(std::move(item));
      }
    }

    if (prefab->contains("instances") && (*prefab)["instances"].is_array()) {
      for (const Json &nested : (*prefab)["instances"]) {
        for (Json &item :
             expandInstance(canonical, nested, prefix, collection)) {
          items.push_back(std::move(item));
        }
      }
    }

    applyOverrides(instance.value("overrides", Json::object()), prefix, items,
                   ownerPath);
    stack_.pop_back();
    active_.erase(canonical);
    return items;
  }

private:
  void applyOverrides(const Json &overrides, const std::string &prefix,
                      Json &entities, const std::filesystem::path &ownerPath) {
    if (!overrides.is_object()) {
      report(ownerPath, "PREFAB_INVALID_OVERRIDES",
             "Instance overrides must be an object keyed by local entity id.");
      return;
    }
    for (const auto &[relativeId, overrideValue] : overrides.items()) {
      const std::string targetId = prefix + "/" + relativeId;
      auto target = std::ranges::find_if(entities, [&](const Json &entity) {
        return entity.is_object() && entity.value("id", "") == targetId;
      });
      if (target == entities.end()) {
        continue;
      }
      if (overrideValue.is_null()) {
        entities.erase(target);
      } else if (overrideValue.is_object()) {
        *target = mergeOverride(std::move(*target), overrideValue);
        (*target)["id"] = targetId;
      } else {
        report(ownerPath, "PREFAB_INVALID_OVERRIDE",
               "Entity override must be an object or null: " + relativeId);
      }
    }
  }

  void report(const std::filesystem::path &path, std::string code,
              std::string message) {
    diagnostics_.push_back(Diagnostic{
        .severity = Severity::Error,
        .code = std::move(code),
        .message = std::move(message),
        .path = path.string(),
        .suggestion = "Inspect the prefab reference and composition chain."});
  }

  Diagnostics &diagnostics_;
  std::set<std::filesystem::path> active_;
  std::vector<std::filesystem::path> stack_;
};

} // namespace

std::optional<std::filesystem::path>
resolvePrefabReference(const std::filesystem::path &sourcePath,
                       const std::string_view reference) {
  constexpr std::string_view Prefix = "prefab://";
  if (!reference.starts_with(Prefix) || reference.size() == Prefix.size()) {
    return std::nullopt;
  }
  const auto root = findProjectRoot(sourcePath);
  if (!root.has_value()) {
    return std::nullopt;
  }
  std::filesystem::path relative(reference.substr(Prefix.size()));
  if (relative.is_absolute() ||
      relative.string().find("..") != std::string::npos) {
    return std::nullopt;
  }
  relative += ".prefab.json";
  return *root / "prefabs" / relative;
}

Json mergeOverride(Json inherited, const Json &overrideValue) {
  if (!overrideValue.is_object() || !inherited.is_object()) {
    return overrideValue;
  }
  for (const auto &[key, value] : overrideValue.items()) {
    if (value.is_null()) {
      inherited.erase(key);
    } else if (value.is_object() && inherited.contains(key) &&
               inherited[key].is_object()) {
      inherited[key] = mergeOverride(std::move(inherited[key]), value);
    } else {
      inherited[key] = value;
    }
  }
  return inherited;
}

ExpansionResult expandScene(const std::filesystem::path &scenePath,
                            const Json &sceneDocument) {
  ExpansionResult result{.document = sceneDocument, .diagnostics = {}};
  if (!sceneDocument.is_object()) {
    result.document.reset();
    result.diagnostics.push_back(
        Diagnostic{.severity = Severity::Error,
                   .code = "SCENE_INVALID_DOCUMENT",
                   .message = "Scene must be a JSON object.",
                   .path = scenePath.string(),
                   .suggestion = "Use the scene schema."});
    return result;
  }
  Json &expanded = *result.document;
  ExpansionContext context(result.diagnostics);
  if (expanded.contains("instances") && expanded["instances"].is_array()) {
    for (const std::string_view collection : {"entities", "elements"}) {
      if (!expanded.contains(collection) || !expanded[collection].is_array()) {
        expanded[collection] = Json::array();
      }
      for (const Json &instance : expanded["instances"]) {
        for (Json &item :
             context.expandInstance(scenePath, instance, {}, collection)) {
          expanded[collection].push_back(std::move(item));
        }
      }
    }
  }
  expanded.erase("instances");
  if (hasErrors(result.diagnostics)) {
    result.document.reset();
  }
  return result;
}

ExpansionResult inspectPrefab(const std::filesystem::path &prefabPath) {
  ExpansionResult result;
  const auto prefab = readJson(prefabPath, result.diagnostics);
  if (!prefab.has_value()) {
    return result;
  }
  Json syntheticScene = {
      {"format_version", 1},
      {"id", "scene://prefab-inspect"},
      {"entities", Json::array()},
      {"instances", {{{"id", "preview"}, {"prefab", (*prefab)["id"]}}}}};
  result = expandScene(prefabPath, syntheticScene);
  return result;
}

} // namespace demi::runtime::composition
