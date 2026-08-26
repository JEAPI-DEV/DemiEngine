#include "editor/EditorProjectDocument.h"

#include "demi/runtime/input/InputActionParser.h"
#include "demi/runtime/scene/ProjectParser.h"

#include <algorithm>
#include <set>

namespace demi::editor {
namespace {

bool validAssetUri(const std::string_view value) {
  return (value.starts_with("asset://") && value.size() > 8) ||
         (value.starts_with("asset-group://") && value.size() > 14);
}

} // namespace

bool EditorProjectDocument::open(const std::filesystem::path &path,
                                 std::string &error) {
  std::string text;
  FileRevision revision;
  if (!store_.read(path, text, revision, error))
    return false;
  try {
    nlohmann::json document = nlohmann::json::parse(text);
    path_ = path;
    if (!validate(document, error))
      return false;
    revision_ = revision;
    document_ = std::move(document);
    savedCanonical_ = document_.dump();
    undo_.clear();
    redo_.clear();
    hasExternalConflict_ = false;
    return true;
  } catch (const nlohmann::json::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool EditorProjectDocument::reload(std::string &error) {
  return open(path_, error);
}

bool EditorProjectDocument::save(std::string &error) {
  const std::string text = document_.dump(2) + '\n';
  FileRevision replacement;
  const DocumentWriteStatus status =
      store_.writeIfUnchanged(path_, text, revision_, replacement, error);
  if (status == DocumentWriteStatus::Conflict) {
    hasExternalConflict_ = true;
    return false;
  }
  if (status != DocumentWriteStatus::Written)
    return false;
  revision_ = replacement;
  savedCanonical_ = document_.dump();
  hasExternalConflict_ = false;
  return true;
}

bool EditorProjectDocument::restore(nlohmann::json document,
                                    std::string &error) {
  if (!validate(document, error))
    return false;
  document_ = std::move(document);
  undo_.clear();
  redo_.clear();
  return true;
}

bool EditorProjectDocument::setPreloadedAssets(std::vector<std::string> assets,
                                               std::string &error) {
  std::ranges::sort(assets);
  if (std::ranges::any_of(assets, [](const std::string &asset) {
        return !validAssetUri(asset);
      })) {
    error = "Preloaded assets require asset:// or asset-group:// IDs.";
    return false;
  }
  if (std::ranges::adjacent_find(assets) != assets.end()) {
    error = "A preloaded asset ID may only appear once.";
    return false;
  }
  nlohmann::json replacement = document_;
  replacement["assets"] = std::move(assets);
  return commit(std::move(replacement), error);
}

bool EditorProjectDocument::addScene(std::string id, std::filesystem::path path,
                                     std::string &error) {
  if (!id.starts_with("scene://") || id.size() <= 8 || path.empty() ||
      path.is_absolute() || std::ranges::any_of(path, [](const auto &part) {
        return part == "..";
      })) {
    error = "Scenes require a non-empty scene:// ID and safe relative path.";
    return false;
  }
  nlohmann::json replacement = document_;
  auto &scenes = replacement["scenes"];
  if (!scenes.is_array()) {
    error = "Project scenes must be an array.";
    return false;
  }
  if (std::ranges::any_of(scenes, [&](const nlohmann::json &scene) {
        return scene.value("id", "") == id ||
               scene.value("path", "") == path.generic_string();
      })) {
    error = "The scene ID or path is already registered.";
    return false;
  }
  scenes.push_back({{"id", std::move(id)}, {"path", path.generic_string()}});
  return commit(std::move(replacement), error);
}

bool EditorProjectDocument::setInputActions(nlohmann::json actions,
                                            std::string &error) {
  if (!actions.is_object()) {
    error = "Input actions must be an object keyed by action name.";
    return false;
  }
  nlohmann::json replacement = document_;
  replacement["input"]["actions"] = std::move(actions);
  return commit(std::move(replacement), error);
}

bool EditorProjectDocument::removeScene(const std::string_view id,
                                        std::string &error) {
  if (document_.value("main_scene", "") == id) {
    error = "The main scene cannot be removed.";
    return false;
  }
  nlohmann::json replacement = document_;
  auto &scenes = replacement["scenes"];
  const auto found =
      std::ranges::find_if(scenes, [&](const nlohmann::json &item) {
        return item.value("id", "") == id;
      });
  if (found == scenes.end()) {
    error = "The scene is not registered by this project.";
    return false;
  }
  scenes.erase(found);
  return commit(std::move(replacement), error);
}

bool EditorProjectDocument::undo(std::string &error) {
  if (undo_.empty()) {
    error = "There is no project edit to undo.";
    return false;
  }
  Change change = std::move(undo_.back());
  undo_.pop_back();
  document_ = change.before;
  redo_.push_back(std::move(change));
  return true;
}

bool EditorProjectDocument::redo(std::string &error) {
  if (redo_.empty()) {
    error = "There is no project edit to redo.";
    return false;
  }
  Change change = std::move(redo_.back());
  redo_.pop_back();
  document_ = change.after;
  undo_.push_back(std::move(change));
  return true;
}

bool EditorProjectDocument::isDirty() const {
  return document_.dump() != savedCanonical_;
}

std::vector<std::string> EditorProjectDocument::preloadedAssets() const {
  if (const auto found = document_.find("assets");
      found != document_.end() && found->is_array())
    return found->get<std::vector<std::string>>();
  return {};
}

std::vector<runtime::SceneEntry> EditorProjectDocument::scenes() const {
  std::vector<runtime::SceneEntry> result;
  if (const auto found = document_.find("scenes");
      found != document_.end() && found->is_array())
    for (const nlohmann::json &scene : *found)
      if (scene.is_object())
        result.push_back(
            {.id = scene.value("id", ""), .path = scene.value("path", "")});
  return result;
}

nlohmann::json EditorProjectDocument::inputActions() const {
  if (const auto input = document_.find("input");
      input != document_.end() && input->is_object())
    if (const auto actions = input->find("actions");
        actions != input->end() && actions->is_object())
      return *actions;
  return nlohmann::json::object();
}

bool EditorProjectDocument::commit(nlohmann::json replacement,
                                   std::string &error) {
  if (replacement == document_)
    return true;
  if (!validate(replacement, error))
    return false;
  undo_.push_back({.before = document_, .after = replacement});
  redo_.clear();
  document_ = std::move(replacement);
  return true;
}

bool EditorProjectDocument::validate(const nlohmann::json &document,
                                     std::string &error) const {
  if (document.value("format_version", 0) != 1) {
    error = "Project format_version must be 1.";
    return false;
  }
  if (!runtime::scene_loading::parseProjectData(path_, document, error))
    return false;
  const auto scenes = document.find("scenes");
  std::set<std::string> ids;
  std::set<std::string> paths;
  for (const nlohmann::json &scene : *scenes) {
    const std::string id = scene.value("id", "");
    const std::string path = scene.value("path", "");
    if (!ids.insert(id).second || !paths.insert(path).second) {
      error = "Project scene IDs and paths must be unique.";
      return false;
    }
  }
  if (!ids.contains(document.value("main_scene", ""))) {
    error = "The project main_scene must reference a registered scene.";
    return false;
  }
  if (const auto assets = document.find("assets"); assets != document.end()) {
    if (!assets->is_array()) {
      error = "Project assets must be an array.";
      return false;
    }
    std::set<std::string> uniqueAssets;
    for (const nlohmann::json &asset : *assets) {
      const std::string id = asset.is_string() ? asset.get<std::string>() : "";
      if (!asset.is_string() || !validAssetUri(id) ||
          !uniqueAssets.insert(id).second) {
        error = "Project assets require unique asset:// or asset-group:// IDs.";
        return false;
      }
    }
  }
  if (const auto input = document.find("input"); input != document.end()) {
    if (!input->is_object() ||
        (input->contains("actions") && !(*input)["actions"].is_object())) {
      error = "Project input actions must be an object.";
      return false;
    }
    if (input->contains("actions"))
      for (const auto &[name, action] : (*input)["actions"].items()) {
        const std::string type =
            action.is_object() ? action.value("type", "") : "";
        const std::string context =
            action.is_object() ? action.value("context", "") : "";
        if (name.empty() || !action.is_object() ||
            (type != "button" && type != "axis1d" && type != "vector2") ||
            context.empty() || !action.contains("bindings") ||
            !action["bindings"].is_array() || action["bindings"].empty()) {
          error = "Every input action requires a name, supported type, "
                  "context, and binding.";
          return false;
        }
      }
    if (input->contains("actions") &&
        runtime::input::parseInputActions(document).size() !=
            (*input)["actions"].size()) {
      error = "Input actions must be accepted by the runtime action parser.";
      return false;
    }
  }
  return true;
}

} // namespace demi::editor
