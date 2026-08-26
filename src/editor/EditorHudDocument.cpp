#include "editor/EditorHudDocument.h"

#include "editor/EditorSpecializedDocument.h"

#include "demi/runtime/scene/HudParser.h"

#include <algorithm>
#include <set>

namespace demi::editor {
namespace {

using Json = nlohmann::json;

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
  return nullptr;
}

bool eraseNode(Json &node, const std::string_view id) {
  auto children = node.find("children");
  if (children == node.end() || !children->is_array())
    return false;
  for (auto child = children->begin(); child != children->end(); ++child) {
    if (child->is_object() && child->value("id", "") == id) {
      children->erase(child);
      return true;
    }
    if (eraseNode(*child, id))
      return true;
  }
  return false;
}

void collectIds(const Json &node, std::set<std::string> &ids) {
  if (!node.is_object())
    return;
  if (const std::string id = node.value("id", ""); !id.empty())
    ids.insert(id);
  if (auto children = node.find("children");
      children != node.end() && children->is_array())
    for (const Json &child : *children)
      collectIds(child, ids);
}

std::string uniqueId(const Json &root, std::string base) {
  std::set<std::string> ids;
  collectIds(root, ids);
  std::replace(base.begin(), base.end(), ' ', '_');
  if (!ids.contains(base))
    return base;
  for (int suffix = 2;; ++suffix) {
    std::string candidate = base + '_' + std::to_string(suffix);
    if (!ids.contains(candidate))
      return candidate;
  }
}

Json defaultNode(const std::string_view type, const std::string &id) {
  Json node{{"id", id}, {"type", type}, {"position", {24, 24}}};
  if (type == "label") {
    node["text"] = "Label";
    node["size"] = {160, 32};
    node["font_size"] = 20;
  } else if (type == "button") {
    node["text"] = "Button";
    node["size"] = {160, 44};
    node["background_color"] = {0.34, 0.25, 0.55, 1.0};
    node["focusable"] = true;
  } else if (type == "image") {
    node["size"] = {128, 128};
    node["texture"] = "";
  } else if (type == "text_input") {
    node["size"] = {220, 40};
    node["placeholder"] = "Text";
    node["background_color"] = {0.10, 0.11, 0.14, 0.95};
    node["focusable"] = true;
  } else {
    node["size"] = {240, 120};
    if (type == "panel")
      node["background_color"] = {0.12, 0.13, 0.17, 0.92};
  }
  return node;
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
  Json *root = replacement.contains("root") ? &replacement["root"] : nullptr;
  Json *parent = root == nullptr
                     ? nullptr
                     : findNode(*root, parentId.empty() ? root->value("id", "")
                                                        : parentId);
  if (parent == nullptr) {
    error = "Select an authored HUD container before adding an element.";
    return false;
  }
  if (parent->contains("prefab")) {
    error = "Prefab-expanded HUD nodes must be edited in their UI prefab.";
    return false;
  }
  createdId = uniqueId(*root, std::string(type));
  if (!parent->contains("children") || !(*parent)["children"].is_array())
    (*parent)["children"] = Json::array();
  (*parent)["children"].push_back(defaultNode(type, createdId));
  if (!document_.replace(std::move(replacement), error))
    return false;
  return rebuild(error);
}

bool EditorHudDocument::deleteNode(const std::string_view id,
                                   std::string &error) {
  Json replacement = document_.json();
  Json *root = replacement.contains("root") ? &replacement["root"] : nullptr;
  if (root == nullptr || root->value("id", "") == id) {
    error = "The HUD root cannot be deleted.";
    return false;
  }
  if (!eraseNode(*root, id)) {
    error = "This node is generated by a UI prefab or no longer exists.";
    return false;
  }
  if (!document_.replace(std::move(replacement), error))
    return false;
  return rebuild(error);
}

bool EditorHudDocument::setNodeField(const std::string_view id,
                                     const std::string_view field, Json value,
                                     std::string &error) {
  Json replacement = document_.json();
  Json *root = replacement.contains("root") ? &replacement["root"] : nullptr;
  Json *node = root == nullptr ? nullptr : findNode(*root, id);
  if (node == nullptr) {
    error = "This node is generated by a UI prefab or no longer exists.";
    return false;
  }
  (*node)[std::string(field)] = std::move(value);
  if (!document_.replace(std::move(replacement), error))
    return false;
  return rebuild(error);
}

bool EditorHudDocument::undo(std::string &error) {
  return document_.undo(error) && rebuild(error);
}

bool EditorHudDocument::redo(std::string &error) {
  return document_.redo(error) && rebuild(error);
}

bool EditorHudDocument::restore(nlohmann::json document, std::string &error) {
  if (!document_.replace(std::move(document), error))
    return false;
  return rebuild(error);
}

const Json *EditorHudDocument::authoredNode(const std::string_view id) const {
  const auto root = document_.json().find("root");
  return root == document_.json().end() ? nullptr : findNode(*root, id);
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

} // namespace demi::editor
