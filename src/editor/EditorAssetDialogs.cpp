#include "editor/EditorAssetDialogs.h"
#include "editor/EditorPanelStyle.h"

#include "editor/EditorAssetDrop.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>

namespace demi::editor {
namespace {

template <std::size_t Size>
void setBuffer(std::array<char, Size> &buffer, const std::string_view value) {
  buffer.fill('\0');
  std::copy_n(value.data(), std::min(value.size(), Size - 1), buffer.data());
}

std::string colliderIdForModel(const std::string_view modelId) {
  constexpr std::string_view Prefix = "asset://";
  std::filesystem::path relative(
      modelId.starts_with(Prefix) ? modelId.substr(Prefix.size()) : modelId);
  return "asset://colliders/" + relative.filename().string();
}

} // namespace

void EditorAssetDialogs::openNewSource(
    const EditorSourceKind kind, std::string selectedEntity,
    std::filesystem::path destinationDirectory,
    const std::string_view suggestedName) {
  sourceKind_ = kind;
  sourceSelection_ = std::move(selectedEntity);
  sourceDirectory_ = std::move(destinationDirectory);
  setBuffer(sourceName_, suggestedName);
  sourceError_.clear();
  showNewSource_ = true;
  focusSourceName_ = true;
}

void EditorAssetDialogs::openNewFolder(std::filesystem::path relativeParent) {
  folderParent_ = std::move(relativeParent);
  folderName_.fill('\0');
  folderError_.clear();
  showNewFolder_ = true;
  focusFolderName_ = true;
}

void EditorAssetDialogs::queueImport(std::filesystem::path source) {
  droppedSources_.push_back(std::move(source));
}

void EditorAssetDialogs::openGenerateCollider(
    std::filesystem::path modelManifest, const std::string_view modelId) {
  colliderModelManifest_ = std::move(modelManifest);
  setBuffer(colliderId_, colliderIdForModel(modelId));
  colliderBody_ = "static";
  colliderDetail_ = 1.0F;
  colliderRecommendation_.reset();
  colliderRecommendationBody_.clear();
  colliderRecommendationError_.clear();
  colliderReplacementHash_.reset();
  colliderReplacementId_.clear();
  showGenerateCollider_ = true;
}

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
  if (showNewSource_) {
    prepareEditorDialog({.preferredEm = {48, 22}, .minimumEm = {30, 16}});
    if (ImGui::Begin("New document", &showNewSource_,
                     ImGuiWindowFlags_NoDocking)) {
      ImGui::TextWrapped("Choose a name for the new document.");
      if (sourceKind_ == EditorSourceKind::PrefabFromSelection)
        ImGui::TextWrapped("Copy hierarchy '%s' into a reusable prefab. The "
                           "original stays unchanged.",
                           sourceSelection_.c_str());
      if (!sourceDirectory_.empty())
        ImGui::TextDisabled("Create in: %s",
                            sourceDirectory_.generic_string().c_str());
      ImGui::TextDisabled(
          "No extension needed. Use chapter/level_01 for subfolders.");
      ImGui::Spacing();
      ImGui::TextUnformatted("Name");
      ImGui::SetNextItemWidth(-1.0F);
      if (focusSourceName_) {
        ImGui::SetKeyboardFocusHere();
        focusSourceName_ = false;
      }
      const bool entered = ImGui::InputText(
          "##source-name", sourceName_.data(), sourceName_.size(),
          ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Spacing();
      ImGui::BeginDisabled(sourceName_[0] == '\0');
      const bool create =
          ImGui::Button(sourceKind_ == EditorSourceKind::PrefabFromSelection
                            ? "Create prefab copy"
                            : "Create and open");
      ImGui::EndDisabled();
      if ((create || entered) && sourceName_[0] != '\0') {
        std::filesystem::path created;
        std::string error;
        if (createEditorSource(workspace, sourceKind_, sourceName_.data(),
                               created, error, sourceSelection_,
                               sourceDirectory_)) {
          createdSource_ = created;
          opensCreatedSource_ =
              sourceKind_ != EditorSourceKind::PrefabFromSelection;
          showNewSource_ = false;
          notice = "Created " + created.filename().string();
        } else {
          sourceError_ = error;
          notice = error;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
        showNewSource_ = false;
      if (!sourceError_.empty())
        ImGui::TextWrapped("%s", sourceError_.c_str());
    }
    ImGui::End();
  }
  if (showNewFolder_) {
    prepareEditorDialog({.preferredEm = {36, 18}, .minimumEm = {26, 13}});
    if (ImGui::Begin("New Folder", &showNewFolder_,
                     ImGuiWindowFlags_NoSavedSettings)) {
      const std::string parent = folderParent_.empty()
                                     ? "Project root"
                                     : folderParent_.generic_string();
      ImGui::TextWrapped("Create in: %s", parent.c_str());
      ImGui::SetNextItemWidth(-1.0F);
      if (focusFolderName_) {
        ImGui::SetKeyboardFocusHere();
        focusFolderName_ = false;
      }
      const bool entered = ImGui::InputTextWithHint(
          "##folder-name", "Folder name", folderName_.data(),
          folderName_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::BeginDisabled(folderName_[0] == '\0');
      const bool create = ImGui::Button("Create", {100.0F, 30.0F});
      ImGui::EndDisabled();
      if ((create || entered) && folderName_[0] != '\0') {
        if (workspace.createFolder(folderParent_, folderName_.data(),
                                   folderError_)) {
          createdFolder_ = folderParent_ / folderName_.data();
          notice = "Created folder: " + createdFolder_->generic_string();
          showNewFolder_ = false;
        } else {
          notice = folderError_;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {90.0F, 30.0F}) ||
          (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)))
        showNewFolder_ = false;
      if (!folderError_.empty())
        ImGui::TextWrapped("%s", folderError_.c_str());
    }
    ImGui::End();
  }
  if (!showImport_ && importSource_[0] == '\0' && !droppedSources_.empty()) {
    std::string error;
    auto suggestion = suggestAssetImport(droppedSources_.front(), error);
    droppedSources_.erase(droppedSources_.begin());
    if (!suggestion) {
      notice = std::move(error);
    } else {
      const std::string source = suggestion->source.string();
      std::copy_n(source.data(),
                  std::min(source.size(), importSource_.size() - 1),
                  importSource_.data());
      std::copy_n(suggestion->assetId.data(),
                  std::min(suggestion->assetId.size(), importId_.size() - 1),
                  importId_.data());
      showImport_ = true;
      notice = "Dropped asset ready to import";
    }
  }
  if (showImport_) {
    prepareEditorDialog({.preferredEm = {48, 28}, .minimumEm = {32, 18}});
    if (ImGui::Begin("Import Asset", &showImport_,
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
          showImport_ = false;
        } else {
          notice = error;
        }
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {90.0F, 30.0F})) {
        importSource_.fill('\0');
        importId_.fill('\0');
        importType_.fill('\0');
        showImport_ = false;
      }
    }
    ImGui::End();
  }

  if (showGenerateCollider_) {
    prepareEditorDialog({.preferredEm = {48, 36}, .minimumEm = {32, 24}});
    if (ImGui::Begin("Generate Collider Asset", &showGenerateCollider_,
                     ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::TextWrapped("Generate a collider from the selected model using "
                         "its authored import profile.");
      ImGui::TextDisabled("Model");
      ImGui::TextWrapped("%s", colliderModelManifest_.string().c_str());
      ImGui::SetNextItemWidth(-1.0F);
      if (ImGui::InputText("Stable ID", colliderId_.data(),
                           colliderId_.size())) {
        colliderReplacementHash_.reset();
        colliderReplacementId_.clear();
      }
      ImGui::SetNextItemWidth(-1.0F);
      if (ImGui::BeginCombo("Body intent", colliderBody_.c_str())) {
        for (const char *body : {"static", "dynamic", "trigger", "character"}) {
          if (ImGui::Selectable(body, colliderBody_ == body)) {
            colliderBody_ = body;
            colliderRecommendationBody_.clear();
            colliderReplacementHash_.reset();
          }
        }
        ImGui::EndCombo();
      }
      if (colliderRecommendationBody_ != colliderBody_) {
        colliderRecommendationBody_ = colliderBody_;
        colliderRecommendationError_.clear();
        colliderRecommendation_ =
            workspace.recommendCollider(colliderModelManifest_, colliderBody_,
                                        colliderRecommendationError_);
        if (colliderRecommendation_)
          colliderDetail_ = colliderRecommendation_->detail;
      }
      if (colliderRecommendation_) {
        ImGui::Text("Recommended shape: %s",
                    colliderRecommendation_->shape.c_str());
        ImGui::TextWrapped("%s", colliderRecommendation_->reason.c_str());
      } else if (!colliderRecommendationError_.empty()) {
        ImGui::TextWrapped("Recommendation unavailable: %s",
                           colliderRecommendationError_.c_str());
      }
      const bool canGenerate =
          colliderBody_ == "static" || colliderBody_ == "trigger";
      if (canGenerate) {
        ImGui::SliderFloat("Geometry detail", &colliderDetail_, 0.0F, 1.0F,
                           "%.2f");
        ImGui::TextDisabled(
            "0 uses transformed bounds; values above 0 retain source "
            "triangles.");
      } else {
        ImGui::TextWrapped(
            "Dynamic bodies and characters use the recommended explicit "
            "convex or capsule component; the generator does not create an "
            "unsafe mesh collider for them.");
      }
      const std::string currentId = colliderId_.data();
      const bool replacing =
          colliderReplacementHash_ && colliderReplacementId_ == currentId;
      if (replacing)
        ImGui::TextWrapped(
            "A generated collider already uses this ID. Replace only the "
            "version inspected when this warning appeared?");
      ImGui::BeginDisabled(!canGenerate || currentId.empty());
      if (ImGui::Button(replacing ? "Replace" : "Generate", {100.0F, 30.0F})) {
        std::string error;
        std::optional<std::string> existingHash;
        auto created = workspace.generateColliderAsset(
            {.modelManifestPath = colliderModelManifest_,
             .id = currentId,
             .detail = colliderDetail_,
             .body = colliderBody_,
             .replaceExisting = replacing,
             .expectedExistingManifestHash =
                 replacing ? colliderReplacementHash_ : std::nullopt},
            existingHash, error);
        if (created) {
          createdCollider_ = *created;
          notice = replacing ? "Collider asset replaced"
                             : "Collider asset generated";
          showGenerateCollider_ = false;
          colliderReplacementHash_.reset();
        } else {
          notice = error;
          colliderReplacementHash_ = std::move(existingHash);
          colliderReplacementId_ =
              colliderReplacementHash_ ? currentId : std::string{};
        }
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {90.0F, 30.0F})) {
        showGenerateCollider_ = false;
        colliderReplacementHash_.reset();
      }
    }
    ImGui::End();
  }

  if (showCreateGroup_) {
    prepareEditorDialog({.preferredEm = {48, 40}, .minimumEm = {32, 26}});
    if (ImGui::Begin("Create Asset Group", &showCreateGroup_,
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
          showCreateGroup_ = false;
        } else {
          notice = error;
        }
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {90.0F, 30.0F}))
        showCreateGroup_ = false;
    }
    ImGui::End();
  }

  if (showEditGroup_) {
    prepareEditorDialog({.preferredEm = {48, 40}, .minimumEm = {32, 26}});
    if (ImGui::Begin("Edit Asset Group", &showEditGroup_,
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
          notice =
              groupDocument_->undo(error) ? "Undid asset-group edit" : error;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!groupDocument_->canRedo());
        if (ImGui::Button("Redo")) {
          std::string error;
          notice =
              groupDocument_->redo(error) ? "Redid asset-group edit" : error;
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
          showEditGroup_ = false;
        } else {
          notice = "Save or undo asset-group changes before closing.";
        }
      }
    }
    ImGui::End();
  }
}

} // namespace demi::editor
