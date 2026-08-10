#include "demi/runtime/ui/UiPrefabResolver.h"

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
findProjectRoot(const std::filesystem::path &sourcePath) {
  std::filesystem::path cursor = sourcePath.parent_path();
  while (!cursor.empty()) {
    if (std::filesystem::exists(cursor / "demi.project.json"))
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

void remapIds(Json &node, const std::string &instanceId) {
  if (!node.is_object())
    return;
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
  for (Json *current : nodes) {
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
}

class ExpansionContext {
public:
  explicit ExpansionContext(Diagnostics &diagnostics)
      : diagnostics_(diagnostics) {}

  std::optional<Json> expandNode(const std::filesystem::path &ownerPath,
                                 Json node) {
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
      return expandInstance(ownerPath, node);
    if (!node.contains("id") || !node["id"].is_string() ||
        node["id"].get<std::string>().empty() || !node.contains("type") ||
        !node["type"].is_string()) {
      report(ownerPath, "UI_PREFAB_NODE_INVALID",
             "Expanded UI nodes require non-empty string id and type fields.");
      return std::nullopt;
    }
    if (node.contains("children")) {
      if (!node["children"].is_array()) {
        report(ownerPath, "UI_PREFAB_CHILDREN_INVALID",
               "UI node children must be an array.");
        return std::nullopt;
      }
      Json children = Json::array();
      for (Json child : node["children"]) {
        const auto expanded = expandNode(ownerPath, std::move(child));
        if (expanded.has_value())
          children.push_back(*expanded);
      }
      node["children"] = std::move(children);
    }
    return node;
  }

private:
  std::optional<Json> expandInstance(const std::filesystem::path &ownerPath,
                                     const Json &instance) {
    if (!instance.contains("id") || !instance["id"].is_string() ||
        instance["id"].get<std::string>().empty() ||
        !instance["prefab"].is_string()) {
      report(
          ownerPath, "UI_PREFAB_INSTANCE_INVALID",
          "UI prefab instances require non-empty string id and prefab fields.");
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
    if (instance.contains("overrides")) {
      if (!instance["overrides"].is_object()) {
        report(ownerPath, "UI_PREFAB_OVERRIDES_INVALID",
               "UI prefab overrides must be an object.");
        return std::nullopt;
      }
      for (const auto &[field, value] : instance["overrides"].items()) {
        if (field == "id" || field == "children" || field == "prefab") {
          report(ownerPath, "UI_PREFAB_OVERRIDE_RESERVED",
                 "UI prefab override cannot replace reserved field: " + field);
          continue;
        }
        root[field] = value;
      }
    }
    remapIds(root, instance["id"].get<std::string>());
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
  std::set<std::filesystem::path> active_;
  std::vector<std::filesystem::path> stack_;
};

void collectIds(const Json &node, std::unordered_set<std::string> &ids,
                const std::filesystem::path &path, Diagnostics &diagnostics) {
  if (!node.is_object())
    return;
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
  const auto projectRoot = findProjectRoot(sourcePath);
  if (!projectRoot.has_value())
    return std::nullopt;
  std::filesystem::path relative(reference.substr(Prefix.size()));
  if (relative.is_absolute() ||
      std::ranges::any_of(relative,
                          [](const auto &part) { return part == ".."; }))
    return std::nullopt;
  relative += ".ui.prefab.json";
  return *projectRoot / "ui" / relative;
}

UiPrefabExpansionResult expandUiDocument(const std::filesystem::path &hudPath,
                                         const Json &hudDocument) {
  UiPrefabExpansionResult result{.document = hudDocument, .diagnostics = {}};
  if (!hudDocument.is_object() || !hudDocument.contains("root")) {
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
  ExpansionContext context(result.diagnostics);
  const auto root = context.expandNode(hudPath, (*result.document)["root"]);
  if (root.has_value()) {
    (*result.document)["root"] = *root;
    std::unordered_set<std::string> ids;
    collectIds(*root, ids, hudPath, result.diagnostics);
  }
  if (!root.has_value() || hasErrors(result.diagnostics))
    result.document.reset();
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
