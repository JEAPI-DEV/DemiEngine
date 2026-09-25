#include "editor/EditorHudDocument.h"

#include "editor/EditorAuthoredJson.h"
#include "editor/EditorSpecializedDocument.h"

#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/scene/HudParser.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/ui/UiPrefabResolver.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace demi::editor {
namespace {

using Json = nlohmann::json;

int authoredDecimalPlaces(const std::string_view field) {
  if (field == "anchor_min" || field == "anchor_max")
    return 2;
  if (field == "position" || field == "size" || field == "min_size" ||
      field == "max_size" || field == "margin" || field == "padding" ||
      field == "pad" || field == "gap" || field == "font_size" ||
      field == "line_spacing" || field == "corner_radius" ||
      field == "border_width" || field == "radius")
    return 1;
  return 3;
}

Json *findNode(Json &node, const std::string_view id) {
  if (!node.is_object())
    return nullptr;
  if (node.value("id", "") == id)
    return &node;
  if (auto children = node.find("children");
      children != node.end() && children->is_array())
    for (Json &child : *children)
      if (Json *found = findNode(child, id))
        return found;
  if (auto elements = node.find("elements");
      elements != node.end() && elements->is_array())
    for (Json &child : *elements)
      if (Json *found = findNode(child, id))
        return found;
  return nullptr;
}

const Json *findNode(const Json &node, const std::string_view id) {
  if (!node.is_object())
    return nullptr;
  if (node.value("id", "") == id)
    return &node;
  if (auto children = node.find("children");
      children != node.end() && children->is_array())
    for (const Json &child : *children)
      if (const Json *found = findNode(child, id))
        return found;
  if (auto elements = node.find("elements");
      elements != node.end() && elements->is_array())
    for (const Json &child : *elements)
      if (const Json *found = findNode(child, id))
        return found;
  return nullptr;
}

bool eraseNode(Json &node, const std::string_view id) {
  for (const char *key : {"children", "elements"}) {
    auto children = node.find(key);
    if (children == node.end() || !children->is_array())
      continue;
    for (auto child = children->begin(); child != children->end(); ++child) {
      if (child->is_object() && child->value("id", "") == id) {
        children->erase(child);
        return true;
      }
      if (eraseNode(*child, id))
        return true;
    }
  }
  return false;
}

void collectIds(const Json &node, std::set<std::string> &ids) {
  if (!node.is_object())
    return;
  if (const std::string id = node.value("id", ""); !id.empty())
    ids.insert(id);
  for (const char *key : {"children", "elements"})
    if (auto children = node.find(key);
        children != node.end() && children->is_array())
      for (const Json &child : *children)
        collectIds(child, ids);
}

std::string uniqueId(const std::set<std::string> &ids, std::string base) {
  std::replace(base.begin(), base.end(), ' ', '_');
  if (!ids.contains(base))
    return base;
  for (int suffix = 2;; ++suffix) {
    std::string candidate = base + '_' + std::to_string(suffix);
    if (!ids.contains(candidate))
      return candidate;
  }
}

std::string uniqueId(const Json &root, std::string base) {
  std::set<std::string> ids;
  collectIds(root, ids);
  return uniqueId(ids, std::move(base));
}

struct NodeLocation {
  Json *owner = nullptr;
  Json *siblings = nullptr;
  std::size_t index = 0;
};

std::optional<NodeLocation> findNodeLocation(Json &owner,
                                             const std::string_view id) {
  for (const char *key : {"children", "elements"}) {
    auto children = owner.find(key);
    if (children == owner.end() || !children->is_array())
      continue;
    for (std::size_t index = 0; index < children->size(); ++index) {
      Json &child = (*children)[index];
      if (child.is_object() && child.value("id", "") == id)
        return NodeLocation{.owner = &owner,
                            .siblings = &*children,
                            .index = index};
      if (auto found = findNodeLocation(child, id))
        return found;
    }
  }
  return std::nullopt;
}

Json *authoredRoot(Json &document) {
  return document.contains("root") ? &document["root"] : &document;
}

std::string authoredRootId(const Json &document) {
  return document.contains("root") ? document["root"].value("id", "")
                                   : "ui_root";
}

Json *findAuthoredHudNode(Json &document, const std::string_view id) {
  if (!document.contains("root") && id == "ui_root")
    return &document;
  return findNode(*authoredRoot(document), id);
}

Json &authoredChildren(Json &owner) {
  if (owner.contains("children") && owner["children"].is_array())
    return owner["children"];
  if (owner.contains("elements") && owner["elements"].is_array())
    return owner["elements"];
  owner["children"] = Json::array();
  return owner["children"];
}

void collectSubtreeIds(const Json &node, std::vector<std::string> &ids) {
  if (!node.is_object())
    return;
  if (auto id = node.find("id"); id != node.end() && id->is_string())
    ids.push_back(id->get<std::string>());
  for (const char *key : {"children", "elements"})
    if (auto children = node.find(key);
        children != node.end() && children->is_array())
      for (const Json &child : *children)
        collectSubtreeIds(child, ids);
}

void remapCopiedSubtree(
    Json &node,
    const std::unordered_map<std::string, std::string> &idRemapping) {
  if (!node.is_object())
    return;
  for (const char *field : {"id", "parent"}) {
    auto value = node.find(field);
    if (value == node.end() || !value->is_string())
      continue;
    if (const auto replacement =
            idRemapping.find(value->get<std::string>());
        replacement != idRemapping.end())
      *value = replacement->second;
  }
  for (const char *key : {"children", "elements"})
    if (auto children = node.find(key);
        children != node.end() && children->is_array())
      for (Json &child : *children)
        remapCopiedSubtree(child, idRemapping);
}

Json defaultNode(const std::string_view type, const std::string &id) {
  Json node{{"id", id}, {"type", type}, {"position", {24, 24}}};
  if (type == "label") {
    node["text"] = "Label";
    node["size"] = {160, 32};
  } else if (type == "text") {
    node["text"] = "Text";
    node["size"] = {240, 80};
  } else if (type == "button") {
    node["text"] = "Button";
    node["size"] = {160, 44};
    node["background_color"] = {0.34, 0.25, 0.55, 1.0};
  } else if (type == "toggle") {
    node["text"] = "Toggle";
    node["size"] = {160, 44};
  } else if (type == "slider") {
    node["size"] = {240, 24};
  } else if (type == "progress") {
    node["size"] = {240, 16};
  } else if (type == "image") {
    node["size"] = {128, 128};
  } else if (type == "text_input") {
    node["size"] = {220, 40};
    node["placeholder"] = "Text";
    node["background_color"] = {0.10, 0.11, 0.14, 0.95};
  } else if (type == "modal") {
    node["size"] = {360, 180};
    node["background_color"] = {0.12, 0.13, 0.17, 0.96};
  } else if (type == "scroll" || type == "list") {
    node["size"] = {320, 240};
  } else {
    node["size"] = {240, 120};
    if (type == "panel")
      node["background_color"] = {0.12, 0.13, 0.17, 0.92};
  }
  return node;
}

const runtime::ui::UiNode *previewNode(const runtime::ui::UiDocument &preview,
                                       const std::string_view id) {
  const auto found =
      std::ranges::find(preview.nodes, id, &runtime::ui::UiNode::id);
  return found == preview.nodes.end() ? nullptr : &*found;
}

std::set<std::string>
previewSubtreeIds(const runtime::ui::UiDocument &preview,
                  const std::string_view rootId) {
  std::set<std::string> ids{std::string(rootId)};
  for (bool changed = true; changed;) {
    changed = false;
    for (const runtime::ui::UiNode &node : preview.nodes)
      if (ids.contains(node.parent) && ids.insert(node.id).second)
        changed = true;
  }
  return ids;
}

Json vec2Json(const runtime::Vec2 value) {
  return Json::array({value.x, value.y});
}

std::optional<Json> starterValue(const std::string_view type) {
  if (type == "string")
    return Json("");
  if (type == "number")
    return Json(0.0);
  if (type == "integer")
    return Json(0);
  if (type == "boolean")
    return Json(false);
  if (type == "array")
    return Json::array();
  if (type == "object")
    return Json::object();
  return std::nullopt;
}

std::optional<Json> prefabArguments(const std::filesystem::path &hudPath,
                                    const std::string_view prefabReference,
                                    std::string &error) {
  const auto prefabPath =
      runtime::ui::resolveUiPrefabReference(hudPath, prefabReference);
  if (!prefabPath) {
    error =
        "The UI prefab reference is invalid: " + std::string(prefabReference);
    return std::nullopt;
  }
  const std::optional<Json> prefab =
      runtime::scene_loading::readJsonFile(*prefabPath, error);
  if (!prefab)
    return std::nullopt;

  const Json parameters = prefab->value("parameters", Json::object());
  if (!parameters.is_object()) {
    error = "UI prefab parameters must be an object.";
    return std::nullopt;
  }
  Json arguments = Json::object();
  for (const auto &[name, specification] : parameters.items()) {
    if (!specification.is_object() || !specification.contains("type") ||
        !specification["type"].is_string()) {
      error = "UI prefab parameter requires a supported type: " + name;
      return std::nullopt;
    }
    if (specification.contains("default")) {
      arguments[name] = specification["default"];
      continue;
    }
    const std::string type = specification["type"].get<std::string>();
    const std::optional<Json> value = starterValue(type);
    if (!value) {
      error = "UI prefab parameter has an unsupported type: " + name;
      return std::nullopt;
    }
    arguments[name] = *value;
  }
  return arguments;
}

std::string prefabInstanceBase(const std::string_view reference) {
  const std::size_t separator = reference.find_last_of('/');
  std::string base(reference.substr(
      separator == std::string_view::npos ? 0 : separator + 1));
  return base.empty() ? "ui_prefab" : base;
}

} // namespace

bool EditorHudDocument::open(std::filesystem::path path, std::string &error) {
  if (!document_.open(
          std::move(path),
          [](const std::filesystem::path &source, const Json &document) {
            return validateSpecializedDocument(EditorSpecializedKind::Hud,
                                               source, document);
          },
          error))
    return false;
  return rebuild(error);
}

bool EditorHudDocument::createNode(const std::string_view type,
                                   const std::string_view parentId,
                                   std::string &createdId, std::string &error) {
  Json replacement = document_.json();
  Json *root =
      replacement.contains("root") ? &replacement["root"] : &replacement;
  Json *parent = root == nullptr ? nullptr
                 : (parentId.empty() ||
                    (!replacement.contains("root") && parentId == "ui_root"))
                     ? root
                     : findNode(*root, parentId);
  if (parent == nullptr) {
    error = "Select an authored HUD container before adding an element.";
    return false;
  }
  if (parent->contains("prefab")) {
    error = "Prefab-expanded HUD nodes must be edited in their UI prefab.";
    return false;
  }
  createdId = uniqueId(*root, std::string(type));
  const auto key = parent == &replacement && parent->contains("elements")
                       ? "elements"
                       : "children";
  if (!parent->contains(key) || !(*parent)[key].is_array())
    (*parent)[key] = Json::array();
  (*parent)[key].push_back(defaultNode(type, createdId));
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::createPrefabInstance(
    const std::string_view prefabReference, const std::string_view parentId,
    std::string &createdId, std::string &error) {
  Json replacement = document_.json();
  Json *root =
      replacement.contains("root") ? &replacement["root"] : &replacement;
  Json *parent = parentId.empty() || (!replacement.contains("root") &&
                                      parentId == "ui_root")
                     ? root
                     : findNode(*root, parentId);
  if (parent == nullptr) {
    error = "Select an authored HUD container before adding a UI prefab.";
    return false;
  }
  if (parent->contains("prefab")) {
    error = "Prefab-expanded HUD nodes must be edited in their UI prefab.";
    return false;
  }

  const std::optional<Json> arguments =
      prefabArguments(document_.path(), prefabReference, error);
  if (!arguments)
    return false;
  createdId = uniqueId(*root, prefabInstanceBase(prefabReference));
  Json instance{{"id", createdId}, {"prefab", prefabReference}};
  if (!arguments->empty())
    instance["arguments"] = *arguments;
  const auto key = parent == &replacement && parent->contains("elements")
                       ? "elements"
                       : "children";
  if (!parent->contains(key) || !(*parent)[key].is_array())
    (*parent)[key] = Json::array();
  (*parent)[key].push_back(std::move(instance));
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::reparentNode(const std::string_view id,
                                     const std::string_view parentId,
                                     std::string &error) {
  Json replacement = document_.json();
  const std::string rootId = authoredRootId(replacement);
  if (id.empty() || id == rootId) {
    error = "The HUD root cannot be moved.";
    return false;
  }
  const std::optional<NodeLocation> source =
      findNodeLocation(*authoredRoot(replacement), id);
  if (!source) {
    error = "This node is generated by a UI prefab or no longer exists.";
    return false;
  }
  Json *parent = findAuthoredHudNode(replacement, parentId);
  if (parent == nullptr) {
    error = "The destination is generated by a UI prefab or no longer exists.";
    return false;
  }
  if (parent->contains("prefab")) {
    error = "UI prefab instances cannot own authored children. Open the UI "
            "prefab source to change its hierarchy.";
    return false;
  }

  const std::set<std::string> subtreeIds = previewSubtreeIds(preview_, id);
  if (subtreeIds.contains(std::string(parentId))) {
    error = "A HUD node cannot be parented to itself or its descendants.";
    return false;
  }
  const std::string nestedParentId =
      source->owner == authoredRoot(replacement)
          ? rootId
          : source->owner->value("id", "");
  const std::string currentParentId =
      (*source->siblings)[source->index].value("parent", nestedParentId);
  if (currentParentId == parentId)
    return true;

  Json moved = std::move((*source->siblings)[source->index]);
  moved.erase("parent");
  source->siblings->erase(source->siblings->begin() +
                          static_cast<Json::difference_type>(source->index));
  parent = findAuthoredHudNode(replacement, parentId);
  if (parent == nullptr) {
    error = "The destination is no longer available.";
    return false;
  }
  authoredChildren(*parent).push_back(std::move(moved));
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::duplicateNode(const std::string_view id,
                                      std::string &createdId,
                                      std::string &error) {
  Json replacement = document_.json();
  if (id.empty() || id == authoredRootId(replacement)) {
    error = "The HUD root cannot be duplicated.";
    return false;
  }
  const std::optional<NodeLocation> source =
      findNodeLocation(*authoredRoot(replacement), id);
  if (!source) {
    error = "This node is generated by a UI prefab or no longer exists.";
    return false;
  }

  std::set<std::string> reservedIds;
  collectIds(*authoredRoot(replacement), reservedIds);
  for (const runtime::ui::UiNode &node : preview_.nodes)
    reservedIds.insert(node.id);
  std::vector<std::string> subtreeIds;
  collectSubtreeIds((*source->siblings)[source->index], subtreeIds);
  std::unordered_map<std::string, std::string> idRemapping;
  for (const std::string &sourceId : subtreeIds) {
    std::string duplicateId = uniqueId(reservedIds, sourceId + "_copy");
    reservedIds.insert(duplicateId);
    idRemapping.emplace(sourceId, std::move(duplicateId));
  }
  const auto rootReplacement = idRemapping.find(std::string(id));
  if (rootReplacement == idRemapping.end()) {
    error = "The authored HUD subtree has no stable root ID.";
    return false;
  }

  Json copy = (*source->siblings)[source->index];
  remapCopiedSubtree(copy, idRemapping);
  const std::string duplicateRootId = rootReplacement->second;
  source->siblings->insert(
      source->siblings->begin() +
          static_cast<Json::difference_type>(source->index + 1),
      std::move(copy));
  if (!replaceAndRebuild(std::move(replacement), error))
    return false;
  createdId = duplicateRootId;
  return true;
}

bool EditorHudDocument::deleteNode(const std::string_view id,
                                   std::string &error) {
  Json replacement = document_.json();
  Json *root =
      replacement.contains("root") ? &replacement["root"] : &replacement;
  if (root->value("id", "ui_root") == id) {
    error = "The HUD root cannot be deleted.";
    return false;
  }
  if (!eraseNode(*root, id)) {
    error = "This node is generated by a UI prefab or no longer exists.";
    return false;
  }
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::setCanvasSize(const runtime::Vec2 size,
                                      std::string &error) {
  if (!isHudFile(document_.path())) {
    error = "Canvas size belongs to HUD documents, not UI prefabs.";
    return false;
  }
  if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0.0F ||
      size.y <= 0.0F) {
    error = "HUD canvas width and height must be positive finite values.";
    return false;
  }
  Json replacement = document_.json();
  const auto previous = replacement.find("canvas_size");
  Json normalized = normalizeEditorAuthoredValue(
      Json::array({size.x, size.y}),
      previous == replacement.end() ? nullptr : &*previous, 1);
  replacement["canvas_size"] = std::move(normalized);
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::setNodeField(const std::string_view id,
                                     const std::string_view field, Json value,
                                     std::string &error) {
  Json replacement = document_.json();
  if (!replacement.contains("root") && id == "ui_root") {
    error = "The implicit HUD root always fills the canvas. Select a child "
            "element to edit its properties.";
    return false;
  }
  Json *root = replacement.contains("root") ? &replacement["root"] : nullptr;
  if (!root)
    root = &replacement;
  Json *node = root == nullptr ? nullptr : findNode(*root, id);
  if (node == nullptr) {
    error = "This node is generated by a UI prefab or no longer exists.";
    return false;
  }
  if (node->contains("prefab") && field != "arguments") {
    error = "UI prefab instances are read-only here. Open the UI prefab source "
            "to edit its nodes.";
    return false;
  }

  const runtime::ui::UiNode *parsed = previewNode(preview_, id);
  if (field == "dock" && value.is_null() && node->contains("dock") && parsed) {
    node->erase("dock");
    (*node)["anchor_min"] = normalizeEditorAuthoredValue(
        vec2Json(parsed->layout.anchorMin), nullptr, 2);
    (*node)["anchor_max"] = normalizeEditorAuthoredValue(
        vec2Json(parsed->layout.anchorMax), nullptr, 2);
  } else {
    if (field == "dock" && !value.is_null()) {
      node->erase("anchor_min");
      node->erase("anchor_max");
    } else if ((field == "anchor_min" || field == "anchor_max") &&
               !value.is_null() && node->contains("dock") && parsed) {
      const char *other = field == "anchor_min" ? "anchor_max" : "anchor_min";
      const runtime::Vec2 otherValue = field == "anchor_min"
                                           ? parsed->layout.anchorMax
                                           : parsed->layout.anchorMin;
      (*node)[other] =
          normalizeEditorAuthoredValue(vec2Json(otherValue), nullptr, 2);
      node->erase("dock");
    }
    if (field == "pad" && !value.is_null())
      node->erase("padding");
    else if (field == "padding" && !value.is_null())
      node->erase("pad");
    if (field == "stack")
      node->erase("layout");
    else if (field == "layout")
      node->erase("stack");
    if (field == "position" && !value.is_null())
      node->erase("at");
    else if (field == "at" && !value.is_null())
      node->erase("position");

    // Null resets an optional property to the parser-owned default.
    if (value.is_null())
      node->erase(std::string(field));
    else {
      const auto previous = node->find(field);
      value = normalizeEditorAuthoredValue(
          std::move(value), previous == node->end() ? nullptr : &*previous,
          authoredDecimalPlaces(field));
      (*node)[std::string(field)] = std::move(value);
    }
  }
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::setNodeAnchors(const std::string_view id,
                                       const runtime::Vec2 anchorMin,
                                       const runtime::Vec2 anchorMax,
                                       std::string &error) {
  Json replacement = document_.json();
  if (!replacement.contains("root") && id == "ui_root") {
    error = "The implicit HUD root always fills the canvas. Select a child "
            "element to edit its properties.";
    return false;
  }
  Json *root =
      replacement.contains("root") ? &replacement["root"] : &replacement;
  Json *node = findNode(*root, id);
  if (node == nullptr || node->contains("prefab")) {
    error = "This node is generated by a UI prefab or no longer exists.";
    return false;
  }
  node->erase("dock");
  (*node)["anchor_min"] =
      normalizeEditorAuthoredValue(vec2Json(anchorMin), nullptr, 2);
  (*node)["anchor_max"] =
      normalizeEditorAuthoredValue(vec2Json(anchorMax), nullptr, 2);
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::undo(std::string &error) {
  return document_.undo(error) && rebuild(error);
}

bool EditorHudDocument::redo(std::string &error) {
  return document_.redo(error) && rebuild(error);
}

bool EditorHudDocument::restore(nlohmann::json document, std::string &error) {
  return replaceAndRebuild(std::move(document), error);
}

bool EditorHudDocument::hasImplicitRoot() const {
  return !document_.json().contains("root") &&
         (document_.json().contains("children") ||
          document_.json().contains("elements"));
}

const Json *EditorHudDocument::authoredNode(const std::string_view id) const {
  const auto root = document_.json().find("root");
  if (root != document_.json().end())
    return findNode(*root, id);
  return id == "ui_root" ? &document_.json() : findNode(document_.json(), id);
}

bool EditorHudDocument::rebuild(std::string &error) {
  std::optional<runtime::ui::UiDocument> parsed =
      runtime::scene_loading::parseHudDocument(document_.path(),
                                               document_.json(), error);
  if (!parsed)
    return false;
  preview_ = std::move(*parsed);
  return true;
}

bool EditorHudDocument::replaceAndRebuild(Json replacement,
                                          std::string &error) {
  std::optional<runtime::ui::UiDocument> parsed =
      runtime::scene_loading::parseHudDocument(document_.path(), replacement,
                                               error);
  if (!parsed || !document_.replace(std::move(replacement), error))
    return false;
  preview_ = std::move(*parsed);
  return true;
}

} // namespace demi::editor
