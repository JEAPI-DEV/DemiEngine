#include "demi/runtime/ui/UiPrefabResolver.h"
#include "demi/assets/PackageContent.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/packages/PackageManifest.h"
#include "demi/runtime/ui/UiPrefabHierarchy.h"
#include "demi/runtime/ui/UiPrefabOverrides.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <ranges>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime::ui {
namespace {

using Json = nlohmann::json;

std::optional<std::filesystem::path>
findProjectRoot(const std::filesystem::path &sourcePath,
                bool includePackage = false) {
  std::filesystem::path cursor = sourcePath.parent_path();
  while (!cursor.empty()) {
    if (std::filesystem::exists(cursor / "demi.project.json") ||
        (includePackage &&
         std::filesystem::exists(cursor / "demi.package.json")))
      return cursor;
    const std::filesystem::path parent = cursor.parent_path();
    if (parent == cursor)
      break;
    cursor = parent;
  }
  return std::nullopt;
}

std::optional<Json> readJson(const std::filesystem::path &path,
                             Diagnostics &diagnostics) {
  std::ifstream input(path);
  if (!input) {
    diagnostics.push_back({
        .severity = Severity::Error,
        .code = "UI_PREFAB_READ_FAILED",
        .message = "Failed to read UI prefab: " + path.string(),
        .path = path.string(),
        .suggestion = "Create the referenced .ui.prefab.json file.",
    });
    return std::nullopt;
  }
  try {
    return Json::parse(input);
  } catch (const Json::parse_error &error) {
    diagnostics.push_back({
        .severity = Severity::Error,
        .code = "UI_PREFAB_INVALID_JSON",
        .message = error.what(),
        .path = path.string(),
        .suggestion = "Fix the UI prefab JSON syntax.",
    });
    return std::nullopt;
  }
}

Json nodeProperties(const Json &node) {
  Json properties = Json::object();
  for (const auto &[key, value] : node.items())
    if (key != "children")
      properties[key] = value;
  return properties;
}

bool matchesType(const Json &value, const std::string_view type) {
  if (type == "string")
    return value.is_string();
  if (type == "number")
    return value.is_number();
  if (type == "integer")
    return value.is_number_integer();
  if (type == "boolean")
    return value.is_boolean();
  if (type == "array")
    return value.is_array();
  if (type == "object")
    return value.is_object();
  return false;
}

std::string scalarText(const Json &value) {
  if (value.is_string())
    return value.get<std::string>();
  if (value.is_boolean())
    return value.get<bool>() ? "true" : "false";
  if (value.is_number())
    return value.dump();
  return {};
}

void substitute(Json &value,
                const std::unordered_map<std::string, Json> &arguments) {
  if (value.is_object()) {
    for (auto &item : value.items())
      substitute(item.value(), arguments);
    return;
  }
  if (value.is_array()) {
    for (Json &item : value)
      substitute(item, arguments);
    return;
  }
  if (!value.is_string())
    return;

  const std::string original = value.get<std::string>();
  if (original.size() > 3 && original.starts_with("${") &&
      original.ends_with('}') && original.find("${", 2) == std::string::npos) {
    const std::string name = original.substr(2, original.size() - 3);
    if (const auto found = arguments.find(name); found != arguments.end()) {
      value = found->second;
      return;
    }
  }

  std::string expanded = original;
  for (const auto &[name, argument] : arguments) {
    const std::string marker = "${" + name + "}";
    const std::string replacement = scalarText(argument);
    if (replacement.empty() && !argument.is_string())
      continue;
    for (std::size_t position = expanded.find(marker);
         position != std::string::npos;
         position = expanded.find(marker, position + replacement.size()))
      expanded.replace(position, marker.size(), replacement);
  }
  value = std::move(expanded);
}

bool containsParameterMarker(const Json &value) {
  if (value.is_string())
    return value.get_ref<const std::string &>().find("${") != std::string::npos;
  if (value.is_array())
    return std::ranges::any_of(value, containsParameterMarker);
  if (value.is_object())
    return std::ranges::any_of(value.items(), [](const auto &item) {
      return containsParameterMarker(item.value());
    });
  return false;
}

std::unordered_map<std::string, std::string>
remapIds(Json &node, const std::string &instanceId) {
  if (!node.is_object())
    return {};
  std::unordered_map<std::string, std::string> ids;
  std::vector<Json *> nodes;
  const auto collect = [&](const auto &self, Json &current) -> void {
    if (!current.is_object())
      return;
    nodes.push_back(&current);
    if (current.contains("id") && current["id"].is_string()) {
      const std::string local = current["id"].get<std::string>();
      ids.emplace(local,
                  nodes.size() == 1 ? instanceId : instanceId + "." + local);
    }
    if (current.contains("children") && current["children"].is_array())
      for (Json &child : current["children"])
        self(self, child);
  };
  collect(collect, node);
  std::unordered_set<std::string> actions;
  for (const auto *current : nodes)
    if (current->contains("action_effects") &&
        (*current)["action_effects"].is_object())
      for (const auto &[action, effect] : (*current)["action_effects"].items())
        actions.insert(action);
  for (Json *current : nodes) {
    if (current->contains("action") && (*current)["action"].is_string() &&
        actions.contains((*current)["action"].get<std::string>()))
      (*current)["action"] =
          instanceId + "." + (*current)["action"].get<std::string>();
    if (current->contains("action_effects") &&
        (*current)["action_effects"].is_object()) {
      Json mapped = Json::object();
      for (const auto &[action, effect] :
           (*current)["action_effects"].items()) {
        auto value = effect;
        if (value.is_object()) {
          for (const auto *field : {"show", "hide"})
            if (value.contains(field) && value[field].is_array())
              for (auto &id : value[field])
                if (id.is_string())
                  if (const auto found = ids.find(id.get<std::string>());
                      found != ids.end())
                    id = found->second;
          if (value.contains("focus") && value["focus"].is_string())
            if (const auto found = ids.find(value["focus"].get<std::string>());
                found != ids.end())
              value["focus"] = found->second;
        }
        mapped[instanceId + "." + action] = std::move(value);
      }
      (*current)["action_effects"] = std::move(mapped);
    }
    if (current->contains("id") && (*current)["id"].is_string()) {
      const auto replacement = ids.find((*current)["id"].get<std::string>());
      if (replacement != ids.end())
        (*current)["id"] = replacement->second;
    }
    if (current->contains("parent") && (*current)["parent"].is_string()) {
      const auto replacement =
          ids.find((*current)["parent"].get<std::string>());
      if (replacement != ids.end())
        (*current)["parent"] = replacement->second;
    }
  }
  return ids;
}

class ExpansionContext {
public:
  ExpansionContext(
      Diagnostics &diagnostics,
      std::unordered_map<std::string, UiPrefabNodeOrigin> &origins,
      std::unordered_map<std::string, Json> &instanceArguments,
      std::unordered_map<std::string, UiAuthoredNodeSource> &authoredNodes,
      std::unordered_set<std::string> &reservedIds,
      std::unordered_map<std::string, Json> &authoringProperties,
      std::unordered_set<std::string> &removedIds)
      : diagnostics_(diagnostics), origins_(origins),
        instanceArguments_(instanceArguments), authoredNodes_(authoredNodes),
        reservedIds_(reservedIds), authoringProperties_(authoringProperties),
        removedIds_(removedIds) {}

  std::optional<Json>
  expandNode(const std::filesystem::path &ownerPath, Json node,
             std::optional<std::string> authoredPointer = std::nullopt) {
    if (!node.is_object()) {
      report(ownerPath, "UI_PREFAB_NODE_INVALID",
             "UI nodes and prefab instances must be objects.");
      return std::nullopt;
    }
    if (node.contains("prefab") && node.contains("type")) {
      report(ownerPath, "UI_PREFAB_NODE_AMBIGUOUS",
             "A UI node must declare either type or prefab, not both.");
      return std::nullopt;
    }
    if (node.contains("prefab"))
      return expandInstance(ownerPath, node, authoredPointer);
    if (node.contains("node_overrides")) {
      report(ownerPath, "UI_PREFAB_NODE_OVERRIDES_INVALID",
             "node_overrides requires a UI prefab instance.");
      return std::nullopt;
    }
    // Shorthand-only forms (dock/stack/at/pad) carry no id/type yet; default
    // them here so terse HUD files validate: containers default the type,
    // ids are synthesized from the sibling position by the caller below.
    if (!node.contains("type") || !node["type"].is_string()) {
      if (node.contains("children") || node.contains("dock") ||
          node.contains("stack"))
        node["type"] = "container";
    }
    if (!node.contains("id") || !node["id"].is_string() ||
        node["id"].get<std::string>().empty() || !node.contains("type") ||
        !node["type"].is_string()) {
      report(ownerPath, "UI_PREFAB_NODE_INVALID",
             "Expanded UI nodes require non-empty string id and type fields.");
      return std::nullopt;
    }
    reservedIds_.insert(node["id"].get<std::string>());
    if (authoredPointer) {
      authoredNodes_[node["id"].get<std::string>()] = {*authoredPointer};
      authoringProperties_[node["id"].get<std::string>()] =
          nodeProperties(node);
    }
    if (node.contains("children")) {
      if (!node["children"].is_array()) {
        report(ownerPath, "UI_PREFAB_CHILDREN_INVALID",
               "UI node children must be an array.");
        return std::nullopt;
      }
      Json children = Json::array();
      for (std::size_t index = 0; index < node["children"].size(); ++index) {
        const auto childPointer =
            authoredPointer
                ? std::optional<std::string>(*authoredPointer + "/children/" +
                                             std::to_string(index))
                : std::nullopt;
        const auto expanded =
            expandNode(ownerPath, node["children"][index], childPointer);
        if (expanded.has_value())
          children.push_back(*expanded);
      }
      node["children"] = std::move(children);
    }
    return node;
  }

private:
  struct MetadataScope {
    ExpansionContext &context;
    std::unordered_map<std::string, UiPrefabNodeOrigin> origins;
    std::unordered_map<std::string, Json> arguments;
    std::unordered_map<std::string, UiAuthoredNodeSource> sources;
    std::unordered_set<std::string> reserved, removed;
    std::unordered_map<std::string, Json> properties;
    bool committed = false;
    explicit MetadataScope(ExpansionContext &c)
        : context(c), origins(std::move(c.origins_)),
          arguments(std::move(c.instanceArguments_)),
          sources(std::move(c.authoredNodes_)),
          reserved(std::move(c.reservedIds_)),
          properties(std::move(c.authoringProperties_)),
          removed(std::move(c.removedIds_)) {
      c.origins_.clear();
      c.instanceArguments_.clear();
      c.authoredNodes_.clear();
      c.reservedIds_.clear();
      c.authoringProperties_.clear();
      c.removedIds_.clear();
    }
    void commit() {
      context.origins_.merge(origins);
      context.instanceArguments_.merge(arguments);
      context.authoredNodes_.merge(sources);
      context.reservedIds_.merge(reserved);
      context.authoringProperties_.merge(properties);
      context.removedIds_.merge(removed);
      committed = true;
    }
    ~MetadataScope() {
      if (!committed) {
        context.origins_ = std::move(origins);
        context.instanceArguments_ = std::move(arguments);
        context.authoredNodes_ = std::move(sources);
        context.reservedIds_ = std::move(reserved);
        context.authoringProperties_ = std::move(properties);
        context.removedIds_ = std::move(removed);
      }
    }
  };
  std::optional<Json>
  expandInstance(const std::filesystem::path &ownerPath, const Json &instance,
                 const std::optional<std::string> &authoredPointer) {
    if (!instance.contains("id") || !instance["id"].is_string() ||
        instance["id"].get<std::string>().empty() ||
        !instance["prefab"].is_string()) {
      report(
          ownerPath, "UI_PREFAB_INSTANCE_INVALID",
          "UI prefab instances require non-empty string id and prefab fields.");
      return std::nullopt;
    }
    for (const auto &[field, value] : instance.items())
      if (field != "id" && field != "prefab" && field != "arguments" &&
          field != "overrides" && field != "node_overrides") {
        report(ownerPath, "UI_PREFAB_INSTANCE_FIELD_INVALID",
               "UI prefab root properties belong under overrides.$root: " +
                   field);
        return std::nullopt;
      }
    const std::string reference = instance["prefab"].get<std::string>();
    const auto resolved = resolveUiPrefabReference(ownerPath, reference);
    if (!resolved.has_value()) {
      report(ownerPath, "UI_PREFAB_REFERENCE_INVALID",
             "Could not resolve UI prefab reference: " + reference);
      return std::nullopt;
    }
    std::error_code canonicalError;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(*resolved, canonicalError);
    if (canonicalError) {
      report(ownerPath, "UI_PREFAB_REFERENCE_INVALID",
             "Could not canonicalize UI prefab reference: " + reference);
      return std::nullopt;
    }
    if (active_.contains(canonical)) {
      std::ostringstream chain;
      for (const auto &path : stack_)
        chain << path.string() << " -> ";
      chain << canonical.string();
      report(ownerPath, "UI_PREFAB_CYCLE",
             "UI prefab cycle detected: " + chain.str());
      return std::nullopt;
    }
    const auto prefab = readJson(canonical, diagnostics_);
    if (!prefab.has_value())
      return std::nullopt;
    if (!prefab->contains("format_version") ||
        !(*prefab)["format_version"].is_number_integer() ||
        (*prefab)["format_version"].get<int>() != 1 ||
        !prefab->contains("id") || !(*prefab)["id"].is_string() ||
        !(*prefab)["id"].get<std::string>().starts_with("ui-prefab://") ||
        !prefab->contains("root") || !(*prefab)["root"].is_object()) {
      report(canonical, "UI_PREFAB_DOCUMENT_INVALID",
             "UI prefab requires format_version 1, a ui-prefab:// id, and a "
             "root node.");
      return std::nullopt;
    }

    const Json supplied = instance.value("arguments", Json::object());
    const Json specs = prefab->value("parameters", Json::object());
    if (!supplied.is_object() || !specs.is_object()) {
      report(ownerPath, "UI_PREFAB_ARGUMENTS_INVALID",
             "UI prefab parameters and instance arguments must be objects.");
      return std::nullopt;
    }
    std::unordered_map<std::string, Json> arguments;
    for (const auto &[name, value] : supplied.items()) {
      if (!specs.contains(name)) {
        report(ownerPath, "UI_PREFAB_ARGUMENT_UNKNOWN",
               "Unknown UI prefab argument: " + name);
        continue;
      }
      arguments[name] = value;
    }
    for (const auto &[name, spec] : specs.items()) {
      if (!spec.is_object() || !spec.contains("type") ||
          !spec["type"].is_string()) {
        report(canonical, "UI_PREFAB_PARAMETER_INVALID",
               "UI prefab parameter requires a supported type: " + name);
        continue;
      }
      if (!arguments.contains(name)) {
        if (spec.contains("default"))
          arguments[name] = spec["default"];
        else {
          report(ownerPath, "UI_PREFAB_ARGUMENT_MISSING",
                 "Missing required UI prefab argument: " + name);
          continue;
        }
      }
      if (!matchesType(arguments.at(name), spec["type"].get<std::string>()))
        report(ownerPath, "UI_PREFAB_ARGUMENT_TYPE",
               "UI prefab argument has the wrong type: " + name);
    }
    if (hasErrors(diagnostics_))
      return std::nullopt;

    MetadataScope metadata(*this);
    active_.insert(canonical);
    stack_.push_back(canonical);
    Json root = (*prefab)["root"];
    substitute(root, arguments);
    if (containsParameterMarker(root)) {
      report(
          canonical, "UI_PREFAB_PARAMETER_UNRESOLVED",
          "UI prefab contains an undeclared or unresolved parameter marker.");
      stack_.pop_back();
      active_.erase(canonical);
      return std::nullopt;
    }
    const auto nested = expandNode(canonical, std::move(root));
    stack_.pop_back();
    active_.erase(canonical);
    if (!nested.has_value())
      return std::nullopt;
    root = *nested;
    const auto oldRootId = root.at("id").get<std::string>();
    const auto instanceId = instance["id"].get<std::string>();
    const auto ids = remapIds(root, instanceId);
    std::unordered_set<std::string> reserved, removed;
    for (const auto &id : reservedIds_)
      reserved.insert(id == oldRootId ? instanceId : instanceId + "." + id);
    for (const auto &id : removedIds_)
      removed.insert(id == oldRootId ? instanceId : instanceId + "." + id);
    reservedIds_ = std::move(reserved);
    removedIds_ = std::move(removed);
    origins_.clear();
    authoredNodes_.clear();
    instanceArguments_.clear();
    authoringProperties_.clear();
    if (authoredPointer) {
      instanceArguments_[instanceId] = arguments;
      authoredNodes_[instanceId] = {*authoredPointer};
      for (const auto &[local, qualified] : ids)
        origins_[qualified] = {instanceId,
                               local == oldRootId ? std::string{} : local,
                               canonical, *authoredPointer, *authoredPointer};
    }
    const auto expandChild = [&](Json child, const std::string &target,
                                 std::size_t index) {
      std::optional<std::string> pointer;
      if (authoredPointer) {
        auto path = Json::json_pointer(*authoredPointer);
        path /= "overrides";
        path /= target;
        path /= "children";
        path /= index;
        pointer = path.to_string();
      }
      return expandNode(ownerPath, std::move(child), pointer);
    };
    if (!applyUiPrefabOverrides(root, instance, ownerPath, diagnostics_,
                                expandChild, removedIds_, ids))
      return std::nullopt;
    const auto remember = [&](auto &&self, const Json &node) -> void {
      authoringProperties_[node.at("id").get<std::string>()] =
          nodeProperties(node);
      if (node.contains("children"))
        for (const auto &child : node["children"])
          self(self, child);
    };
    remember(remember, root);
    metadata.commit();
    return root;
  }

  void report(const std::filesystem::path &path, std::string code,
              std::string message) {
    diagnostics_.push_back({
        .severity = Severity::Error,
        .code = std::move(code),
        .message = std::move(message),
        .path = path.string(),
        .suggestion =
            "Inspect the UI prefab reference, parameters, and nesting chain.",
    });
  }

  Diagnostics &diagnostics_;
  std::unordered_map<std::string, UiPrefabNodeOrigin> &origins_;
  std::unordered_map<std::string, Json> &instanceArguments_;
  std::unordered_map<std::string, UiAuthoredNodeSource> &authoredNodes_;
  std::unordered_set<std::string> &reservedIds_;
  std::unordered_set<std::string> &removedIds_;
  std::unordered_map<std::string, Json> &authoringProperties_;
  std::set<std::filesystem::path> active_;
  std::vector<std::filesystem::path> stack_;
};

void validateActions(const Json &node, const std::filesystem::path &path,
                     Diagnostics &diagnostics) {
  const auto effects = node.find("action_effects");
  if (effects == node.end())
    return;
  bool valid = effects->is_object();
  if (valid)
    for (const auto &[action, effect] : effects->items()) {
      if (action.empty() || !effect.is_object()) {
        valid = false;
        break;
      }
      for (const auto &[field, value] : effect.items()) {
        if (field == "focus")
          valid = valid && value.is_string();
        else if (field == "show" || field == "hide")
          valid = valid && value.is_array() &&
                  std::ranges::all_of(value, [](const Json &id) {
                    return id.is_string() &&
                           !id.get_ref<const std::string &>().empty();
                  });
        else
          valid = false;
      }
    }
  if (!valid)
    diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "UI_ACTION_EFFECTS_INVALID",
         .message = "UI action effects require named objects with show/hide ID "
                    "arrays and optional string focus.",
         .path = path.string()});
}

void collectIds(const Json &node, std::unordered_set<std::string> &ids,
                const std::filesystem::path &path, Diagnostics &diagnostics) {
  if (!node.is_object())
    return;
  validateActions(node, path, diagnostics);
  if (node.contains("id") && node["id"].is_string()) {
    const std::string id = node["id"].get<std::string>();
    if (!ids.insert(id).second)
      diagnostics.push_back({
          .severity = Severity::Error,
          .code = "UI_PREFAB_ID_COLLISION",
          .message = "Expanded UI contains duplicate node id: " + id,
          .path = path.string(),
          .suggestion = "Use unique instance and local node ids.",
      });
  }
  if (node.contains("children") && node["children"].is_array())
    for (const Json &child : node["children"])
      collectIds(child, ids, path, diagnostics);
}

} // namespace

std::optional<std::filesystem::path>
resolveUiPrefabReference(const std::filesystem::path &sourcePath,
                         const std::string_view reference) {
  constexpr std::string_view Prefix = "ui-prefab://";
  if (!reference.starts_with(Prefix) || reference.size() == Prefix.size())
    return std::nullopt;
  const auto projectRoot = findProjectRoot(sourcePath, true);
  if (!projectRoot.has_value())
    return std::nullopt;
  std::filesystem::path relative(reference.substr(Prefix.size()));
  if (relative.is_absolute() ||
      std::ranges::any_of(relative,
                          [](const auto &part) { return part == ".."; }))
    return std::nullopt;
  relative += ".ui.prefab.json";
  const auto local = *projectRoot / "ui" / relative;
  if (std::filesystem::is_regular_file(local)) {
    const auto manifestPath = *projectRoot / "demi.package.json";
    if (std::filesystem::is_regular_file(manifestPath)) {
      const auto loaded = packages::loadPackageManifest(manifestPath);
      const auto declaredPath =
          local.lexically_relative(*projectRoot).generic_string();
      if (!loaded.manifest ||
          std::ranges::find(loaded.manifest->files, declaredPath) ==
              loaded.manifest->files.end())
        return std::nullopt;
    }
    return local;
  }
  // Package sources resolve their own nested prefabs conventionally. A project
  // can additionally consume declared, verified content from its installed
  // lock.
  const auto hostRoot = findProjectRoot(sourcePath);
  if (!hostRoot)
    return local;
  const auto content = assets::loadLockedPackageContent(*hostRoot, "");
  if (hasErrors(content.diagnostics))
    return std::nullopt;
  std::optional<std::filesystem::path> match;
  for (const auto &file : content.files) {
    if (!isUiPrefabFile(file))
      continue;
    std::ifstream input(file);
    const auto document = Json::parse(input, nullptr, false);
    if (!document.is_object() || !document.contains("id") ||
        !document["id"].is_string() ||
        document["id"].get<std::string>() != reference)
      continue;
    if (match && *match != file)
      return std::nullopt; // Ambiguous package identities must not depend on
                           // order.
    match = file;
  }
  return match ? match : std::optional<std::filesystem::path>(local);
}

UiPrefabExpansionResult
expandUiDocument(const std::filesystem::path &hudPath, const Json &hudDocument,
                 const std::unordered_set<std::string> &externalParents) {
  UiPrefabExpansionResult result{.document = hudDocument, .diagnostics = {}};
  if (hudDocument.is_object() && !hudDocument.contains("root") &&
      hudDocument.contains("children") && hudDocument["children"].is_array()) {
    (*result.document)["root"] = {{"id", "ui_root"},
                                  {"type", "container"},
                                  {"anchor_min", {0, 0}},
                                  {"anchor_max", {1, 1}},
                                  {"children", hudDocument["children"]}};
    result.document->erase("children");
  }
  if (!result.document->is_object() || !result.document->contains("root")) {
    result.document.reset();
    result.diagnostics.push_back({
        .severity = Severity::Error,
        .code = "UI_DOCUMENT_INVALID",
        .message = "HUD must be an object with a root node.",
        .path = hudPath.string(),
        .suggestion = "Add a root UI node.",
    });
    return result;
  }
  validateActions(hudDocument, hudPath, result.diagnostics);
  ExpansionContext context(result.diagnostics, result.origins,
                           result.instanceArguments, result.authoredNodes,
                           result.reservedIds, result.authoringProperties,
                           result.removedIds);
  const auto root =
      context.expandNode(hudPath, (*result.document)["root"],
                         hudDocument.contains("root") ? "/root" : "");
  if (root.has_value()) {
    (*result.document)["root"] = *root;
    (void)composeUiHierarchy((*result.document)["root"], result.removedIds,
                             externalParents, hudPath, result.diagnostics);
    std::unordered_set<std::string> ids;
    collectIds((*result.document)["root"], ids, hudPath, result.diagnostics);
    for (const auto &[id, origin] : result.origins)
      if (!ids.contains(id))
        result.removedOrigins[id] = origin;
    for (const auto &[id, source] : result.authoredNodes)
      if (!ids.contains(id))
        result.removedSources[id] = source;
    std::erase_if(result.origins,
                  [&](const auto &item) { return !ids.contains(item.first); });
    std::erase_if(result.authoredNodes,
                  [&](const auto &item) { return !ids.contains(item.first); });
    std::erase_if(result.instanceArguments,
                  [&](const auto &item) { return !ids.contains(item.first); });
    std::erase_if(result.authoringProperties,
                  [&](const auto &item) { return !ids.contains(item.first); });
  }
  if (!root.has_value() || hasErrors(result.diagnostics)) {
    result.document.reset();
    result.origins.clear();
    result.instanceArguments.clear();
    result.authoredNodes.clear();
    result.reservedIds.clear();
    result.authoringProperties.clear();
  }
  return result;
}

UiPrefabExpansionResult
inspectUiPrefab(const std::filesystem::path &prefabPath) {
  UiPrefabExpansionResult result;
  const auto prefab = readJson(prefabPath, result.diagnostics);
  if (!prefab.has_value())
    return result;
  if (!prefab->contains("id") || !(*prefab)["id"].is_string()) {
    result.diagnostics.push_back({
        .severity = Severity::Error,
        .code = "UI_PREFAB_DOCUMENT_INVALID",
        .message = "UI prefab requires a ui-prefab:// id.",
        .path = prefabPath.string(),
        .suggestion = "Add a stable ui-prefab:// id.",
    });
    return result;
  }
  Json arguments = Json::object();
  if (prefab->contains("parameters") && (*prefab)["parameters"].is_object()) {
    for (const auto &[name, spec] : (*prefab)["parameters"].items()) {
      if (spec.is_object() && spec.contains("default"))
        arguments[name] = spec["default"];
      else if (!spec.is_object() || !spec.contains("type") ||
               !spec["type"].is_string())
        continue;
      else if (spec["type"] == "string")
        arguments[name] = "preview";
      else if (spec["type"] == "number")
        arguments[name] = 0.0;
      else if (spec["type"] == "integer")
        arguments[name] = 0;
      else if (spec["type"] == "boolean")
        arguments[name] = false;
      else if (spec["type"] == "array")
        arguments[name] = Json::array();
      else if (spec["type"] == "object")
        arguments[name] = Json::object();
    }
  }
  Json synthetic = {
      {"format_version", 1},
      {"root",
       {{"id", "preview"},
        {"prefab", (*prefab)["id"]},
        {"arguments", std::move(arguments)}}},
  };
  return expandUiDocument(prefabPath, synthetic);
}

} // namespace demi::runtime::ui
