#include "editor/EditorProjectPanel.h"

#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include "cli/project/ProjectTemplates.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>

namespace demi::editor {
namespace {

constexpr EditorDialogLayoutSpec ProjectSettingsLayout{
    .preferredEm = {72.0F, 50.0F},
    .minimumEm = {42.0F, 30.0F},
};
constexpr EditorDialogLayoutSpec CreateProjectLayout{
    .preferredEm = {46.0F, 26.0F},
    .minimumEm = {34.0F, 22.0F},
};

void reportDiagnostics(const Diagnostics &diagnostics, std::string &notice) {
  if (diagnostics.empty())
    return;
  notice = diagnostics.front().message;
}

std::string inputBindingValue(const nlohmann::json &binding) {
  return binding.is_object() ? binding.value("input", std::string{})
                             : std::string{};
}

void copyInput(std::array<char, 160> &destination,
               const std::string_view value) {
  destination.fill('\0');
  const std::size_t count = std::min(value.size(), destination.size() - 1);
  std::memcpy(destination.data(), value.data(), count);
}

} // namespace

void EditorProjectPanel::draw(EditorWorkspace &workspace, std::string &notice) {
  if (showSettings_) {
    prepareEditorDialog(ProjectSettingsLayout);
    if (ImGui::Begin("Project Settings")) {
      ImGui::TextUnformatted(workspace.project().project.name.c_str());
      if (workspace.projectDocument().isDirty()) {
        ImGui::SameLine();
        ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "Modified");
      }
      ImGui::Separator();
      const float footerHeight = ImGui::GetFrameHeightWithSpacing();
      ImGui::BeginChild("project-settings-content", {0.0F, -footerHeight},
                        ImGuiChildFlags_None,
                        ImGuiWindowFlags_AlwaysVerticalScrollbar);
      ImGui::SeparatorText("Preloaded assets and groups");
      std::vector<std::string> preloads =
          workspace.projectDocument().preloadedAssets();
      std::optional<std::size_t> removePreload;
      if (ImGui::BeginTable("preload-list", 2,
                            ImGuiTableFlags_SizingStretchProp |
                                ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
        for (std::size_t index = 0; index < preloads.size(); ++index) {
          ImGui::PushID(static_cast<int>(index));
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::AlignTextToFramePadding();
          ImGui::TextUnformatted(preloads[index].c_str());
          ImGui::TableNextColumn();
          if (ImGui::SmallButton("Remove"))
            removePreload = index;
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      if (removePreload) {
        preloads.erase(preloads.begin() +
                       static_cast<std::ptrdiff_t>(*removePreload));
        std::string error;
        notice = workspace.setPreloadedAssets(std::move(preloads), error)
                     ? "Project preload removed"
                     : error;
      }
      if (ImGui::BeginCombo("##add-preload", "+ Add preload")) {
        const auto add = [&](const std::string &id) {
          if (std::ranges::find(preloads, id) != preloads.end())
            return;
          if (ImGui::Selectable(id.c_str())) {
            auto replacement = preloads;
            replacement.push_back(id);
            std::string error;
            notice = workspace.setPreloadedAssets(std::move(replacement), error)
                         ? "Project preload added"
                         : error;
          }
        };
        for (const EditorAssetRecord &asset : workspace.assetIndex().assets())
          add(asset.manifest.id);
        for (const assets::AssetGroupDescriptor &group :
             workspace.assetIndex().groups())
          add(group.id);
        ImGui::EndCombo();
      }

      ImGui::Spacing();
      ImGui::SeparatorText("Scene membership");
      const std::vector<runtime::SceneEntry> projectScenes =
          workspace.projectDocument().scenes();
      const std::string mainScene =
          workspace.projectDocument().json().value("main_scene", "");
      if (ImGui::BeginTable("start-scene", 2,
                            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 9.0F);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Start scene");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("##start-scene", mainScene.c_str())) {
          for (const runtime::SceneEntry &scene : projectScenes) {
            const bool isMain = scene.id == mainScene;
            if (ImGui::Selectable(scene.id.c_str(), isMain) && !isMain) {
              std::string error;
              notice = workspace.setProjectMainScene(scene.id, error)
                           ? "Project start scene updated"
                           : error;
            }
            if (isMain)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        ImGui::EndTable();
      }

      std::optional<std::string> removeScene;
      if (ImGui::BeginTable("scene-list", 3,
                            ImGuiTableFlags_SizingStretchProp |
                                ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch,
                                0.9F);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch,
                                1.1F);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
        for (const runtime::SceneEntry &scene : projectScenes) {
          ImGui::PushID(scene.id.c_str());
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::AlignTextToFramePadding();
          ImGui::TextUnformatted(scene.id.c_str());
          ImGui::TableNextColumn();
          ImGui::AlignTextToFramePadding();
          ImGui::TextDisabled("%s", scene.path.string().c_str());
          ImGui::TableNextColumn();
          const bool isMain = scene.id == mainScene;
          if (isMain)
            ImGui::TextDisabled("Main");
          else if (ImGui::SmallButton("Remove"))
            removeScene = scene.id;
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      if (removeScene) {
        std::string error;
        notice = workspace.removeProjectScene(*removeScene, error)
                     ? "Project scene removed"
                     : error;
      }
      if (ImGui::BeginTable("add-scene", 3,
                            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch, 0.9F);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch,
                                1.1F);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##scene-id", "scene://game/level",
                                 sceneId_.data(), sceneId_.size());
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##scene-path", "scenes/level.scene.json",
                                 scenePath_.data(), scenePath_.size());
        ImGui::TableNextColumn();
        if (ImGui::Button("Add")) {
          std::string error;
          if (workspace.addProjectScene(sceneId_.data(), scenePath_.data(),
                                        error)) {
            notice = "Project scene added";
            sceneId_.fill('\0');
            scenePath_.fill('\0');
          } else {
            notice = error;
          }
        }
        ImGui::EndTable();
      }

      ImGui::Spacing();
      if (ImGui::CollapsingHeader("Input Actions",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        // Presets expand into actions at runtime (wasd_arrows, confirm,
        // gamepad_confirm, move_3d). Explicit actions below override presets.
        const std::vector<std::string> activePresets =
            workspace.projectDocument().inputPresets();
        ImGui::TextDisabled("Presets");
        const int presetColumns =
            std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x /
                                        (ImGui::GetFontSize() * 14.0F)),
                       1, 4);
        if (ImGui::BeginTable("input-presets", presetColumns,
                              ImGuiTableFlags_SizingStretchSame)) {
          for (const char *preset :
               {"wasd_arrows", "confirm", "gamepad_confirm", "move_3d"}) {
            ImGui::TableNextColumn();
            bool enabled =
                std::ranges::find(activePresets, preset) != activePresets.end();
            if (ImGui::Checkbox(preset, &enabled)) {
              std::vector<std::string> updated = activePresets;
              if (enabled)
                updated.push_back(preset);
              else
                std::erase(updated, preset);
              std::string error;
              notice =
                  workspace.setProjectInputPresets(std::move(updated), error)
                      ? "Input presets updated"
                      : error;
            }
          }
          ImGui::EndTable();
        }
        nlohmann::json actions = workspace.projectDocument().inputActions();
        std::optional<std::string> removeAction;
        for (const auto &[name, action] : actions.items()) {
          ImGui::PushID(name.c_str());
          if (ImGui::BeginTable("action-summary", 3,
                                ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch,
                                    1.0F);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch,
                                    1.0F);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s / %s", action.value("type", "").c_str(),
                                action.value("context", "gameplay").c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Remove"))
              removeAction = name;
            ImGui::EndTable();
          }
          const auto bindings = action.find("bindings");
          if (bindings != action.end() && bindings->is_array()) {
            for (std::size_t index = 0; index < bindings->size(); ++index) {
              ImGui::PushID(static_cast<int>(index));
              const std::string input = inputBindingValue((*bindings)[index]);
              const std::string editorId = name + '#' + std::to_string(index);
              InputBindingEditor &editor = inputBindingEditors_[editorId];
              const bool hasPendingEdit =
                  std::string(editor.value.data()) != editor.source;
              if (editor.source != input) {
                if (!hasPendingEdit)
                  copyInput(editor.value, input);
                editor.source = input;
              }
              if (ImGui::BeginTable("binding-row", 3,
                                    ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Label",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        ImGui::GetFontSize() * 7.0F);
                ImGui::TableSetupColumn("Binding",
                                        ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Action",
                                        ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Binding %zu", index + 1);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0F);
                const bool submitted = ImGui::InputTextWithHint(
                    "##input", "key:space, mouse:left, gamepad:south...",
                    editor.value.data(), editor.value.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::TableNextColumn();
                const std::string replacement = editor.value.data();
                const bool changed = replacement != input;
                ImGui::BeginDisabled(!changed || replacement.empty());
                if ((ImGui::SmallButton("Apply") || submitted) && changed &&
                    !replacement.empty()) {
                  std::string error;
                  if (workspace.setProjectInputBinding(name, index, replacement,
                                                       error)) {
                    editor.source = replacement;
                    notice = "Input binding updated";
                  } else {
                    notice = error;
                  }
                }
                ImGui::EndDisabled();
                ImGui::EndTable();
              }
              ImGui::PopID();
            }
          }
          ImGui::PopID();
          ImGui::Spacing();
        }
        if (removeAction) {
          actions.erase(*removeAction);
          std::string error;
          notice = workspace.setProjectInputActions(std::move(actions), error)
                       ? "Input action removed"
                       : error;
        }
        ImGui::TextDisabled("New action");
        if (ImGui::BeginTable("add-action", 5,
                              ImGuiTableFlags_SizingStretchProp)) {
          ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch,
                                  1.0F);
          ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed,
                                  ImGui::GetFontSize() * 8.0F);
          ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch,
                                  0.8F);
          ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch,
                                  1.2F);
          ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1.0F);
          ImGui::InputTextWithHint("##action-name", "action_name",
                                   actionName_.data(), actionName_.size());
          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1.0F);
          if (ImGui::BeginCombo("##action-type", actionType_.c_str())) {
            for (const char *type : {"button", "axis1d", "vector2"})
              if (ImGui::Selectable(type, actionType_ == type))
                actionType_ = type;
            ImGui::EndCombo();
          }
          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1.0F);
          ImGui::InputTextWithHint("##action-context", "gameplay",
                                   actionContext_.data(),
                                   actionContext_.size());
          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1.0F);
          ImGui::InputTextWithHint("##action-binding", "key:space",
                                   actionBinding_.data(),
                                   actionBinding_.size());
          ImGui::TableNextColumn();
          const bool canAddAction =
              actionName_[0] != '\0' && actionBinding_[0] != '\0';
          ImGui::BeginDisabled(!canAddAction);
          if (ImGui::Button("Add##action")) {
            if (actions.contains(actionName_.data())) {
              notice = "An input action with that name already exists.";
            } else {
              actions[actionName_.data()] = {
                  {"type", actionType_},
                  {"bindings", nlohmann::json::array(
                                   {{{"input", actionBinding_.data()}}})}};
              if (actionContext_[0] != '\0')
                actions[actionName_.data()]["context"] = actionContext_.data();
              std::string error;
              if (workspace.setProjectInputActions(std::move(actions), error)) {
                notice = "Input action added";
                actionName_.fill('\0');
                actionContext_.fill('\0');
                actionBinding_.fill('\0');
              } else {
                notice = error;
              }
            }
          }
          ImGui::EndDisabled();
          ImGui::EndTable();
        }
      }

      ImGui::EndChild();
      if (ImGui::BeginTable("project-settings-footer", 2,
                            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("History", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!workspace.projectDocument().canUndo());
        if (ImGui::Button("Undo")) {
          std::string error;
          notice = workspace.projectUndo(error) ? "Undid project edit" : error;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!workspace.projectDocument().canRedo());
        if (ImGui::Button("Redo")) {
          std::string error;
          notice = workspace.projectRedo(error) ? "Redid project edit" : error;
        }
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!workspace.projectDocument().isDirty());
        if (ImGui::Button("Save Project")) {
          std::string error;
          notice = workspace.saveProject(error) ? "Project saved" : error;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
          if (workspace.projectDocument().isDirty())
            notice = "Save or undo project changes before closing.";
          else
            showSettings_ = false;
        }
        ImGui::EndTable();
      }
    }
    ImGui::End();
  }

  if (showCreateProject_) {
    prepareEditorDialog(CreateProjectLayout);
    if (ImGui::Begin("Create Project")) {
      ImGui::BeginChild("create-project-content", {}, ImGuiChildFlags_None,
                        ImGuiWindowFlags_AlwaysVerticalScrollbar);
      Diagnostics catalogDiagnostics;
      cli::project::ProjectTemplateCatalog catalog(
          std::filesystem::path(DEMI_SOURCE_DIR) / "templates");
      const auto templates = catalog.discover(catalogDiagnostics);
      if (selectedTemplate_.empty() && !templates.empty())
        selectedTemplate_ = templates.front().id;
      ImGui::TextUnformatted("Create from an engine project template");
      if (ImGui::BeginTable("create-project-form", 2,
                            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 9.0F);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Location");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint(
            "##new-project-destination", "/absolute/path/to/project",
            projectDestination_.data(), projectDestination_.size());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##new-project-name", "Project name",
                                 projectName_.data(), projectName_.size());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Template");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("##new-project-template",
                              selectedTemplate_.c_str())) {
          for (const auto &item : templates)
            if (ImGui::Selectable(item.title.c_str(),
                                  selectedTemplate_ == item.id))
              selectedTemplate_ = item.id;
          ImGui::EndCombo();
        }
        ImGui::EndTable();
      }
      if (!catalogDiagnostics.empty())
        ImGui::TextColored({0.95F, 0.34F, 0.38F, 1.0F}, "%s",
                           catalogDiagnostics.front().message.c_str());
      ImGui::Spacing();
      const bool canCreate = projectDestination_[0] != '\0' &&
                             projectName_[0] != '\0' &&
                             !selectedTemplate_.empty();
      ImGui::BeginDisabled(!canCreate);
      if (ImGui::Button("Create Project")) {
        auto projectTemplate =
            catalog.find(selectedTemplate_, catalogDiagnostics);
        if (projectTemplate) {
          const auto result = cli::project::ProjectScaffolder{}.create(
              {.projectTemplate = *projectTemplate,
               .destination = projectDestination_.data(),
               .projectName = projectName_.data()});
          if (result.committed) {
            notice = "Created project at " +
                     std::filesystem::path(projectDestination_.data()).string();
            showCreateProject_ = false;
          } else {
            reportDiagnostics(result.diagnostics, notice);
          }
        } else {
          reportDiagnostics(catalogDiagnostics, notice);
        }
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
        showCreateProject_ = false;
      ImGui::EndChild();
    }
    ImGui::End();
  }
}

} // namespace demi::editor
