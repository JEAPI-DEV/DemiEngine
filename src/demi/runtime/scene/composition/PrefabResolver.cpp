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

// P5: dotted field helpers for flattened prefab overrides
// ("body.Transform3D.position"). Intermediate objects are created on set.
// A bare "Component.field" address (no components/ prefix) resolves inside
// the entity's components block, matching the nested override shape
// {entity: {components: {Component: ...}}}.
std::string qualifyOverridePath(const std::string &path) {
  const std::size_t dot = path.find('.');
  if (dot == std::string::npos || path.starts_with("components.") ||
      path.starts_with("id"))
    return path;
  return "components." + path;
}
Json *navigateDotted(Json &root, const std::string &path, bool create) {
  if (path.empty() || !root.is_object())
    return nullptr;
  Json *cursor = &root;
  std::size_t start = 0;
  while (true) {
    const std::size_t dot = path.find('.', start);
    const std::string part = (dot == std::string::npos)
                                 ? path.substr(start)
                                 : path.substr(start, dot - start);
    if (part.empty() || !cursor->is_object())
      return nullptr;
    if (!cursor->contains(part)) {
      if (!create)
        return nullptr;
      (*cursor)[part] = Json::object();
    }
    cursor = &(*cursor)[part];
    if (dot == std::string::npos)
      return cursor->is_object() ? cursor : nullptr;
    if (!cursor->is_object())
      return nullptr;
    start = dot + 1;
  }
}

void setDottedField(Json &root, const std::string &path, const Json &value) {
  const std::string effective = qualifyOverridePath(path);
  const std::size_t last = effective.rfind('.');
  if (last == std::string::npos) {
    if (effective != "id")
      root[effective] = value;
    return;
  }
  Json *parent = navigateDotted(root, effective.substr(0, last), true);
  if (parent != nullptr)
    (*parent)[effective.substr(last + 1)] = value;
}

void removeDottedField(Json &root, const std::string &path) {
  const std::string effective = qualifyOverridePath(path);
  const std::size_t last = effective.rfind('.');
  if (last == std::string::npos) {
    if (effective != "id")
      root.erase(effective);
    return;
  }
  Json *parent = navigateDotted(root, effective.substr(0, last), false);
  if (parent != nullptr)
    parent->erase(effective.substr(last + 1));
}

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
                      const Json &instance, const std::string &parentPrefix) {
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
    if (prefab->contains("entities") && (*prefab)["entities"].is_array()) {
      for (const Json &item : (*prefab)["entities"]) {
        if (item.is_object() && item.contains("id") && item["id"].is_string()) {
          const std::string local = item["id"].get<std::string>();
          ids.emplace(local, prefix + "/" + local);
        }
      }
      for (Json item : (*prefab)["entities"]) {
        if (!item.is_object() || !item.contains("id") ||
            !item["id"].is_string()) {
          continue;
        }
        const std::string local = item["id"].get<std::string>();
        item["id"] = ids.at(local);
        remapEntityReferences(item, ids);
        items.push_back(std::move(item));
      }
    }

    if (prefab->contains("instances") && (*prefab)["instances"].is_array()) {
      for (const Json &nested : (*prefab)["instances"]) {
        for (Json &item :
             expandInstance(canonical, nested, prefix)) {
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
      // P5: flattened form "body.Transform3D.position": [...] alongside the
      // nested {body: {components: ...}} form.
      const std::size_t dot = relativeId.find('.');
      if (dot != std::string::npos) {
        const std::string entityPart = relativeId.substr(0, dot);
        const std::string fieldPath = relativeId.substr(dot + 1);
        const std::string targetId = prefix + "/" + entityPart;
        auto target = std::ranges::find_if(entities, [&](const Json &entity) {
          return entity.is_object() && entity.value("id", "") == targetId;
        });
        if (target == entities.end())
          continue;
        if (!overrideValue.is_null()) {
          setDottedField(*target, fieldPath, overrideValue);
          (*target)["id"] = targetId;
        } else {
          // Null flattened override removes the addressed field.
          removeDottedField(*target, fieldPath);
          (*target)["id"] = targetId;
        }
        continue;
      }
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
    if (!expanded.contains("entities") || !expanded["entities"].is_array())
      expanded["entities"] = Json::array();
    for (const Json &instance : expanded["instances"]) {
      for (Json &item : context.expandInstance(scenePath, instance, {})) {
        expanded["entities"].push_back(std::move(item));
      }
    }
  }
  expanded.erase("instances");
  if (hasErrors(result.diagnostics)) {
    result.document.reset();
  }
  return result;
}

ExpansionResult expandPrefabInstance(const std::filesystem::path &ownerPath,
                                     const Json &instance) {
  ExpansionResult result{.document = Json::array(), .diagnostics = {}};
  ExpansionContext context(result.diagnostics);
  *result.document = context.expandInstance(ownerPath, instance, {});
  if (hasErrors(result.diagnostics))
    result.document.reset();
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
