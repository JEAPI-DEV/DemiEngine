#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/runtime/scene/composition/EntityHierarchy.h"
#include "demi/assets/FractureAuthoring.h"
#include "demi/assets/MasonryGeneration.h"

#include <algorithm>
#include <fstream>
#include <functional>
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

void collectDeferredIds(const Json &entity,
    const std::function<void(const std::string &)> &add) {
  if(!entity.contains("components"))return;
  const auto &c=entity["components"];
  if(!c.contains("Destructible3D") || !c["Destructible3D"].contains("deferred_visuals"))return;
  for(const auto &templates:c["Destructible3D"]["deferred_visuals"])
    for(const auto &item:templates)add(item.at("id").get<std::string>());
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
    if (component.key() == "Destructible3D" && component.value().contains("parts") &&
        component.value()["parts"].is_object()) {
      for (auto &[part, visual] : component.value()["parts"].items()) {
        if (!visual.is_string()) continue;
        const auto replacement = ids.find(visual.get<std::string>());
        if (replacement != ids.end()) visual = replacement->second;
      }
      if(component.value().contains("deferred_visuals")) {
        Json groups=Json::object();
        for(auto &[region,templates]:component.value()["deferred_visuals"].items()) {
          for(auto &item:templates) {
            item["id"]=ids.at(item.at("id").get<std::string>());
            remapEntityReferences(item,ids);
          }
          groups[ids.at(region)]=std::move(templates);
        }
        component.value()["deferred_visuals"]=std::move(groups);
      }
      if (component.value().contains("intact_visuals")) {
        Json remappedRegions = Json::object();
        for (const auto &[regionId, visualIds] :
             component.value()["intact_visuals"].items()) {
          Json remappedVisualIds = Json::array();
          for (const auto &visualId : visualIds) {
            remappedVisualIds.push_back(ids.at(visualId.get<std::string>()));
          }
          remappedRegions[ids.at(regionId)] = std::move(remappedVisualIds);
        }
        component.value()["intact_visuals"] = std::move(remappedRegions);
      }
    }
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
  explicit ExpansionContext(Diagnostics &diagnostics,
      std::filesystem::path overridePath = {}, std::optional<Json> overrideDocument = {}, bool compileFractures = true)
      : diagnostics_(diagnostics), overridePath_(std::move(overridePath)), overrideDocument_(std::move(overrideDocument)), compileFractures_(compileFractures) {}

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

    auto prefab = overrideDocument_ && canonical==overridePath_ ? overrideDocument_ : readJson(canonical, diagnostics_);
    if (!prefab.has_value()) {
      return items;
    }
    if (prefab->contains("fracture")) {
      report(canonical, "PREFAB_INVALID_DOCUMENT",
             "Top-level fracture recipes are unsupported. Author Destructible3D "
             "with Fracture3D or Masonry3D components instead.");
      return items;
    }
    if (prefab->contains("source_recipe")) {
      const auto &recipe = prefab->at("source_recipe");
      if (!recipe.is_object() || recipe.contains("fracture") ||
          recipe.value("id", "") != prefab->value("id", "")) {
        report(canonical, "PREFAB_SOURCE_RECIPE_INVALID",
               "Prepared prefab has an invalid source recipe.");
        return items;
      }
      bool needsSource = !compileFractures_ || !parentPrefix.empty();
      const auto overrides = instance.value("overrides", Json::object());
      if (!needsSource && !overrides.empty()) {
        auto overridden = prefab->value("entities", Json::array());
        applyOverrides(overrides, {}, overridden, ownerPath);
        needsSource = overridden != prefab->value("entities", Json::array());
      }
      if (needsSource) {
        *prefab = recipe;
      }
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

    if (prefab->contains("entities")) {
      try {
        (*prefab)["entities"] = flattenEntityHierarchy((*prefab)["entities"]);
      } catch (const std::exception &error) {
        report(canonical, "ENTITY_HIERARCHY_INVALID", error.what());
        stack_.pop_back();
        active_.erase(canonical);
        return items;
      }
    }
    std::unordered_map<std::string, std::string> ids;
    if (prefab->contains("entities") && (*prefab)["entities"].is_array()) {
      for (const Json &item : (*prefab)["entities"]) {
        if (item.is_object() && item.contains("id") && item["id"].is_string()) {
          const std::string local = item["id"].get<std::string>();
          ids.emplace(local, prefix + "/" + local);
          collectDeferredIds(item,[&](const std::string &id){ids.emplace(id,prefix+"/"+id);});
        }
      }
      for (Json item : (*prefab)["entities"]) {
        if (!item.is_object() || !item.contains("id") ||
            !item["id"].is_string()) {
          continue;
        }
        if(item.contains("prefab")) {
          remapEntityReferences(item, ids);
          for (const char *transform : {"Transform2D", "Transform3D", "IsoTransform"}) {
            if (item.contains(transform) && item[transform].is_object()) {
              auto parent = item[transform].find("parent");
              if (parent != item[transform].end() && parent->is_string()) {
                const auto mapped = ids.find(parent->get<std::string>());
                if (mapped != ids.end())
                  *parent = mapped->second;
              }
            }
          }
          for(auto &nested:expandInstance(canonical,item,prefix)) items.push_back(std::move(nested));
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
        for (Json &item : expandInstance(canonical, nested, prefix)) {
          items.push_back(std::move(item));
        }
      }
    }

    applyOverrides(instance.value("overrides", Json::object()), prefix, items,
                   ownerPath);
    attachInstanceParent(ownerPath, instance, items);
    const bool finalizeInstance = parentPrefix.empty();
    if (!compileFractures_ && finalizeInstance) {
      items = assets::expandMasonry(items, true);
    }
    if (!compileFractures_ && finalizeInstance) {
      const auto authored = items;
      for (const auto &entity : authored)
        for (auto &preview : expandPlacement(canonical, entity))
          items.push_back(std::move(preview));
    }
    if (compileFractures_ && finalizeInstance) {
      try {
        items = assets::compileEntityFractures(*findProjectRoot(canonical), items, prefix);
      } catch (const std::exception &error) {
        report(canonical, "FRACTURE_GENERATION_FAILED", error.what());
      }
    }
    stack_.pop_back();
    active_.erase(canonical);
    return items;
  }

  void attachInstanceParent(const std::filesystem::path &ownerPath,
                            const Json &instance, Json &items) {
    const Json &authored = instance.contains("components") ? instance["components"] : instance;
    for (const char *domain : {"Transform2D", "Transform3D", "IsoTransform"}) {
      const auto transform = authored.find(domain);
      if (transform == authored.end() || !transform->is_object())
        continue;
      const std::string parent = transform->value("parent", std::string{});
      if (parent.empty())
        continue;
      std::set<std::string> internalIds;
      for (const auto &item : items)
        internalIds.insert(item.at("id").get<std::string>());
      for (auto &item : items) {
        Json &components = item.contains("components") ? item["components"] : item;
        bool internalChild = false;
        for (const char *candidate : {"Transform2D", "Transform3D", "IsoTransform"}) {
          const auto current = components.find(candidate);
          if (current == components.end() || !current->is_object())
            continue;
          if (std::string_view(candidate) != domain) {
            report(ownerPath, "PREFAB_PARENT_DOMAIN_MISMATCH",
                   "A nested prefab must use its parent's transform domain.");
            return;
          }
          const auto existing = current->value("parent", std::string{});
          internalChild = internalIds.contains(existing);
          if (!internalChild && !existing.empty() && existing != parent) {
            report(ownerPath, "PREFAB_PARENT_CONFLICT",
                   "A nested prefab root has a conflicting external parent.");
            return;
          }
        }
        if (!internalChild) {
          if (!components.contains(domain))
            components[domain] = Json::object();
          components[domain]["parent"] = parent;
        }
      }
    }
  }

  Json expandPlacement(const std::filesystem::path &ownerPath, const Json &entity) {
    if (!entity.contains("components") || !entity["components"].is_object())
      return Json::array();
    const auto placement = entity["components"].find("PrefabPlacement3D");
    if (placement == entity["components"].end() || !placement->is_object())
      return Json::array();
    const auto reference = placement->value("prefab", std::string{});
    if (reference.empty()) return Json::array(); // Incomplete editor authoring.
    const auto owner = entity.at("id").get<std::string>();
    const auto root = placement->value("root", std::string("assembly"));
    const auto prefix = owner + "/__preview";
    auto items = expandInstance(ownerPath, {{"id", prefix}, {"prefab", reference}}, {});
    auto found = std::ranges::find_if(items, [&](const auto &item) {
      return item.value("id", std::string{}) == prefix + "/" + root;
    });
    if (found == items.end() || !(*found)["components"].contains("Transform3D")) {
      report(ownerPath, "PREFAB_PLACEMENT_ROOT_INVALID", "Placement " + owner + " has no Transform3D root: " + root);
      return Json::array();
    }
    // The placement owns the root pose. Internal child transforms stay in the
    // prefab. This transient parent is never written to the authored document.
    (*found)["components"]["Transform3D"] = {{"parent", owner}};
    for (auto &item : items) {
      const auto &components = item["components"];
      if (item["id"] != prefix + "/" + root && components.contains("Transform3D") &&
          (!components["Transform3D"].contains("parent") || !components["Transform3D"]["parent"].is_string() ||
           components["Transform3D"]["parent"].get<std::string>().empty())) {
        report(ownerPath, "PREFAB_PLACEMENT_ROOT_INVALID", "Placement prefabs require one Transform3D root.");
        return Json::array();
      }
      if (!entity.value("enabled", true)) item["enabled"] = false;
    }
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
        const std::string targetId =
            prefix.empty() ? entityPart : prefix + "/" + entityPart;
        auto target = std::ranges::find_if(entities, [&](const Json &entity) {
          return entity.is_object() && entity.value("id", "") == targetId;
        });
        if (target == entities.end()) {
          report(ownerPath, "PREFAB_OVERRIDE_TARGET_MISSING",
                 "Override '" + relativeId + "' targets missing entity '" +
                     targetId + "'.");
          continue;
        }
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
      const std::string targetId =
          prefix.empty() ? relativeId : prefix + "/" + relativeId;
      auto target = std::ranges::find_if(entities, [&](const Json &entity) {
        return entity.is_object() && entity.value("id", "") == targetId;
      });
      if (target == entities.end()) {
        report(ownerPath, "PREFAB_OVERRIDE_TARGET_MISSING",
               "Override '" + relativeId + "' targets missing entity '" +
                   targetId + "'.");
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
  std::filesystem::path overridePath_;
  std::optional<Json> overrideDocument_;
  bool compileFractures_ = true;
};

} // namespace

nlohmann::json rebasePrefabEntities(nlohmann::json entities,
    std::string_view oldPrefix, std::string_view newPrefix) {
  std::unordered_map<std::string,std::string> ids;
  const auto prefix=std::string(oldPrefix)+"/";
  for (const auto &entity:entities) {
    const auto id=entity.at("id").get<std::string>();
    if (!id.starts_with(prefix)) throw std::runtime_error("Invalid prepared prefab identity");
    ids.emplace(id,std::string(newPrefix)+"/"+id.substr(prefix.size()));
    collectDeferredIds(entity,[&](const std::string &leaf) {
      if(!leaf.starts_with(prefix))throw std::runtime_error("Invalid deferred prefab identity");
      ids.emplace(leaf,std::string(newPrefix)+"/"+leaf.substr(prefix.size()));
    });
  }
  for (auto &entity:entities) {
    entity["id"]=ids.at(entity.at("id").get<std::string>());
    remapEntityReferences(entity,ids);
  }
  return entities;
}

PrefabOriginIndex::PrefabOriginIndex(const Json &ownerDocument) {
  const auto consider = [&](const Json &instance) {
    if (!instance.is_object())
      return;
    const auto id = instance.find("id");
    if (id == instance.end() || !id->is_string())
      return;
    const std::string &instanceId = id->get_ref<const std::string &>();
    if (!instanceId.empty())
      instances_.insert(instanceId);
  };
  for (const char *field : {"instances", "prefab_origins"}) {
    const auto instances = ownerDocument.find(field);
    if (instances != ownerDocument.end() && instances->is_array())
      for (const auto &instance : *instances)
        consider(instance);
  }
  std::vector<const Json *> pending;
  if (const auto entities = ownerDocument.find("entities");
      entities != ownerDocument.end() && entities->is_array())
    pending.push_back(&*entities);
  while (!pending.empty()) {
    const Json &entities = *pending.back();
    pending.pop_back();
    for (const Json &entity : entities) {
      if (!entity.is_object())
        continue;
      if (entity.contains("prefab"))
        consider(entity);
      if (const auto children = entity.find("children");
          children != entity.end() && children->is_array())
        pending.push_back(&*children);
    }
  }
}

std::optional<PrefabEntityOrigin>
PrefabOriginIndex::find(const std::string_view expandedEntityId) const {
  if (instances_.empty())
    return std::nullopt;
  auto separator = expandedEntityId.rfind('/');
  while (separator != std::string_view::npos) {
    const std::string prefix(expandedEntityId.substr(0, separator));
    if (separator + 1 < expandedEntityId.size() && instances_.contains(prefix))
      return PrefabEntityOrigin{prefix, std::string(expandedEntityId.substr(separator + 1))};
    if (separator == 0)
      break;
    separator = expandedEntityId.rfind('/', separator - 1);
  }
  return std::nullopt;
}

std::optional<PrefabEntityOrigin>
prefabEntityOrigin(const Json &ownerDocument, const std::string_view expandedEntityId) {
  return PrefabOriginIndex(ownerDocument).find(expandedEntityId);
}

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
                            const Json &sceneDocument, bool compileFractures) {
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
  if (expanded.contains("entities")) {
    try {
      expanded["entities"] = flattenEntityHierarchy(expanded["entities"]);
    } catch (const std::exception &error) {
      result.diagnostics.push_back({.severity = Severity::Error,
          .code = "ENTITY_HIERARCHY_INVALID", .message = error.what(),
          .path = scenePath.string()});
      result.document.reset();
      return result;
    }
  }
  const Json authoredEntities = expanded.value("entities", Json::array());
  const bool prefabSource = sceneDocument.value("id", std::string{}).starts_with("prefab://");
  ExpansionContext context(result.diagnostics,
      prefabSource ? std::filesystem::weakly_canonical(scenePath) : std::filesystem::path{},
      prefabSource ? std::optional<Json>(sceneDocument) : std::nullopt, compileFractures);
  if (expanded.contains("entities") && expanded["entities"].is_array()) {
    Json resolved = Json::array();
    for (const Json &entity : authoredEntities) {
      if (entity.contains("prefab")) {
        for (auto &item : context.expandInstance(scenePath, entity, {}))
          resolved.push_back(std::move(item));
        if (!expanded.contains("prefab_origins"))
          expanded["prefab_origins"] = Json::array();
        expanded["prefab_origins"].push_back(entity);
      } else {
        resolved.push_back(entity);
      }
    }
    expanded["entities"] = std::move(resolved);
  }
  if (expanded.contains("instances") && expanded["instances"].is_array()) {
    if (!expanded.contains("entities") || !expanded["entities"].is_array())
      expanded["entities"] = Json::array();
    for (const Json &instance : expanded["instances"]) {
      for (Json &item : context.expandInstance(scenePath, instance, {})) {
        expanded["entities"].push_back(std::move(item));
      }
    }
  }
  if (expanded.contains("instances")) {
    if (!expanded.contains("prefab_origins"))
      expanded["prefab_origins"] = Json::array();
    for (const auto &instance : expanded["instances"])
      expanded["prefab_origins"].push_back(instance);
    expanded.erase("instances");
  }
  if (!compileFractures && expanded.contains("entities")) {
    for (const auto &entity : authoredEntities)
      for (auto &preview : context.expandPlacement(scenePath, entity))
        expanded["entities"].push_back(std::move(preview));
  }
  if (!compileFractures && expanded.contains("entities"))
    expanded["entities"] = assets::expandMasonry(expanded["entities"], true);
  if (compileFractures && expanded.contains("entities")) {
    try {
      expanded["entities"] = assets::compileEntityFractures(
          findProjectRoot(scenePath).value_or(scenePath.parent_path()), expanded["entities"]);
    } catch (const std::exception &error) {
      result.diagnostics.push_back({.severity = Severity::Error,
          .code = "FRACTURE_GENERATION_FAILED", .message = error.what(), .path = scenePath.string()});
    }
  }
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

ExpansionResult bakeFracturePrefab(const std::filesystem::path &path) {
  Diagnostics diagnostics;
  const auto source = readJson(path, diagnostics);
  if (!source) {
    return {.document = std::nullopt, .diagnostics = std::move(diagnostics)};
  }
  return bakeFracturePrefab(path, *source);
}
ExpansionResult bakeFracturePrefab(const std::filesystem::path &path,
                                  const Json &source) {
  if (!source.is_object() ||
      (!assets::hasFractureAuthoring(source) && !assets::hasMasonryAuthoring(source) &&
       !hasPrefabComposition(source))) {
    return {.document = std::nullopt,
            .diagnostics = {{.severity = Severity::Error, .code = "FRACTURE_AUTHORING_REQUIRED",
                             .message = "Expected a prefab with fracture or masonry components, directly or through nested prefabs.",
                             .path = path.string()}}};
  }
  return preparePrefabDocument(path, source);
}

bool hasPrefabComposition(const Json &document) {
  if (document.is_array()) {
    return std::ranges::any_of(document, hasPrefabComposition);
  }
  if (!document.is_object()) {
    return false;
  }
  if (document.contains("prefab") || document.contains("instances")) {
    return true;
  }
  for (const char *field : {"entities", "children"}) {
    if (document.contains(field) && hasPrefabComposition(document.at(field))) {
      return true;
    }
  }
  return false;
}

ExpansionResult preparePrefabDocument(const std::filesystem::path &path,
                                      const Json &source) {
  ExpansionResult result{.document = Json::object(), .diagnostics = {}};
  if (!source.is_object() || !source.contains("format_version") ||
      source["format_version"] != 1 || !source.contains("id") ||
      !source["id"].is_string() || !source["id"].get<std::string>().starts_with("prefab://")) {
    result.diagnostics.push_back({.severity = Severity::Error,
                                  .code = "PREFAB_INVALID_DOCUMENT",
                                  .message = "Expected a versioned prefab document.",
                                  .path = path.string()});
    result.document.reset();
    return result;
  }

  ExpansionContext context(result.diagnostics, std::filesystem::weakly_canonical(path), source);
  auto entities = context.expandInstance(path, {{"id", "preview"}, {"prefab", source["id"]}}, {});
  if (hasErrors(result.diagnostics)) {
    result.document.reset();
    return result;
  }

  std::unordered_map<std::string, std::string> ids;
  for (const auto &entity : entities) {
    const auto id = entity.at("id").get<std::string>();
    ids.emplace(id, id.substr(std::string("preview/").size()));
    collectDeferredIds(entity, [&](const std::string &leaf) {
      const auto separator = leaf.find('/');
      if (separator == std::string::npos) {
        throw std::runtime_error("Invalid prepared deferred identity");
      }
      ids.emplace(leaf, leaf.substr(separator + 1));
    });
  }
  bool containsFracture = false;
  for (auto &entity : entities) {
    entity["id"] = ids.at(entity["id"].get<std::string>());
    remapEntityReferences(entity, ids);
    containsFracture = containsFracture || entity["components"].contains("Destructible3D");
  }
  *result.document = {{"format_version", 1}, {"id", source["id"]}, {"entities", std::move(entities)}};
  if (source.contains("name")) {
    (*result.document)["name"] = source["name"];
  }
  if (containsFracture) {
    // The prepared path remains cheap. Keep the compact recipe for meaningful
    // runtime overrides instead of applying authoring edits to generated shards.
    (*result.document)["source_recipe"] = source.value("source_recipe", source);
  }
  return result;
}
} // namespace demi::runtime::composition
