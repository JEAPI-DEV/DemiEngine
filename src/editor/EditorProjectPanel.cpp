#include "editor/EditorProjectPanel.h"

#include "editor/EditorWorkspace.h"

#include "cli/project/ProjectTemplates.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>

namespace demi::editor {
namespace {

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
    ImGui::SetNextWindowSize({660.0F, 720.0F}, ImGuiCond_Appearing);
    if (ImGui::Begin("Project Settings", nullptr,
                     ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::TextUnformatted(workspace.project().project.name.c_str());
      if (workspace.projectDocument().isDirty()) {
        ImGui::SameLine();
        ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "Modified");
      }
      ImGui::Separator();
      ImGui::BeginChild("project-settings-content", {0.0F, -48.0F}, false);
      ImGui::TextUnformatted("Preloaded assets and groups");
      std::vector<std::string> preloads =
          workspace.projectDocument().preloadedAssets();
      std::optional<std::size_t> removePreload;
      for (std::size_t index = 0; index < preloads.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextUnformatted(preloads[index].c_str());
        ImGui::SameLine(500.0F);
        if (ImGui::SmallButton("Remove"))
          removePreload = index;
        ImGui::PopID();
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
      ImGui::Separator();
      ImGui::TextUnformatted("Scene membership");
      std::optional<std::string> removeScene;
      for (const runtime::SceneEntry &scene :
           workspace.projectDocument().scenes()) {
        ImGui::PushID(scene.id.c_str());
        ImGui::Text("%s", scene.id.c_str());
        ImGui::SameLine(315.0F);
        ImGui::TextDisabled("%s", scene.path.string().c_str());
        ImGui::SameLine(540.0F);
        const bool isMain = scene.id == workspace.project().project.mainScene;
        if (isMain)
          ImGui::TextDisabled("Main");
        else if (ImGui::SmallButton("Remove"))
          removeScene = scene.id;
        ImGui::PopID();
      }
      if (removeScene) {
        std::string error;
        notice = workspace.removeProjectScene(*removeScene, error)
                     ? "Project scene removed"
                     : error;
      }
      ImGui::SetNextItemWidth(250.0F);
      ImGui::InputTextWithHint("##scene-id", "scene://game/level",
                               sceneId_.data(), sceneId_.size());
      ImGui::SameLine();
      ImGui::SetNextItemWidth(250.0F);
      ImGui::InputTextWithHint("##scene-path", "scenes/level.scene.json",
                               scenePath_.data(), scenePath_.size());
      ImGui::SameLine();
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

      ImGui::Spacing();
      ImGui::Separator();
      if (ImGui::CollapsingHeader("Input Actions",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        nlohmann::json actions = workspace.projectDocument().inputActions();
        std::optional<std::string> removeAction;
        for (const auto &[name, action] : actions.items()) {
          ImGui::PushID(name.c_str());
          ImGui::Text("%s", name.c_str());
          ImGui::SameLine(220.0F);
          ImGui::TextDisabled("%s / %s", action.value("type", "").c_str(),
                              action.value("context", "").c_str());
          ImGui::SameLine(565.0F);
          if (ImGui::SmallButton("Remove"))
            removeAction = name;
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
              ImGui::TextDisabled("Binding %zu", index + 1);
              ImGui::SameLine(105.0F);
              ImGui::SetNextItemWidth(360.0F);
              const bool submitted = ImGui::InputTextWithHint(
                  "##input", "key:space, mouse:left, gamepad:south...",
                  editor.value.data(), editor.value.size(),
                  ImGuiInputTextFlags_EnterReturnsTrue);
              ImGui::SameLine();
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
        ImGui::SetNextItemWidth(145.0F);
        ImGui::InputTextWithHint("##action-name", "action_name",
                                 actionName_.data(), actionName_.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105.0F);
        if (ImGui::BeginCombo("##action-type", actionType_.c_str())) {
          for (const char *type : {"button", "axis1d", "vector2"})
            if (ImGui::Selectable(type, actionType_ == type))
              actionType_ = type;
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0F);
        ImGui::InputTextWithHint("##action-context", "gameplay",
                                 actionContext_.data(), actionContext_.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(145.0F);
        ImGui::InputTextWithHint("##action-binding", "key:space",
                                 actionBinding_.data(), actionBinding_.size());
        ImGui::SameLine();
        const bool canAddAction = actionName_[0] != '\0' &&
                                  actionContext_[0] != '\0' &&
                                  actionBinding_[0] != '\0';
        ImGui::BeginDisabled(!canAddAction);
        if (ImGui::Button("Add##action")) {
          if (actions.contains(actionName_.data())) {
            notice = "An input action with that name already exists.";
          } else {
            actions[actionName_.data()] = {
                {"type", actionType_},
                {"context", actionContext_.data()},
                {"bindings",
                 nlohmann::json::array({{{"input", actionBinding_.data()}}})}};
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
      }

      ImGui::EndChild();
      ImGui::Separator();
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
      ImGui::SameLine(400.0F);
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
    }
    ImGui::End();
  }

  if (showCreateProject_) {
    ImGui::SetNextWindowSize({520.0F, 310.0F}, ImGuiCond_Appearing);
    if (ImGui::Begin("Create Project", nullptr,
                     ImGuiWindowFlags_NoSavedSettings)) {
      Diagnostics catalogDiagnostics;
      cli::project::ProjectTemplateCatalog catalog(
          std::filesystem::path(DEMI_SOURCE_DIR) / "templates");
      const auto templates = catalog.discover(catalogDiagnostics);
      if (selectedTemplate_.empty() && !templates.empty())
        selectedTemplate_ = templates.front().id;
      ImGui::TextUnformatted("Create from an engine project template");
      ImGui::SetNextItemWidth(-1.0F);
      ImGui::InputTextWithHint(
          "##new-project-destination", "/absolute/path/to/project",
          projectDestination_.data(), projectDestination_.size());
      ImGui::SetNextItemWidth(-1.0F);
      ImGui::InputTextWithHint("##new-project-name", "Project name",
                               projectName_.data(), projectName_.size());
      if (ImGui::BeginCombo("Template", selectedTemplate_.c_str())) {
        for (const auto &item : templates)
          if (ImGui::Selectable(item.title.c_str(),
                                selectedTemplate_ == item.id))
            selectedTemplate_ = item.id;
        ImGui::EndCombo();
      }
      if (!catalogDiagnostics.empty())
        ImGui::TextColored({0.95F, 0.34F, 0.38F, 1.0F}, "%s",
                           catalogDiagnostics.front().message.c_str());
      ImGui::Spacing();
      const bool canCreate = projectDestination_[0] != '\0' &&
                             projectName_[0] != '\0' &&
                             !selectedTemplate_.empty();
      ImGui::BeginDisabled(!canCreate);
      if (ImGui::Button("Create Project", {150.0F, 30.0F})) {
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
      if (ImGui::Button("Cancel", {90.0F, 30.0F}))
        showCreateProject_ = false;
    }
    ImGui::End();
  }
}

} // namespace demi::editor
