#include "editor/EditorTerrainGraphPanel.h"
#include "editor/EditorChrome.h"
#include "editor/EditorClipboard.h"
#include "editor/EditorColorControl.h"
#include "editor/EditorGraphCanvasScale.h"
#include "editor/EditorKeyBindings.h"
#include "editor/EditorModulesPanel.h"
#include "editor/EditorTerrainGraphSettings.h"

#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "editor/EditorDragDropPayloads.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imnodes.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace demi::editor {
namespace {

using Json = nlohmann::json;
using Definition = runtime::TerrainGraphNodeDefinition;
using Port = runtime::TerrainGraphPort;

void nextToolbarItem(float width) {
  const float right =
      ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
  if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <=
      right)
    ImGui::SameLine();
}

void graphToolbarGroupBreak() {
  nextToolbarItem(8);
  ImGui::Dummy({8.0F, 30.0F});
  const ImVec2 minimum = ImGui::GetItemRectMin();
  const ImVec2 maximum = ImGui::GetItemRectMax();
  const float x = (minimum.x + maximum.x) * 0.5F;
  ImGui::GetWindowDrawList()->AddLine({x, minimum.y + 5.0F},
                                      {x, maximum.y - 5.0F},
                                      ImGui::GetColorU32(ImGuiCol_Border));
}

Json graphOf(const Json &draft) {
  if (draft.contains("graph") && !draft["graph"].is_null())
    return draft["graph"];
  return Json{{"format_version", 1},
              {"nodes", Json::array()},
              {"links", Json::array()},
              {"output", ""}};
}

std::string fieldKey(std::string_view node, std::string_view name) {
  return std::string(node) + ":" + std::string(name);
}

ImU32 pinColor(runtime::TerrainGraphValueKind kind) {
  using Kind = runtime::TerrainGraphValueKind;
  switch (kind) {
  case Kind::Field:
    return IM_COL32(226, 171, 92, 255);
  case Kind::Mask:
    return IM_COL32(159, 133, 223, 255);
  case Kind::Drainage:
    return IM_COL32(103, 178, 206, 255);
  case Kind::Water:
    return IM_COL32(76, 160, 226, 255);
  case Kind::Instances:
    return IM_COL32(121, 194, 133, 255);
  }
  return IM_COL32_WHITE;
}

std::string diagnosticNode(const Json &graph,
                           const runtime::TerrainGraphDiagnostic &diagnostic) {
  if (!diagnostic.node.empty())
    return diagnostic.node;
  constexpr std::string_view nodePrefix = "Node ";
  constexpr std::string_view linkPrefix = "Link ";
  const std::string_view message = diagnostic.message;
  if (message.starts_with(nodePrefix)) {
    const std::size_t colon = message.find(':', nodePrefix.size());
    if (colon != std::string_view::npos)
      return std::string(
          message.substr(nodePrefix.size(), colon - nodePrefix.size()));
  }
  if (message.starts_with(linkPrefix)) {
    const std::size_t colon = message.find(':', linkPrefix.size());
    if (colon != std::string_view::npos) {
      const std::string id(
          message.substr(linkPrefix.size(), colon - linkPrefix.size()));
      if (graph.contains("links") && graph["links"].is_array()) {
        for (const Json &link : graph["links"])
          if (link.is_object() && link.value("id", "") == id &&
              link.contains("to") && link["to"].is_object())
            return link["to"].value("node", "");
      }
    }
  }
  if (message.find("graph output") != std::string_view::npos)
    return graph.value("output", "");
  return {};
}

Json parameterDefaults(const Definition &definition) {
  Json values = Json::object();
  for (const auto &parameter : definition.parameters)
    values[parameter.name] = parameter.defaultValue;
  return values;
}

bool outsideParameterRange(const Json &value,
                           const runtime::TerrainGraphParameter &definition) {
  if (!value.is_number())
    return false;
  const double number = value.get<double>();
  if (definition.minimum &&
      (definition.exclusiveMinimum ? number <= *definition.minimum
                                   : number < *definition.minimum))
    return true;
  return definition.maximum && number > *definition.maximum;
}

} // namespace

EditorTerrainGraphPanel::~EditorTerrainGraphPanel() { releaseUiResources(); }

void EditorTerrainGraphPanel::releaseUiResources() noexcept {
  if (context_)
    ImNodes::SetCurrentContext(context_);
  if (editorContext_)
    ImNodes::EditorContextFree(editorContext_);
  editorContext_ = nullptr;
  if (context_)
    ImNodes::DestroyContext(context_);
  context_ = nullptr;
}

void EditorTerrainGraphPanel::bind(const EditorWorkspace &workspace) {
  const std::string current = workspace.terrainAuthoring().entityId();
  entityId_ = current;
  // Selection can leave the terrain temporarily. Its draft belongs to the
  // source document, so keep graph history until another source is bound.
  if (current.empty())
    return;
  const auto *asset = workspace.terrainEditingAsset()
                          ? workspace.terrainAssetDocument()
                          : nullptr;
  const std::string sourceIdentity = asset ? asset->id() : current;
  const std::string identity =
      workspace.terrainAuthoringDocumentPath().string() + "#" + sourceIdentity;
  if (binding_ == identity)
    return;
  binding_ = identity;
  document_.bind(identity);
  selectedNode_.clear();
  settingsNode_.clear();
  selectedLink_.clear();
  selectedNodes_.clear();
  selectedLinks_.clear();
  selectionPending_ = false;
  uiIds_.clear();
  pins_.clear();
  links_.clear();
  shownPositions_.clear();
  textEdits_.clear();
  numericEdits_.clear();
  waypointEdits_.clear();
  nextUiId_ = 1;
  canvasView_.reset();
  lastPasteClipboard_.clear();
  nextPasteOffset_ = 32.0F;
  releaseUiResources();
}

void EditorTerrainGraphPanel::open(const EditorWorkspace &workspace) {
  bind(workspace);
  open_ = true;
}

bool EditorTerrainGraphPanel::openNodeSettings(const EditorWorkspace &workspace,
                                               std::string_view nodeId,
                                               std::string &error) {
  const Json graph = graphOf(workspace.terrainAuthoring().draft());
  if (!graph.is_object() || !graph.contains("nodes") ||
      !graph["nodes"].is_array()) {
    error = "The terrain has no graph to inspect.";
    return false;
  }
  const auto target =
      std::ranges::find_if(graph.at("nodes"), [&](const auto &node) {
        return node.is_object() && node.value("id", std::string{}) == nodeId;
      });
  if (target == graph.at("nodes").end()) {
    error = "No terrain node has ID '" + std::string(nodeId) + "'.";
    return false;
  }
  const auto type = target->value("type", std::string{});
  if (type != "biomes" && type != "scatter" && type != "output" &&
      type != "landform") {
    error = "This node's parameters are edited directly on its card.";
    return false;
  }
  open(workspace);
  settingsNode_ = nodeId;
  showSettings_ = true;
  settingsExpanded_ = true;
  return true;
}

bool EditorTerrainGraphPanel::acceptModulePayload(std::string_view moduleId) {
  constexpr std::string_view prefix = "terrain:";
  if (!moduleId.starts_with(prefix))
    return false;
  const std::string_view type = moduleId.substr(prefix.size());
  if (!runtime::terrainGraphNodeDefinition(type))
    return false;
  pendingNodeType_ = type;
  open_ = true;
  return true;
}

void EditorTerrainGraphPanel::selectNodes(std::vector<std::string> nodes) {
  selectedNodes_ = std::move(nodes);
  selectedLinks_.clear();
  selectedNode_ = selectedNodes_.empty() ? "" : selectedNodes_.front();
  selectedLink_.clear();
  // Newly pasted nodes don't exist in imnodes until their first draw.
  selectionPending_ = true;
}

void EditorTerrainGraphPanel::applyPendingSelection(const Json &graph) {
  if (!selectionPending_ || !editorContext_)
    return;
  ImNodes::ClearNodeSelection();
  ImNodes::ClearLinkSelection();
  for (const std::string &node : selectedNodes_) {
    const bool exists = std::any_of(
        graph.at("nodes").begin(), graph.at("nodes").end(),
        [&](const Json &candidate) { return candidate.at("id") == node; });
    if (!exists)
      continue;
    const auto found = uiIds_.find("n:" + node);
    if (found != uiIds_.end())
      ImNodes::SelectNode(found->second);
  }
  selectionPending_ = false;
}

void EditorTerrainGraphPanel::readSelection(const Json &graph) {
  selectedNodes_.clear();
  selectedLinks_.clear();
  std::vector<int> nodes(static_cast<std::size_t>(ImNodes::NumSelectedNodes()));
  if (!nodes.empty())
    ImNodes::GetSelectedNodes(nodes.data());
  for (const Json &node : graph.at("nodes")) {
    const std::string id = node.at("id").get<std::string>();
    if (std::find(nodes.begin(), nodes.end(), uiId("n:" + id)) != nodes.end())
      selectedNodes_.push_back(id);
  }
  std::vector<int> links(static_cast<std::size_t>(ImNodes::NumSelectedLinks()));
  if (!links.empty())
    ImNodes::GetSelectedLinks(links.data());
  for (const int id : links)
    if (const auto found = links_.find(id); found != links_.end())
      selectedLinks_.push_back(found->second);
  selectedNode_ = selectedNodes_.empty() ? "" : selectedNodes_.front();
  selectedLink_ = selectedLinks_.empty() ? "" : selectedLinks_.front();
}

bool EditorTerrainGraphPanel::executeCommand(EditorWorkspace &workspace,
                                             EditorCommand command,
                                             std::string &error) {
  error.clear();
  if (command != EditorCommand::Copy && command != EditorCommand::Cut &&
      command != EditorCommand::Paste && command != EditorCommand::Duplicate &&
      command != EditorCommand::Delete && command != EditorCommand::SelectAll &&
      command != EditorCommand::Undo && command != EditorCommand::Redo &&
      command != EditorCommand::FrameSelection)
    return false;
  workspace.syncTerrainAuthoring();
  bind(workspace);
  auto &authoring = workspace.terrainAuthoring();
  if (entityId_.empty()) {
    error = "Select a terrain before editing its graph.";
    return false;
  }
  if (command == EditorCommand::FrameSelection) {
    frameSelectionRequested_ = true;
    return true;
  }
  if (authoring.busy() || authoring.stroking()) {
    error = "Finish terrain generation or the brush stroke before editing the "
            "graph.";
    return false;
  }
  Json &draft = authoring.draft();
  switch (command) {
  case EditorCommand::Copy:
  case EditorCommand::Cut: {
    const auto subgraph = document_.copySelection(draft, selectedNodes_, error);
    if (!subgraph)
      return false;
    const std::string encoded =
        encodeEditorClipboard(EditorClipboardKind::TerrainGraph, *subgraph);
    ImGui::SetClipboardText(encoded.c_str());
    const char *published = ImGui::GetClipboardText();
    if (!published || encoded != published) {
      error = "Could not publish terrain nodes to the system clipboard.";
      return false;
    }
    lastPasteClipboard_ = encoded;
    nextPasteOffset_ = 32.0F;
    if (command == EditorCommand::Copy)
      return true;
    if (!document_.removeSelection(draft, selectedNodes_, {}, error))
      return false;
    selectNodes({});
    return true;
  }
  case EditorCommand::Paste: {
    const char *text = ImGui::GetClipboardText();
    const std::string encoded = text ? text : "";
    const auto payload = decodeEditorClipboard(encoded, error);
    if (!payload)
      return false;
    if (payload->kind != EditorClipboardKind::TerrainGraph) {
      error = "The clipboard does not contain terrain graph nodes.";
      return false;
    }
    const float offset =
        lastPasteClipboard_ == encoded ? nextPasteOffset_ : 32.0F;
    std::vector<std::string> pasted;
    if (!document_.pasteSelection(draft, payload->data, offset, offset, pasted,
                                  error))
      return false;
    lastPasteClipboard_ = encoded;
    nextPasteOffset_ = offset + 32.0F;
    selectNodes(std::move(pasted));
    return true;
  }
  case EditorCommand::Duplicate: {
    std::vector<std::string> duplicates;
    if (!document_.duplicateSelection(draft, selectedNodes_, duplicates, error))
      return false;
    selectNodes(std::move(duplicates));
    return true;
  }
  case EditorCommand::Delete:
    if (!document_.removeSelection(draft, selectedNodes_, selectedLinks_,
                                   error))
      return false;
    selectNodes({});
    return true;
  case EditorCommand::SelectAll: {
    std::vector<std::string> nodes;
    const Json graph = graphOf(draft);
    for (const Json &node : graph.at("nodes"))
      nodes.push_back(node.at("id").get<std::string>());
    selectNodes(std::move(nodes));
    return true;
  }
  case EditorCommand::Undo:
  case EditorCommand::Redo: {
    const bool changed = command == EditorCommand::Undo ? document_.undo(draft)
                                                        : document_.redo(draft);
    if (!changed) {
      error = "No graph draft history is available for this action.";
      return false;
    }
    shownPositions_.clear();
    selectNodes({});
    return true;
  }
  default:
    return false;
  }
}

std::string
EditorTerrainGraphPanel::commandTooltip(EditorCommand command) const {
  const auto &definition = editorCommandDefinition(command);
  std::string tooltip(definition.description);
  const std::string shortcut = keyBindings_ ? keyBindings_->label(command) : "";
  if (!shortcut.empty())
    tooltip += " (" + shortcut + ")";
  return tooltip;
}

void EditorTerrainGraphPanel::drawCommandButton(EditorWorkspace &workspace,
                                                EditorCommand command,
                                                std::string &notice,
                                                bool enabled) {
  EditorIcon icon = EditorIcon::Pointer;
  switch (command) {
  case EditorCommand::Copy:
    icon = EditorIcon::Copy;
    break;
  case EditorCommand::Cut:
    icon = EditorIcon::Cut;
    break;
  case EditorCommand::Paste:
    icon = EditorIcon::Paste;
    break;
  case EditorCommand::Duplicate:
    icon = EditorIcon::Duplicate;
    break;
  case EditorCommand::Delete:
    icon = EditorIcon::Delete;
    break;
  case EditorCommand::SelectAll:
    icon = EditorIcon::SelectAll;
    break;
  case EditorCommand::Undo:
    icon = EditorIcon::Undo;
    break;
  case EditorCommand::Redo:
    icon = EditorIcon::Redo;
    break;
  default:
    return;
  }
  const std::string id =
      "graph-command-" + std::string(editorCommandDefinition(command).id);
  const std::string tooltip = commandTooltip(command);
  if (editorIconButton(id.c_str(), icon, tooltip.c_str(), false, enabled)) {
    std::string error;
    if (!executeCommand(workspace, command, error) && !error.empty())
      notice = std::move(error);
  }
}

int EditorTerrainGraphPanel::uiId(std::string_view key) {
  const auto [found, inserted] =
      uiIds_.try_emplace(std::string(key), nextUiId_);
  if (inserted)
    ++nextUiId_;
  return found->second;
}

void EditorTerrainGraphPanel::addPendingNode(EditorWorkspace &workspace,
                                             float x, float y,
                                             std::string &notice) {
  const Definition *definition =
      runtime::terrainGraphNodeDefinition(pendingNodeType_);
  if (!definition)
    return;
  auto &draft = workspace.terrainAuthoring().draft();
  std::string nodeId;
  std::string error;
  if (!document_.addNode(draft, definition->type,
                         parameterDefaults(*definition), x, y, nodeId, error,
                         true)) {
    notice = std::move(error);
    pendingNodeType_.clear();
    return;
  }
  selectNodes({nodeId});
  pendingNodeType_.clear();
}

void EditorTerrainGraphPanel::drawPalette(EditorWorkspace &workspace,
                                          std::string &notice) {
  ImGui::BeginChild("Terrain graph modules", {218.0F, 0.0F},
                    ImGuiChildFlags_Borders);
  ImGui::TextUnformatted("Terrain Nodes");
  ImGui::Separator();
  std::string category;
  for (const Definition &definition : runtime::terrainGraphNodeDefinitions()) {
    if (definition.category != category) {
      category = definition.category;
      ImGui::Spacing();
      ImGui::TextDisabled("%s", category.c_str());
    }
    const EditorModule module{"terrain:" + definition.type,
                              EditorModuleKind::TerrainNode,
                              definition.category,
                              definition.label,
                              definition.description,
                              "~",
                              definition.type};
    if (drawEditorModuleCard(module)) {
      pendingNodeType_ = definition.type;
      addPendingNode(workspace, 55.0F, 65.0F, notice);
    }
  }
  ImGui::EndChild();
}

void EditorTerrainGraphPanel::clearWaypointEdits(std::string_view nodeId) {
  const std::string prefix = std::string(nodeId) + ":river_path:";
  std::erase_if(waypointEdits_, [&](const auto &entry) {
    return entry.first.starts_with(prefix);
  });
}

std::optional<Json> EditorTerrainGraphPanel::drawRiverPath(
    std::string_view nodeId, const Json &path, const Json &defaultPath) {
  const bool valid =
      path.is_array() &&
      std::all_of(path.begin(), path.end(), [](const Json &point) {
        return point.is_array() && point.size() == 3 &&
               std::all_of(point.begin(), point.end(),
                           [](const Json &coordinate) {
                             return coordinate.is_number() &&
                                    std::isfinite(coordinate.get<double>());
                           });
      });
  if (!valid) {
    ImGui::TextColored({1.0F, 0.48F, 0.48F, 1.0F},
                       "Expected a list of XYZ waypoints.");
    if (ImGui::SmallButton("Restore default waypoints")) {
      clearWaypointEdits(nodeId);
      return defaultPath;
    }
    return std::nullopt;
  }

  std::optional<Json> edited;
  static constexpr std::array axisNames{"X", "Y", "Z"};
  static constexpr std::array axisIds{"##x", "##y", "##z"};
  for (std::size_t index = 0; index < path.size(); ++index) {
    ImGui::PushID(static_cast<int>(index));
    ImGui::Text("Waypoint %zu", index + 1);
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
      Json shortened = edited.value_or(path);
      shortened.erase(shortened.begin() + static_cast<std::ptrdiff_t>(index));
      clearWaypointEdits(nodeId);
      ImGui::PopID();
      return shortened;
    }
    const Json &point = path[index];
    const std::array<double, 3> source{
        point[0].get<double>(), point[1].get<double>(), point[2].get<double>()};
    const std::string cacheKey =
        std::string(nodeId) + ":river_path:" + std::to_string(index);
    auto &coordinates =
        waypointEdits_.try_emplace(cacheKey, source).first->second;
    bool finished = false;
    bool active = false;
    for (std::size_t axis = 0; axis < coordinates.size(); ++axis) {
      ImGui::TextDisabled("%s", axisNames[axis]);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(154.0F * canvasView_.zoom());
      ImGui::InputDouble(axisIds[axis], &coordinates[axis], 0, 0, "%.3f");
      finished = ImGui::IsItemDeactivatedAfterEdit() || finished;
      active = ImGui::IsItemActive() || active;
    }
    if (finished) {
      if (std::all_of(
              coordinates.begin(), coordinates.end(),
              [](double coordinate) { return std::isfinite(coordinate); })) {
        if (!edited)
          edited = path;
        (*edited)[index] = {coordinates[0], coordinates[1], coordinates[2]};
      } else {
        ImGui::TextColored({1.0F, 0.48F, 0.48F, 1.0F},
                           "Waypoint coordinates must be finite.");
      }
    } else if (!active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      coordinates = source;
    }
    ImGui::Separator();
    ImGui::PopID();
  }
  if (ImGui::SmallButton("Add waypoint")) {
    Json extended = edited.value_or(path);
    if (extended.empty()) {
      extended.push_back(Json::array({0.0, 0.0, 0.0}));
    } else {
      const Json &last = extended.back();
      extended.push_back(
          Json::array({last[0].get<double>() + 1.0, last[1].get<double>(),
                       last[2].get<double>()}));
    }
    clearWaypointEdits(nodeId);
    return extended;
  }
  return edited;
}

void EditorTerrainGraphPanel::drawParameter(
    EditorWorkspace &workspace, std::string_view nodeId, const Json &parameters,
    const runtime::TerrainGraphParameter &definition, std::string &notice) {
  const Json value = parameters.value(definition.name, definition.defaultValue);
  const std::string key = fieldKey(nodeId, definition.name);
  ImGui::PushID(definition.name.c_str());
  ImNodes::BeginStaticAttribute(uiId("s:" + key));
  ImGui::TextUnformatted(definition.label.c_str());
  if (ImGui::IsItemHovered() &&
      (!definition.help.empty() || definition.minimum || definition.maximum)) {
    ImGui::BeginTooltip();
    if (!definition.help.empty())
      ImGui::TextWrapped("%s", definition.help.c_str());
    if (definition.minimum)
      ImGui::Text("Minimum: %s %g", definition.exclusiveMinimum ? ">" : ">=",
                  *definition.minimum);
    if (definition.maximum)
      ImGui::Text("Maximum: <= %g", *definition.maximum);
    ImGui::EndTooltip();
  }
  ImGui::SetNextItemWidth(178.0F * canvasView_.zoom());
  bool changed = false;
  Json edited;
  switch (definition.kind) {
  case runtime::TerrainGraphParameterKind::Color: {
    Json &working = numericEdits_.try_emplace(key, value).first->second;
    float rgba[4]{working.at(0).get<float>(), working.at(1).get<float>(),
                  working.at(2).get<float>(), working.at(3).get<float>()};
    const bool colorChanged =
        drawEditorColorControl("##value", rgba,
                               {.flags = ImGuiColorEditFlags_AlphaBar |
                                         ImGuiColorEditFlags_DisplayHex});
    if (colorChanged)
      working = Json::array({rgba[0], rgba[1], rgba[2], rgba[3]});
    if (ImGui::IsItemDeactivatedAfterEdit() ||
        (colorChanged && !ImGui::IsItemActive())) {
      edited = working;
      changed = true;
    } else if (!ImGui::IsAnyItemActive()) {
      working = value;
    }
    break;
  }
  case runtime::TerrainGraphParameterKind::Number: {
    Json &working = numericEdits_.try_emplace(key, value).first->second;
    float number = working.is_number() ? working.get<float>() : 0.0F;
    if (ImGui::DragFloat("##value", &number, 0.05F, 0.0F, 0.0F, "%.3f") &&
        std::isfinite(number))
      working = number;
    const bool finished = ImGui::IsItemDeactivatedAfterEdit();
    if (finished) {
      edited = working;
      changed = true;
    } else if (!ImGui::IsItemActive() &&
               !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      working = value;
    }
    break;
  }
  case runtime::TerrainGraphParameterKind::Integer: {
    Json &working = numericEdits_.try_emplace(key, value).first->second;
    int number = working.is_number_integer() ? working.get<int>() : 0;
    if (ImGui::DragInt("##value", &number))
      working = number;
    const bool finished = ImGui::IsItemDeactivatedAfterEdit();
    if (finished) {
      edited = working;
      changed = true;
    } else if (!ImGui::IsItemActive() &&
               !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      working = value;
    }
    break;
  }
  case runtime::TerrainGraphParameterKind::Boolean: {
    bool boolean = value.is_boolean() && value.get<bool>();
    if (ImGui::Checkbox("##value", &boolean)) {
      edited = boolean;
      changed = true;
    }
    break;
  }
  case runtime::TerrainGraphParameterKind::Choice: {
    const std::string selected =
        value.is_string() ? value.get<std::string>() : "";
    if (ImGui::BeginCombo("##value", selected.c_str())) {
      for (const std::string &choice : definition.choices) {
        if (ImGui::Selectable(choice.c_str(), choice == selected)) {
          edited = choice;
          changed = true;
        }
      }
      ImGui::EndCombo();
    }
    break;
  }
  case runtime::TerrainGraphParameterKind::Text:
  case runtime::TerrainGraphParameterKind::Structured: {
    if (definition.kind == runtime::TerrainGraphParameterKind::Structured &&
        definition.name == "river_path") {
      if (auto path = drawRiverPath(nodeId, value, definition.defaultValue)) {
        edited = std::move(*path);
        changed = true;
      }
      break;
    }
    const bool structured =
        definition.kind == runtime::TerrainGraphParameterKind::Structured;
    const std::string source = structured          ? value.dump(2)
                               : value.is_string() ? value.get<std::string>()
                                                   : std::string{};
    std::string &buffer = textEdits_.try_emplace(key, source).first->second;
    const bool submitted = ImGui::InputText(
        "##value", &buffer, ImGuiInputTextFlags_EnterReturnsTrue);
    if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
      if (structured) {
        edited = Json::parse(buffer, nullptr, false);
        if (edited.is_discarded())
          notice = definition.label + " must be valid JSON.";
        else
          changed = true;
      } else {
        edited = buffer;
        changed = true;
      }
    } else if (!ImGui::IsItemActive() &&
               !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      buffer = source;
    }
    break;
  }
  }
  if (outsideParameterRange(value, definition))
    ImGui::TextColored({1.0F, 0.48F, 0.48F, 1.0F}, "Outside allowed range");
  ImNodes::EndStaticAttribute();
  ImGui::PopID();
  if (changed) {
    std::string error;
    if (!document_.setParameter(workspace.terrainAuthoring().draft(), nodeId,
                                definition.name, std::move(edited), error))
      notice = std::move(error);
  }
}

void EditorTerrainGraphPanel::drawNode(
    EditorWorkspace &workspace, const Json &node,
    const std::unordered_map<std::string, std::string> &errors,
    std::string &notice) {
  if (!node.is_object() || !node.contains("id") || !node["id"].is_string() ||
      !node.contains("type") || !node["type"].is_string())
    return;
  const std::string id = node["id"].get<std::string>();
  const std::string type = node["type"].get<std::string>();
  const Definition *definition = runtime::terrainGraphNodeDefinition(type);
  const int nodeUiId = uiId("n:" + id);
  const Json position = node.value("position", Json::array({0.0F, 0.0F}));
  if (position.is_array() && position.size() == 2 && position[0].is_number() &&
      position[1].is_number() &&
      (!shownPositions_.contains(id) || shownPositions_[id] != position)) {
    ImNodes::SetNodeGridSpacePos(
        nodeUiId, canvasView_.toDisplay(
                      {position[0].get<float>(), position[1].get<float>()}));
    shownPositions_[id] = position;
  }
  const bool hasError = errors.contains(id);
  if (hasError)
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(148, 53, 55, 255));
  ImNodes::BeginNode(nodeUiId);
  ImNodes::BeginNodeTitleBar();
  ImGui::TextUnformatted(definition ? definition->label.c_str() : type.c_str());
  ImNodes::EndNodeTitleBar();
  if (hasError)
    ImNodes::PopColorStyle();
  ImGui::TextDisabled("%s", id.c_str());
  if (hasError) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                           192.0F * canvasView_.zoom());
    ImGui::TextColored({1.0F, 0.48F, 0.48F, 1.0F}, "%s", errors.at(id).c_str());
    ImGui::PopTextWrapPos();
  }
  if (definition) {
    if (!definition->inputs.empty())
      ImGui::TextDisabled("Inputs");
    for (const Port &port : definition->inputs) {
      const int pinId = uiId("p:" + id + ":i:" + port.name);
      pins_[pinId] = {id, port.name, false};
      ImNodes::PushColorStyle(ImNodesCol_Pin, pinColor(port.kind));
      ImNodes::BeginInputAttribute(pinId);
      ImGui::Text("%s%s", port.label.c_str(), port.required ? " *" : "");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s input",
                          runtime::terrainGraphValueKindName(port.kind).data());
      drawInputSource(workspace, id, port, notice);
      ImNodes::EndInputAttribute();
      ImNodes::PopColorStyle();
    }
    const Json parameters = node.value("parameters", Json::object());
    if (parameters.is_object()) {
      if (!definition->parameters.empty())
        ImGui::TextDisabled("Parameters");
      for (const auto &parameter : definition->parameters)
        drawParameter(workspace, id, parameters, parameter, notice);
    }
    if (type == "biomes" || type == "scatter" || type == "landform" ||
        type == "output") {
      ImNodes::BeginStaticAttribute(uiId("settings:" + id));
      ImGui::PushID(id.c_str());
      if (ImGui::Button(type == "biomes"     ? "Colors and biome rules..."
                        : type == "scatter"  ? "Choose asset palette..."
                        : type == "landform" ? "Landscape shapes..."
                                             : "Terrain settings...")) {
        std::string error;
        if (!openNodeSettings(workspace, id, error))
          notice = error;
      }
      if (type == "output") {
        const bool active =
            workspace.terrainAuthoring().draft().at("graph").value("output",
                                                                   "") == id;
        if (ImGui::RadioButton("Active terrain output", active) && !active) {
          std::string error;
          if (!document_.setOutput(workspace.terrainAuthoring().draft(), id,
                                   error))
            notice = error;
        }
      }
      ImGui::PopID();
      ImNodes::EndStaticAttribute();
    }
    if (!definition->outputs.empty())
      ImGui::TextDisabled("Outputs");
    for (const Port &port : definition->outputs) {
      const int pinId = uiId("p:" + id + ":o:" + port.name);
      pins_[pinId] = {id, port.name, true};
      ImNodes::PushColorStyle(ImNodesCol_Pin, pinColor(port.kind));
      ImNodes::BeginOutputAttribute(pinId);
      ImGui::TextUnformatted(port.label.c_str());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s output. Drag this pin to a matching input, or "
                          "select it in an input's source dropdown.",
                          runtime::terrainGraphValueKindName(port.kind).data());
      ImNodes::EndOutputAttribute();
      ImNodes::PopColorStyle();
    }
  }
  const auto surface = workspace.terrainAuthoring().surface();
  const auto field = surface ? surface->heightField() : nullptr;
  if (field && field->graphArtifacts) {
    const auto &runs = field->graphArtifacts->nodes;
    const auto run =
        std::ranges::find(runs, id, &runtime::TerrainGraphNodeRun::id);
    if (run != runs.end()) {
      if (run->cached)
        ImGui::TextDisabled("Last generation: reused");
      else
        ImGui::TextDisabled("Last generation: %.2f ms", run->milliseconds);
    }
  }
  ImNodes::EndNode();
}

void EditorTerrainGraphPanel::drawDiagnostics(const Json &graph) {
  const auto diagnostics = runtime::terrainGraphDiagnostics(graph);
  if (diagnostics.empty()) {
    ImGui::TextColored({0.52F, 0.78F, 0.59F, 1.0F}, "Graph ready to generate");
    return;
  }
  ImGui::TextColored({0.96F, 0.63F, 0.39F, 1.0F}, "%zu graph issue(s)",
                     diagnostics.size());
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    for (const auto &diagnostic : diagnostics)
      ImGui::TextWrapped("%s%s%s: %s", diagnostic.node.c_str(),
                         diagnostic.port.empty() ? "" : ".",
                         diagnostic.port.c_str(), diagnostic.message.c_str());
    ImGui::EndTooltip();
  }
}

void EditorTerrainGraphPanel::drawCanvas(EditorWorkspace &workspace,
                                         std::string &notice) {
  auto &draft = workspace.terrainAuthoring().draft();
  if (!draft.contains("graph") || draft["graph"].is_null()) {
    ImGui::TextWrapped("Conventional landform recipe. Its settings generate "
                       "terrain directly. Choose Starter graph to convert "
                       "it to connected modules.");
    return;
  }
  const Json graph = graphOf(draft);
  if (!graph.is_object() || !graph.value("nodes", Json::array()).is_array() ||
      !graph.value("links", Json::array()).is_array()) {
    ImGui::TextColored({1.0F, 0.5F, 0.5F, 1.0F}, "Graph source is malformed.");
    return;
  }
  std::unordered_map<std::string, std::string> errors;
  for (const auto &diagnostic : runtime::terrainGraphDiagnostics(graph)) {
    const std::string node = diagnosticNode(graph, diagnostic);
    if (!node.empty() && !errors.contains(node))
      errors.emplace(node, diagnostic.message);
  }

  if (context_)
    ImNodes::SetCurrentContext(context_);
  else if (!ImNodes::GetCurrentContext())
    context_ = ImNodes::CreateContext();
  if (!editorContext_)
    editorContext_ = ImNodes::EditorContextCreate();
  ImNodes::EditorContextSet(editorContext_);
  pins_.clear();
  links_.clear();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 canvasExtent = ImGui::GetContentRegionAvail();
  if (resetZoomRequested_) {
    const auto size = ImGui::GetContentRegionAvail();
    ImNodes::EditorContextResetPanning(
        canvasView_.zoomAt(1.0F, {size.x * 0.5F, size.y * 0.5F},
                           ImNodes::EditorContextGetPanning()));
    shownPositions_.clear();
    resetZoomRequested_ = false;
  }
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
      !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
      ImGui::GetIO().MouseWheel != 0.0F) {
    const float before = canvasView_.zoom();
    const auto mouse = ImGui::GetMousePos();
    ImNodes::EditorContextResetPanning(canvasView_.wheelAt(
        ImGui::GetIO().MouseWheel, {mouse.x - origin.x, mouse.y - origin.y},
        ImNodes::EditorContextGetPanning()));
    if (before != canvasView_.zoom())
      shownPositions_.clear();
  }
  EditorGraphCanvasScale scale(canvasView_.zoom());
  std::optional<ImVec2> droppedNodePosition;
  ImNodes::BeginNodeEditor();
  const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2{canvasView_.zoom(), canvasView_.zoom()});
  for (const Json &node : graph.at("nodes"))
    drawNode(workspace, node, errors, notice);
  for (const Json &link : graph.at("links")) {
    if (!link.is_object() || !link.contains("id") || !link["id"].is_string() ||
        !link.contains("from") || !link.contains("to") ||
        !link["from"].is_object() || !link["to"].is_object())
      continue;
    const std::string fromNode = link["from"].value("node", "");
    const std::string fromPort = link["from"].value("port", "");
    const std::string toNode = link["to"].value("node", "");
    const std::string toPort = link["to"].value("port", "");
    const int from = uiId("p:" + fromNode + ":o:" + fromPort);
    const int to = uiId("p:" + toNode + ":i:" + toPort);
    if (!pins_.contains(from) || !pins_.contains(to))
      continue;
    const std::string id = link["id"].get<std::string>();
    const int linkUiId = uiId("l:" + id);
    links_[linkUiId] = id;
    ImNodes::Link(linkUiId, from, to);
  }
  ImNodes::MiniMap(0.16F, ImNodesMiniMapLocation_BottomRight);
  ImGui::PopStyleVar();
  // Handle delivery while the canvas child is current. EndNodeEditor leaves
  // a group item in the parent, which is not a reliable drag/drop target.
  const ImRect dropBounds(canvasOrigin, {canvasOrigin.x + canvasSize.x,
                                         canvasOrigin.y + canvasSize.y});
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                             ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
      ImGui::BeginDragDropTargetCustom(dropBounds,
                                       ImGui::GetID("terrain-canvas-drop"))) {
    const ImGuiPayload *payload =
        ImGui::AcceptDragDropPayload(EditorModulePayload);
    if (payload && payload->IsDelivery() && payload->Data &&
        payload->DataSize > 1) {
      const auto *data = static_cast<const char *>(payload->Data);
      if (data[payload->DataSize - 1] == '\0' &&
          acceptModulePayload(std::string_view(data, payload->DataSize - 1))) {
        const auto pan = ImNodes::EditorContextGetPanning();
        const auto mouse = ImGui::GetMousePos();
        droppedNodePosition =
            canvasView_.toDocument({mouse.x - canvasOrigin.x - pan.x,
                                    mouse.y - canvasOrigin.y - pan.y});
      }
    }
    ImGui::EndDragDropTarget();
  }
  ImNodes::EndNodeEditor();
  applyPendingSelection(graph);
  readSelection(graph);
  if (frameSelectionRequested_) {
    ImVec2 minimum{std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()};
    ImVec2 maximum{std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest()};
    bool hasBounds = false;
    for (const Json &node : graph.at("nodes")) {
      const std::string id = node.at("id").get<std::string>();
      if (!selectedNodes_.empty() &&
          std::find(selectedNodes_.begin(), selectedNodes_.end(), id) ==
              selectedNodes_.end())
        continue;
      const int nodeUiId = uiId("n:" + id);
      const ImVec2 position = ImNodes::GetNodeGridSpacePos(nodeUiId);
      const ImVec2 size = ImNodes::GetNodeDimensions(nodeUiId);
      minimum.x = std::min(minimum.x, position.x);
      minimum.y = std::min(minimum.y, position.y);
      maximum.x = std::max(maximum.x, position.x + size.x);
      maximum.y = std::max(maximum.y, position.y + size.y);
      hasBounds = true;
    }
    if (hasBounds) {
      ImNodes::EditorContextResetPanning(
          {(canvasExtent.x - minimum.x - maximum.x) * 0.5F,
           (canvasExtent.y - minimum.y - maximum.y) * 0.5F});
    }
    frameSelectionRequested_ = false;
  }

  if (workspace.terrainAuthoring().busy() ||
      workspace.terrainAuthoring().stroking())
    return;

  int first = 0;
  int second = 0;
  if (ImNodes::IsLinkCreated(&first, &second)) {
    const auto a = pins_.find(first);
    const auto b = pins_.find(second);
    if (a != pins_.end() && b != pins_.end() &&
        a->second.output != b->second.output) {
      const Pin &from = a->second.output ? a->second : b->second;
      const Pin &to = a->second.output ? b->second : a->second;
      std::string error;
      if (!document_.connect(draft, from.node, from.port, to.node, to.port,
                             error))
        notice = std::move(error);
    } else {
      notice = "Connect an output pin to an input pin.";
    }
  }
  int destroyed = 0;
  if (ImNodes::IsLinkDestroyed(&destroyed) && links_.contains(destroyed)) {
    std::string error;
    if (!document_.removeLink(draft, links_.at(destroyed), error))
      notice = std::move(error);
  }

  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    std::vector<EditorTerrainGraphDocument::NodePosition> moved;
    for (const Json &node : graph.at("nodes")) {
      if (!node.is_object() || !node.contains("id") || !node["id"].is_string())
        continue;
      const std::string id = node["id"].get<std::string>();
      const ImVec2 position =
          canvasView_.toDocument(ImNodes::GetNodeGridSpacePos(uiId("n:" + id)));
      if (!std::isfinite(position.x) || !std::isfinite(position.y))
        continue;
      const auto authored = node.value("position", Json::array({0.0F, 0.0F}));
      if (authored.is_array() && authored.size() == 2 &&
          authored[0].is_number() && authored[1].is_number()) {
        const float x = authored[0].get<float>();
        const float y = authored[1].get<float>();
        const float tolerance = std::numeric_limits<float>::epsilon() * 16 *
                                std::max({1.0F, std::abs(x), std::abs(y)});
        if (std::abs(position.x - x) <= tolerance &&
            std::abs(position.y - y) <= tolerance)
          continue;
      }
      moved.push_back({id, position.x, position.y});
    }
    std::string error;
    if (!moved.empty() && document_.setPositions(draft, moved, error)) {
      for (const auto &position : moved)
        shownPositions_[position.nodeId] =
            Json::array({position.x, position.y});
    } else if (!error.empty())
      notice = std::move(error);
  }
  if (droppedNodePosition)
    addPendingNode(workspace, droppedNodePosition->x, droppedNodePosition->y,
                   notice);
}

void EditorTerrainGraphPanel::draw(EditorWorkspace &workspace,
                                   std::string &notice) {
  if (!open_)
    return;
  workspace.syncTerrainAuthoring();
  bind(workspace);
  auto &authoring = workspace.terrainAuthoring();
  if (entityId_.empty()) {
    ImGui::TextWrapped(
        "Select a Terrain3D entity in the 3D scene to edit its graph.");
    return;
  }
  if (!pendingNodeType_.empty() && !authoring.busy() && !authoring.stroking())
    addPendingNode(workspace, 120.0F, 120.0F, notice);
  auto &draft = authoring.draft();
  ImGui::Text("Terrain graph  |  %s", entityId_.c_str());
  if (authoring.hasDraftChanges()) {
    nextToolbarItem(ImGui::CalcTextSize("Unapplied draft").x);
    ImGui::TextColored({1.0F, 0.75F, 0.35F, 1.0F}, "Unapplied draft");
  }
  const auto surface = authoring.surface();
  const auto field = surface ? surface->heightField() : nullptr;
  if (field && field->graphArtifacts) {
    ImGui::PushTextWrapPos(0);
    for (const auto &warning : field->graphArtifacts->warnings)
      ImGui::TextColored({1.0F, 0.75F, 0.35F, 1.0F}, "%s", warning.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::BeginDisabled(authoring.busy() || authoring.stroking());
  drawCommandButton(workspace, EditorCommand::Undo, notice,
                    document_.canUndo());
  nextToolbarItem(30);
  drawCommandButton(workspace, EditorCommand::Redo, notice,
                    document_.canRedo());
  graphToolbarGroupBreak();
  nextToolbarItem(30);
  const bool nodesSelected = !selectedNodes_.empty();
  drawCommandButton(workspace, EditorCommand::Copy, notice, nodesSelected);
  nextToolbarItem(30);
  drawCommandButton(workspace, EditorCommand::Cut, notice, nodesSelected);
  nextToolbarItem(30);
  drawCommandButton(workspace, EditorCommand::Paste, notice, true);
  graphToolbarGroupBreak();
  nextToolbarItem(30);
  drawCommandButton(workspace, EditorCommand::Duplicate, notice, nodesSelected);
  nextToolbarItem(30);
  drawCommandButton(workspace, EditorCommand::Delete, notice,
                    nodesSelected || !selectedLinks_.empty());
  nextToolbarItem(30);
  drawCommandButton(workspace, EditorCommand::SelectAll, notice,
                    !graphOf(draft).at("nodes").empty());
  graphToolbarGroupBreak();
  nextToolbarItem(30);
  if (editorIconButton("graph-starter", EditorIcon::Add,
                       "Create a starter graph. Existing draft replacement "
                       "requires confirmation.")) {
    const Json graph = graphOf(draft);
    if (!graph.is_object() ||
        (graph.contains("nodes") && graph["nodes"].is_array() &&
         graph["nodes"].empty())) {
      std::string error;
      if (!document_.replace(draft, runtime::defaultTerrainGraph(), error))
        notice = std::move(error);
    } else {
      ImGui::OpenPopup("Replace terrain graph?");
    }
  }
  if (ImGui::BeginPopupModal("Replace terrain graph?", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(
        "Replace the current draft graph with the starter graph? "
        "The applied terrain stays unchanged until Generate.");
    if (ImGui::Button("Replace draft graph")) {
      std::string error;
      if (!document_.replace(draft, runtime::defaultTerrainGraph(), error))
        notice = std::move(error);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  nextToolbarItem(30);
  if (editorIconButton(
          "graph-generate", EditorIcon::Generate,
          "Generate the terrain from this draft graph and recipe settings.")) {
    if (authoring.needsResizeDecision()) {
      ImGui::OpenPopup("Graph terrain grid changed");
    } else {
      std::string error;
      if (!authoring.generate(std::nullopt, error))
        notice = std::move(error);
    }
  }
  nextToolbarItem(30);
  if (editorIconButton("graph-discard", EditorIcon::Discard,
                       "Discard unapplied graph and recipe changes.", false,
                       authoring.hasDraftChanges())) {
    authoring.discardDraft();
    document_.clearHistory();
  }
  if (ImGui::BeginPopupModal("Graph terrain grid changed", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(
        "The terrain grid changed. Keep compatible edits or clear "
        "regions, edits and exclusions for this generation?");
    if (ImGui::Button("Keep edits")) {
      std::string error;
      if (authoring.generate(EditorTerrainResize::Keep, error)) {
        ImGui::CloseCurrentPopup();
      } else {
        notice = std::move(error);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear edits")) {
      std::string error;
      if (authoring.generate(EditorTerrainResize::Clear, error)) {
        ImGui::CloseCurrentPopup();
      } else {
        notice = std::move(error);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::EndDisabled();
  if (authoring.busy()) {
    nextToolbarItem(120);
    ImGui::ProgressBar(authoring.progress(), {120.0F, 0.0F});
    nextToolbarItem(30);
    if (editorIconButton(
            "graph-cancel-generation", EditorIcon::Stop,
            "Cancel terrain generation; keep the previously applied terrain."))
      authoring.cancel();
  }
  ImGui::BeginDisabled(authoring.busy() || authoring.stroking());
  ImGui::Separator();
  if (!selectedNodes_.empty()) {
    ImGui::Text("%zu node%s selected", selectedNodes_.size(),
                selectedNodes_.size() == 1 ? "" : "s");
    const Json graph = graphOf(draft);
    const auto nodes =
        graph.is_object() ? graph.value("nodes", Json::array()) : Json::array();
    const auto selected =
        std::find_if(nodes.begin(), nodes.end(), [&](const Json &node) {
          return node.is_object() && node.value("id", "") == selectedNode_;
        });
    if (selected != nodes.end()) {
      if (selectedNodes_.size() == 1 &&
          selected->value("type", "") == "output") {
        ImGui::SameLine();
        if (editorIconButton(
                "graph-use-output", EditorIcon::Terrain,
                "Use the selected Terrain Output node as the graph output.")) {
          std::string error;
          if (!document_.setOutput(draft, selectedNode_, error))
            notice = std::move(error);
        }
      }
    } else {
      selectNodes({});
    }
  }
  if (!selectedLink_.empty()) {
    const Json graph = graphOf(draft);
    const auto links =
        graph.is_object() ? graph.value("links", Json::array()) : Json::array();
    const bool exists =
        std::any_of(links.begin(), links.end(), [&](const Json &link) {
          return link.is_object() && link.value("id", "") == selectedLink_;
        });
    if (exists) {
      ImGui::Text("%zu link%s selected", selectedLinks_.size(),
                  selectedLinks_.size() == 1 ? "" : "s");
    } else {
      selectedLink_.clear();
    }
  }
  if (draft.contains("graph") && !draft["graph"].is_null())
    drawDiagnostics(graphOf(draft));
  else
    ImGui::TextDisabled("Conventional landform generation");
  nextToolbarItem(30);
  if (editorIconButton("graph-settings", EditorIcon::Settings,
                       "Show or hide recipe settings over the graph.",
                       showSettings_))
    showSettings_ = !showSettings_;
  nextToolbarItem(30);
  if (editorIconButton("graph-modules", EditorIcon::Modules,
                       "Show or hide the local terrain module palette.",
                       showLocalPalette_))
    showLocalPalette_ = !showLocalPalette_;
  ImGui::EndDisabled();
  nextToolbarItem(30);
  const auto frameTooltip = commandTooltip(EditorCommand::FrameSelection);
  if (editorIconButton("graph-frame-selection", EditorIcon::Frame,
                       frameTooltip.c_str())) {
    std::string error;
    if (!executeCommand(workspace, EditorCommand::FrameSelection, error))
      notice = std::move(error);
  }
  nextToolbarItem(ImGui::CalcTextSize("100%").x);
  ImGui::Text("%.0f%%", canvasView_.zoom() * 100.0F);
  ImGui::SameLine();
  if (editorIconButton("graph-reset-zoom", EditorIcon::ZoomReset,
                       "Reset graph zoom to 100%. Wheel zooms at the cursor; "
                       "middle-drag pans."))
    resetZoomRequested_ = true;
  ImGui::BeginDisabled(authoring.busy() || authoring.stroking());
  if (showLocalPalette_) {
    drawPalette(workspace, notice);
    ImGui::SameLine();
  }
  const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  ImGui::BeginChild(
      "Terrain graph canvas", {0.0F, 0.0F}, ImGuiChildFlags_Borders,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  drawCanvas(workspace, notice);
  ImGui::EndChild();
  ImGui::EndDisabled();
  if (showSettings_)
    drawSettingsOverlay(workspace, canvasOrigin, canvasSize, notice);
}

void EditorTerrainGraphPanel::drawSettingsOverlay(EditorWorkspace &workspace,
                                                  ImVec2 origin, ImVec2 size,
                                                  std::string &notice) {
  constexpr float inset = 12.0F;
  if (size.x <= inset * 2 || size.y <= inset * 2)
    return;
  const auto end = ImGui::GetCursorScreenPos();
  const float width = std::min(settingsWidth_, size.x - inset * 2);
  const float collapsedHeight =
      ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;
  const float height = settingsExpanded_
                           ? std::max(collapsedHeight, size.y - inset * 2)
                           : collapsedHeight;
  ImGui::SetCursorScreenPos({origin.x + inset, origin.y + inset});
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
  ImGui::BeginChild("Terrain recipe settings overlay", {width, height},
                    ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX,
                    ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
  settingsWidth_ = ImGui::GetWindowWidth();
  ImGui::SetNextItemOpen(settingsExpanded_, ImGuiCond_Always);
  settingsExpanded_ = ImGui::CollapsingHeader("Recipe settings");
  if (settingsExpanded_) {
    if (ImGui::BeginChild("Recipe settings content", {0.0F, 0.0F})) {
      const Json emptyNodes = Json::array();
      const auto graph = workspace.terrainAuthoring().draft().find("graph");
      const auto &nodes = graph != workspace.terrainAuthoring().draft().end() &&
                                  graph->is_object() && graph->contains("nodes")
                              ? graph->at("nodes")
                              : emptyNodes;
      const auto target =
          std::ranges::find(nodes, settingsNode_, [](const auto &node) {
            return node.value("id", std::string{});
          });
      if (!settingsNode_.empty() && target != nodes.end()) {
        if (ImGui::Button("All landscape settings"))
          settingsNode_.clear();
        if (!settingsNode_.empty()) {
          ImGui::TextWrapped("Node: %s", settingsNode_.c_str());
          drawTerrainNodeSettings(
              workspace, target->at("type").get<std::string>(), notice);
        } else
          drawTerrainGraphSettings(workspace, notice);
      } else
        drawTerrainGraphSettings(workspace, notice);
    }
    ImGui::EndChild();
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::SetCursorScreenPos({end.x, end.y - ImGui::GetStyle().ItemSpacing.y});
  ImGui::Dummy({0.0F, 0.0F});
}

} // namespace demi::editor
