#include "editor/EditorAssetDialogs.h"

#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>

namespace demi::editor {

bool EditorAssetDialogs::openEditGroup(const std::filesystem::path &path,
                                       std::string &error) {
  EditorAssetGroupDocument document;
  if (!document.open(path, error))
    return false;
  groupDocument_ = std::move(document);
  showEditGroup_ = true;
  return true;
}

void EditorAssetDialogs::draw(EditorWorkspace &workspace, std::string &notice) {
  if (showImport_)
    ImGui::OpenPopup("Import Asset");
  showImport_ = false;
  ImGui::SetNextWindowSize({520.0F, 285.0F}, ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Import Asset", nullptr,
                             ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::TextWrapped("The source is copied into the project and registered "
                       "through the engine importer.");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("Source", "/path/to/source.png",
                             importSource_.data(), importSource_.size());
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("Stable ID", "asset://textures/source",
                             importId_.data(), importId_.size());
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("Type", "Optional; importer chooses when empty",
                             importType_.data(), importType_.size());
    const bool canImport = importSource_[0] != '\0' && importId_[0] != '\0';
    ImGui::BeginDisabled(!canImport);
    if (ImGui::Button("Import", {100.0F, 30.0F})) {
      std::string error;
      if (workspace.importAsset({.source = importSource_.data(),
                                 .id = importId_.data(),
                                 .type = importType_.data()},
                                error)) {
        notice = "Asset imported";
        importSource_.fill('\0');
        importId_.fill('\0');
        importType_.fill('\0');
        ImGui::CloseCurrentPopup();
      } else {
        notice = error;
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {90.0F, 30.0F}))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (showCreateGroup_)
    ImGui::OpenPopup("Create Asset Group");
  showCreateGroup_ = false;
  ImGui::SetNextWindowSize({560.0F, 520.0F}, ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Create Asset Group", nullptr,
                             ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("Stable ID", "asset-group://chapter",
                             groupId_.data(), groupId_.size());
    ImGui::TextDisabled("Roots");
    ImGui::BeginChild("group-roots", {0.0F, 360.0F}, ImGuiChildFlags_Borders);
    const auto rootChoice = [&](const std::string &id) {
      bool selected = groupRoots_.contains(id);
      if (ImGui::Checkbox(id.c_str(), &selected)) {
        if (selected)
          groupRoots_.insert(id);
        else
          groupRoots_.erase(id);
      }
    };
    for (const EditorAssetRecord &asset : workspace.assetIndex().assets())
      rootChoice(asset.manifest.id);
    for (const runtime::SceneEntry &scene :
         workspace.projectDocument().scenes())
      rootChoice(scene.id);
    ImGui::EndChild();
    const bool canCreate = groupId_[0] != '\0' && !groupRoots_.empty();
    ImGui::BeginDisabled(!canCreate);
    if (ImGui::Button("Create", {100.0F, 30.0F})) {
      std::string error;
      const std::vector<std::string> roots(groupRoots_.begin(),
                                           groupRoots_.end());
      if (workspace.createAssetGroup(groupId_.data(), roots, error)) {
        notice = "Asset group created";
        groupId_.fill('\0');
        groupRoots_.clear();
        ImGui::CloseCurrentPopup();
      } else {
        notice = error;
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {90.0F, 30.0F}))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (showEditGroup_)
    ImGui::OpenPopup("Edit Asset Group");
  showEditGroup_ = false;
  ImGui::SetNextWindowSize({560.0F, 520.0F}, ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Edit Asset Group", nullptr,
                             ImGuiWindowFlags_NoSavedSettings)) {
    if (!groupDocument_) {
      ImGui::TextDisabled("The asset-group document is unavailable.");
    } else {
      ImGui::TextUnformatted(groupDocument_->id().c_str());
      if (groupDocument_->isDirty()) {
        ImGui::SameLine();
        ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "Modified");
      }
      ImGui::BeginChild("edit-group-roots", {0.0F, 390.0F},
                        ImGuiChildFlags_Borders);
      const std::vector<std::string> current = groupDocument_->roots();
      const auto rootChoice = [&](const std::string &id) {
        bool selected = std::ranges::find(current, id) != current.end();
        if (!ImGui::Checkbox(id.c_str(), &selected))
          return;
        std::vector<std::string> replacement = current;
        if (selected)
          replacement.push_back(id);
        else
          std::erase(replacement, id);
        std::string error;
        if (!groupDocument_->setRoots(std::move(replacement), error))
          notice = error;
      };
      for (const EditorAssetRecord &asset : workspace.assetIndex().assets())
        rootChoice(asset.manifest.id);
      for (const runtime::SceneEntry &scene :
           workspace.projectDocument().scenes())
        rootChoice(scene.id);
      ImGui::EndChild();
      ImGui::BeginDisabled(!groupDocument_->canUndo());
      if (ImGui::Button("Undo")) {
        std::string error;
        notice = groupDocument_->undo(error) ? "Undid asset-group edit" : error;
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(!groupDocument_->canRedo());
      if (ImGui::Button("Redo")) {
        std::string error;
        notice = groupDocument_->redo(error) ? "Redid asset-group edit" : error;
      }
      ImGui::EndDisabled();
      ImGui::SameLine(355.0F);
      ImGui::BeginDisabled(!groupDocument_->isDirty());
      if (ImGui::Button("Save")) {
        std::string error;
        if (groupDocument_->save(error)) {
          workspace.refreshAssetMetadata();
          notice = "Asset group saved";
        } else {
          notice = error;
        }
      }
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      if (!groupDocument_ || !groupDocument_->isDirty()) {
        groupDocument_.reset();
        ImGui::CloseCurrentPopup();
      } else {
        notice = "Save or undo asset-group changes before closing.";
      }
    }
    ImGui::EndPopup();
  }
}

} // namespace demi::editor
