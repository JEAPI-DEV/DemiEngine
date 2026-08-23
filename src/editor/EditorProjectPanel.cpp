#include "editor/EditorProjectPanel.h"

#include "editor/EditorWorkspace.h"

#include "cli/project/ProjectTemplates.h"

#include <imgui.h>

#include <algorithm>

namespace demi::editor {
namespace {

void reportDiagnostics(const Diagnostics &diagnostics, std::string &notice) {
  if (diagnostics.empty())
    return;
  notice = diagnostics.front().message;
}

} // namespace

void EditorProjectPanel::draw(EditorWorkspace &workspace, std::string &notice) {
  if (showSettings_)
    ImGui::OpenPopup("Project Settings");
  showSettings_ = false;
  ImGui::SetNextWindowSize({620.0F, 540.0F}, ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Project Settings", nullptr,
                             ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::TextUnformatted(workspace.project().project.name.c_str());
    if (workspace.projectDocument().isDirty()) {
      ImGui::SameLine();
      ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "Modified");
    }
    ImGui::Separator();
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

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 50.0F);
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
    if (ImGui::Button("Close"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (showCreateProject_)
    ImGui::OpenPopup("Create Project");
  showCreateProject_ = false;
  ImGui::SetNextWindowSize({520.0F, 310.0F}, ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Create Project", nullptr,
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
        if (ImGui::Selectable(item.title.c_str(), selectedTemplate_ == item.id))
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
          ImGui::CloseCurrentPopup();
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
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

} // namespace demi::editor
