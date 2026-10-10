#include "editor/EditorHudDocument.h"
#include "editor/EditorAuthoringClipboard.h"
#include "editor/EditorHudFlowPlacement.h"

#include "demi/filesystem/AuthoredJsonPatch.h"
#include "editor/EditorSpecializedDocument.h"

#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/scene/HudParser.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/ui/UiPrefabResolver.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace demi::editor {
// Authored-source patching is shared infrastructure, not editor state.
using demi::filesystem::normalizeAuthoredValue;
using demi::filesystem::patchAuthoredJsonSource;

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

std::optional<Json> copiedHudNodes(const Json &payload,
                                  const runtime::ui::UiDocument &preview,
                                  const Json &document, std::string &error) {
  std::vector<std::string> ids;
  if (!collectClipboardTreeIds(payload, ids, error))
    return std::nullopt;
  std::set<std::string> reserved;
  collectIds(document, reserved);
  for (const auto &node : preview.nodes)
    reserved.insert(node.id);
  auto copies = payload;
  remapClipboardHudNodes(copies, allocateClipboardIds(ids, std::move(reserved)));
  return copies;
}

Json *pasteHudParent(Json &document, const runtime::ui::UiDocument &preview,
                     std::string_view selectedId) {
  const auto container = [](const std::string &type) {
    return type == "container" || type == "panel" || type == "scroll" ||
           type == "list" || type == "modal";
  };
  std::set<std::string> visited;
  while (!selectedId.empty() && visited.insert(std::string(selectedId)).second) {
    const auto node = std::ranges::find(preview.nodes, selectedId,
                                      &runtime::ui::UiNode::id);
    if (node == preview.nodes.end())
      break;
    Json *authored = findAuthoredHudNode(document, selectedId);
    if (authored && !authored->contains("prefab") && container(node->type))
      return authored;
    selectedId = node->parent;
  }
  return authoredRoot(document);
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

std::optional<Json>
prefabArgumentDefaults(const std::filesystem::path &hudPath,
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
  previewVisibility_.clear();
  return rebuild(error);
}

bool EditorHudDocument::createNode(const std::string_view type,
                                   const std::string_view parentId,
                                   std::string &createdId, std::string &error,
                                   const std::optional<runtime::Vec2> position) {
  if (position && (!std::isfinite(position->x) || !std::isfinite(position->y))) {
    error = "HUD placement must have finite coordinates.";
    return false;
  }
  Json replacement = document_.json();
  std::string prefix;
  Json *parent = childStorage(replacement, parentId, prefix);
  if (!parent) {
    error = "Select a HUD container before adding content.";
    return false;
  }
  const auto localId = newChildId(std::string(type), prefix);
  createdId = prefix + localId;
  const auto key = parent == &replacement && parent->contains("elements")
                       ? "elements"
                       : "children";
  if (!parent->contains(key) || !(*parent)[key].is_array())
    (*parent)[key] = Json::array();
  const auto placement = editorHudFlowPlacement(
      preview_, parentId.empty() ? authoredRootId(replacement) : parentId,
      (*parent)[key], position);
  Json node = defaultNode(type, localId);
  if (placement.flow)
    node.erase("position");
  else if (position)
    node["position"] = normalizeAuthoredValue(
        Json::array({position->x, position->y}), nullptr, 1);
  (*parent)[key].insert((*parent)[key].begin() +
                            static_cast<Json::difference_type>(std::min(
                                placement.index, (*parent)[key].size())),
                        std::move(node));
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::createPrefabInstance(
    const std::string_view prefabReference, const std::string_view parentId,
    std::string &createdId, std::string &error,
    const std::optional<runtime::Vec2> position) {
  if (position && (!std::isfinite(position->x) || !std::isfinite(position->y))) {
    error = "HUD placement must have finite coordinates.";
    return false;
  }
  Json replacement = document_.json();
  std::string prefix;
  Json *parent = childStorage(replacement, parentId, prefix);
  if (!parent) {
    error = "Select a HUD container before adding content.";
    return false;
  }
  const std::optional<Json> arguments =
      prefabArgumentDefaults(document_.path(), prefabReference, error);
  if (!arguments)
    return false;
  const auto localId = newChildId(prefabInstanceBase(prefabReference), prefix);
  createdId = prefix + localId;
  Json instance{{"id", localId}, {"prefab", prefabReference}};
  if (!arguments->empty())
    instance["arguments"] = *arguments;
  const auto key = parent == &replacement && parent->contains("elements")
                       ? "elements"
                       : "children";
  if (!parent->contains(key) || !(*parent)[key].is_array())
    (*parent)[key] = Json::array();
  const auto placement = editorHudFlowPlacement(
      preview_, parentId.empty() ? authoredRootId(replacement) : parentId,
      (*parent)[key], position);
  if (placement.flow) {
    // The container owns the root's slot, even if the source prefab uses an
    // offset or stretch anchors for standalone placement. Its contents remain
    // inherited.
    instance["overrides"]["$root"] = {
        {"position", {0, 0}}, {"anchor_min", {0, 0}}, {"anchor_max", {0, 0}}};
  } else if (position) {
    instance["overrides"]["$root"] = {
        {"position", normalizeAuthoredValue(
                         Json::array({position->x, position->y}), nullptr, 1)}};
  }
  (*parent)[key].insert((*parent)[key].begin() +
                            static_cast<Json::difference_type>(std::min(
                                placement.index, (*parent)[key].size())),
                        std::move(instance));
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::duplicateNode(const std::string_view id,
                                      std::string &createdId,
                                      std::string &error) {
  const std::vector ids{std::string(id)};
  std::vector<std::string> created;
  if (!duplicateNodes(ids, created, error))
    return false;
  createdId = created.front();
  return true;
}

bool EditorHudDocument::deleteNode(const std::string_view id,
                                   std::string &error) {
  const std::vector ids{std::string(id)};
  return deleteNodes(ids, error);
}

std::optional<Json> EditorHudDocument::exportNodes(
    const std::span<const std::string> ids, std::string &error) const {
  if (ids.empty()) {
    error = "Select authored HUD elements to copy.";
    return std::nullopt;
  }
  std::set<std::string> selected(ids.begin(), ids.end());
  std::set<std::string> descendants;
  for (const auto &id : selected) {
    const auto *source = authoredNode(id);
    if (!source || id == authoredRootId(document_.json())) {
      error = "Select authored non-root controls; generated prefab children cannot be copied.";
      return std::nullopt;
    }
    std::vector<std::string> members;
    collectSubtreeIds(*source, members);
    for (const auto &member : members)
      if (member != id)
        descendants.insert(member);
  }
  Json result = Json::array();
  // Preview order is authored pre-order and gives stable sibling ordering.
  for (const auto &node : preview_.nodes)
    if (selected.contains(node.id) && !descendants.contains(node.id)) {
      Json copy = *authoredNode(node.id);
      if (!node.parent.empty())
        copy["parent"] = node.parent;
      result.push_back(std::move(copy));
    }
  return result;
}

bool EditorHudDocument::pasteNodes(const Json &payload,
                                  const std::string_view parentId,
                                  std::vector<std::string> &createdIds,
                                  std::string &error) {
  try {
    auto copies = copiedHudNodes(payload, preview_, document_.json(), error);
    if (!copies)
      return false;
    Json replacement = document_.json();
    Json *parent = pasteHudParent(replacement, preview_, parentId);
    std::vector<std::string> created;
    for (auto &copy : *copies) {
      created.push_back(copy.at("id").get<std::string>());
      copy.erase("parent");
      authoredChildren(*parent).push_back(std::move(copy));
    }
    if (!replaceAndRebuild(std::move(replacement), error))
      return false;
    createdIds = std::move(created);
    return true;
  } catch (const std::exception &exception) {
    error = "Could not paste HUD elements: " + std::string(exception.what());
    return false;
  }
}

bool EditorHudDocument::duplicateNodes(const std::span<const std::string> ids,
                                      std::vector<std::string> &createdIds,
                                      std::string &error) {
  const auto payload = exportNodes(ids, error);
  if (!payload)
    return false;
  try {
    auto copies = copiedHudNodes(*payload, preview_, document_.json(), error);
    if (!copies)
      return false;
    Json replacement = document_.json();
    std::vector<std::string> created;
    for (std::size_t index = 0; index < copies->size(); ++index) {
      const auto sourceId = (*payload)[index].at("id").get<std::string>();
      const auto location = findNodeLocation(*authoredRoot(replacement), sourceId);
      if (!location) {
        error = "The authored HUD source no longer exists.";
        return false;
      }
      auto copy = std::move((*copies)[index]);
      created.push_back(copy.at("id").get<std::string>());
      copy.erase("parent");
      location->siblings->insert(location->siblings->begin() +
                                    static_cast<Json::difference_type>(location->index + 1),
                                std::move(copy));
    }
    if (!replaceAndRebuild(std::move(replacement), error))
      return false;
    createdIds = std::move(created);
    return true;
  } catch (const std::exception &exception) {
    error = "Could not duplicate HUD elements: " + std::string(exception.what());
    return false;
  }
}

bool EditorHudDocument::deleteNodes(const std::span<const std::string> ids,
                                   std::string &error) {
  Json replacement = document_.json();
  std::set<std::string> selected(ids.begin(), ids.end());
  std::map<std::string, std::vector<std::size_t>> removals;
  for (const auto &id : selected) {
    const auto *node = previewNode(preview_, id);
    if (!node || node->parent.empty()) {
      error = "The HUD root cannot be deleted.";
      return false;
    }
    bool covered = false;
    for (auto parent = node->parent; !parent.empty();) {
      if (selected.contains(parent)) {
        covered = true;
        break;
      }
      const auto *ancestor = previewNode(preview_, parent);
      parent = ancestor ? ancestor->parent : std::string{};
    }
    if (covered)
      continue;
    const auto source = composition_.authoredNodes.find(id);
    if (source != composition_.authoredNodes.end()) {
      discardOverridesForSource(replacement, source->second.pointer, true);
      auto pointer = Json::json_pointer(source->second.pointer);
      const auto index = std::stoull(pointer.back());
      pointer.pop_back();
      removals[pointer.to_string()].push_back(index);
    } else if (const auto *origin = prefabOrigin(id)) {
      replacement.at(Json::json_pointer(
          origin->instancePointer))["overrides"][origin->localNodeId] = nullptr;
    } else {
      error = "The selected HUD node has no editable owner.";
      return false;
    }
  }
  std::vector<std::pair<std::string, std::vector<std::size_t>>> ordered(
      removals.begin(), removals.end());
  std::ranges::sort(ordered, [](const auto &a, const auto &b) {
    return std::ranges::count(a.first, '/') > std::ranges::count(b.first, '/');
  });
  for (auto &[pointer, indices] : ordered) {
    auto &array = replacement.at(Json::json_pointer(pointer));
    std::ranges::sort(indices, std::greater{});
    for (auto index : indices)
      array.erase(array.begin() + static_cast<Json::difference_type>(index));
  }
  const auto prune = [&](auto &&self, Json &value) -> void {
    if (value.is_object()) {
      if (value.contains("prefab") && value["prefab"].is_string() &&
          value["prefab"].get<std::string>().starts_with("ui-prefab://")) {
        if (auto patches = value.find("overrides"); patches != value.end()) {
          for (auto it = patches->begin(); it != patches->end();) {
            if (it->is_object() && it->contains("children") &&
                (*it)["children"].empty())
              it->erase("children");
            if (it->is_object() && it->empty())
              it = patches->erase(it);
            else
              ++it;
          }
          if (patches->empty())
            value.erase(patches);
        }
      }
      for (auto &child : value)
        self(self, child);
    } else if (value.is_array())
      for (auto &child : value)
        self(self, child);
  };
  prune(prune, replacement);
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
  Json normalized = normalizeAuthoredValue(
      Json::array({size.x, size.y}),
      previous == replacement.end() ? nullptr : &*previous, 1);
  replacement["canvas_size"] = std::move(normalized);
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::setNodeField(const std::string_view id,
                                     const std::string_view field, Json value,
                                     std::string &error, bool continuous) {
  Json replacement = document_.json();
  if (!replacement.contains("root") && id == "ui_root") {
    error = "The implicit HUD root always fills the canvas. Select a child "
            "element to edit its properties.";
    return false;
  }
  Json *node = nullptr;
  if (field == "arguments") {
    node = mutableAuthoredNode(replacement, id);
    if (!node || !node->contains("prefab")) {
      error = "Select an authored UI prefab instance to edit its parameters.";
      return false;
    }
  } else {
    node = mutableNodeProperties(replacement, id);
  }
  if (!node) {
    error = "This HUD node no longer has an editable source or instance.";
    return false;
  }
  const Json *effective = effectiveNode(id);
  const bool hadDock = effective && effective->contains("dock");
  const runtime::ui::UiNode *parsed = previewNode(preview_, id);
  const auto preserveDockPosition = [&] {
    if (!parsed)
      return;
    const auto &layout = parsed->layout;
    if (layout.dockPivot.x == 0 && layout.dockPivot.y == 0)
      return;
    const runtime::Vec2 position{
        layout.position.x -
            layout.dockPivot.x * (parsed->resolved.width + layout.margin.left +
                                  layout.margin.right),
        layout.position.y -
            layout.dockPivot.y * (parsed->resolved.height + layout.margin.top +
                                  layout.margin.bottom)};
    node->erase("at");
    (*node)["position"] =
        normalizeAuthoredValue(vec2Json(position), nullptr, 1);
  };
  if (field == "dock" && value.is_null() && hadDock && parsed) {
    preserveDockPosition();
    node->erase("dock");
    (*node)["anchor_min"] = normalizeAuthoredValue(
        vec2Json(parsed->layout.anchorMin), nullptr, 2);
    (*node)["anchor_max"] = normalizeAuthoredValue(
        vec2Json(parsed->layout.anchorMax), nullptr, 2);
  } else {
    if (field == "dock" && !value.is_null()) {
      if (!value.is_string() ||
          (value != "fill" && value != "top" && value != "bottom" &&
           value != "left" && value != "right" && value != "center")) {
        error = "Choose Fill, Top, Bottom, Left, Right or Center for a dock "
                "preset.";
        return false;
      }
      node->erase("anchor_min");
      node->erase("anchor_max");
      node->erase("position");
      node->erase("at");
    } else if ((field == "anchor_min" || field == "anchor_max") &&
               !value.is_null() && hadDock && parsed) {
      preserveDockPosition();
      const char *other = field == "anchor_min" ? "anchor_max" : "anchor_min";
      const runtime::Vec2 otherValue = field == "anchor_min"
                                           ? parsed->layout.anchorMax
                                           : parsed->layout.anchorMin;
      (*node)[other] =
          normalizeAuthoredValue(vec2Json(otherValue), nullptr, 2);
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
      value = normalizeAuthoredValue(
          std::move(value), previous == node->end() ? nullptr : &*previous,
          authoredDecimalPlaces(field));
      (*node)[std::string(field)] = std::move(value);
    }
  }
  pruneOverrides(replacement, id);
  return replaceAndRebuild(std::move(replacement), error,
                           continuous ? Json::array({id, field}).dump()
                                      : std::string{});
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
  Json *node = mutableNodeProperties(replacement, id);
  if (!node) {
    error = "This HUD node no longer has an editable source or instance.";
    return false;
  }
  node->erase("dock");
  (*node)["anchor_min"] =
      normalizeAuthoredValue(vec2Json(anchorMin), nullptr, 2);
  (*node)["anchor_max"] =
      normalizeAuthoredValue(vec2Json(anchorMax), nullptr, 2);
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

const Json *EditorHudDocument::effectiveNode(std::string_view id) const {
  if (!composition_.document)
    return nullptr;
  return findNode(composition_.document->at("root"), id);
}

bool EditorHudDocument::rebuild(std::string &error) {
  std::optional<runtime::ui::UiDocument> parsed =
      runtime::scene_loading::parseHudDocument(
          document_.path(), document_.json(), error, &composition_);
  if (!parsed)
    return false;
  preview_ = std::move(*parsed);
  applyPreviewState();
  return true;
}

bool EditorHudDocument::replaceAndRebuild(Json replacement, std::string &error,
                                          std::string_view continuousKey) {
  runtime::ui::UiPrefabExpansionResult composition;
  std::optional<runtime::ui::UiDocument> parsed =
      runtime::scene_loading::parseHudDocument(document_.path(), replacement,
                                               error, &composition);
  if (!parsed ||
      !document_.replace(std::move(replacement), error, continuousKey))
    return false;
  preview_ = std::move(*parsed);
  composition_ = std::move(composition);
  applyPreviewState();
  return true;
}

} // namespace demi::editor
