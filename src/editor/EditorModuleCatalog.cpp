#include "editor/EditorModuleCatalog.h"

#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/terrain/TerrainGraphRegistry.h"
#include "demi/runtime/ui/UiPrefabResolver.h"
#include "editor/EditorHudHierarchy.h"
#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

namespace demi::editor {
namespace {

struct HudEntry {
  const char *type;
  const char *category;
  const char *title;
  const char *description;
  const char *icon;
};

constexpr std::array HudEntries{
    HudEntry{"panel", "Layout", "Panel", "A visible surface for a group of controls.", "[]"},
    HudEntry{"container", "Layout", "Container", "Groups elements without drawing a surface.", "::"},
    HudEntry{"scroll", "Layout", "Scroll area", "A region that can scroll through its content.", "<>"},
    HudEntry{"list", "Layout", "List", "A repeating list of items.", "=="},
    HudEntry{"modal", "Layout", "Modal", "A focused overlay for a dialog.", "[]"},
    HudEntry{"label", "Text & media", "Label", "A short line of text.", "Aa"},
    HudEntry{"text", "Text & media", "Text", "A block of readable text.", "T"},
    HudEntry{"image", "Text & media", "Image", "Displays an image asset.", "@"},
    HudEntry{"button", "Controls", "Button", "An action the player can press.", ">"},
    HudEntry{"toggle", "Controls", "Toggle", "An on or off choice.", "o"},
    HudEntry{"slider", "Controls", "Slider", "A draggable value control.", "--"},
    HudEntry{"progress", "Controls", "Progress bar", "Shows progress or a changing value.", "="},
    HudEntry{"text_input", "Controls", "Text input", "Lets the player enter text.", "I"},
    HudEntry{"virtual_stick", "Touch", "Virtual stick", "A touch movement control.", "+"},
    HudEntry{"virtual_button", "Touch", "Virtual button", "A touch action control.", ">"},
};

std::string titleFromReference(std::string_view reference) {
  const auto slash = reference.find_last_of('/');
  std::string title(reference.substr(slash == std::string_view::npos ? 0 : slash + 1));
  std::replace(title.begin(), title.end(), '_', ' ');
  std::replace(title.begin(), title.end(), '-', ' ');
  if (!title.empty())
    title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
  return title;
}

std::string prefabDescription(const nlohmann::json &document) {
  const auto root = document.find("root");
  if (root == document.end() || !root->is_object())
    return "Reusable HUD composition.";
  const auto children = root->find("children");
  if (children == root->end() || !children->is_array() || children->empty())
    return "Reusable HUD element.";
  std::vector<std::string> parts;
  for (const auto &child : *children) {
    if (!child.is_object())
      continue;
    const auto typeField = child.find("type");
    std::string type = typeField != child.end() && typeField->is_string()
                           ? typeField->get<std::string>()
                           : "element";
    if (type == "progress")
      type = "progress bar";
    else if (type == "text_input")
      type = "text input";
    else if (type == "virtual_button")
      type = "touch button";
    std::replace(type.begin(), type.end(), '_', ' ');
    parts.push_back(std::move(type));
  }
  if (parts.empty())
    return "Reusable HUD composition.";
  std::string description = "Reusable group with ";
  for (std::size_t index = 0; index < parts.size(); ++index) {
    if (index != 0)
      description += index + 1 == parts.size() ? " and " : ", ";
    description += parts[index];
  }
  return description + ".";
}

} // namespace

std::vector<EditorModule>
editorModules(const EditorWorkspace &workspace) {
  const auto terrainNodes = runtime::terrainGraphNodeDefinitions();
  std::vector<EditorModule> modules;
  modules.reserve(HudEntries.size() + workspace.sources().size() + terrainNodes.size());
  for (const HudEntry &entry : HudEntries)
    modules.push_back({"hud:" + std::string(entry.type), EditorModuleKind::HudElement,
                       entry.category, entry.title, entry.description, entry.icon,
                       entry.type});
  auto sources = workspace.sources();
  const auto &packageFiles = workspace.assetIndex().registry().packageFiles;
  sources.insert(sources.end(), packageFiles.begin(), packageFiles.end());
  std::ranges::sort(sources);
  sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
  for (const auto &source : sources) {
    if (!isUiPrefabFile(source))
      continue;
    auto reference =
        editorUiPrefabReference(workspace.projectPath().parent_path(), source);
    std::ifstream input(source);
    const auto document = nlohmann::json::parse(input, nullptr, false);
    if (!document.is_object() || !document.contains("id") ||
        !document["id"].is_string())
      continue;
    if (!reference) {
      const auto id = document["id"].get<std::string>();
      const auto resolved = runtime::ui::resolveUiPrefabReference(
          workspace.projectPath().parent_path() / "palette.hud.json", id);
      if (!resolved ||
          resolved->lexically_normal() != source.lexically_normal())
        continue;
      reference = id;
    }
    if (document["id"] != *reference)
      continue;
    modules.push_back({"prefab:" + *reference, EditorModuleKind::UiPrefab,
                       "UI prefabs", titleFromReference(*reference),
                       prefabDescription(document), "[]",
                       *reference});
  }
  for (const auto &node : terrainNodes)
    modules.push_back({"terrain:" + node.type, EditorModuleKind::TerrainNode,
                       node.category, node.label, node.description, "~",
                       node.type});
  return modules;
}

const EditorModule *resolveModule(const std::span<const EditorModule> modules,
                                  const std::string_view id) {
  const auto found = std::ranges::find(modules, id, &EditorModule::id);
  return found == modules.end() ? nullptr : &*found;
}

} // namespace demi::editor
