#include "editor/EditorConflictPanel.h"

#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace demi::editor {
namespace {

std::filesystem::path suggestedCopyPath(const std::filesystem::path &source) {
  return source.parent_path() /
         (source.stem().string() + ".copy" + source.extension().string());
}

} // namespace

void EditorConflictPanel::draw(EditorWorkspace &workspace,
                               std::string &notice) {
  const bool isOpen = workspace.sceneDocument().hasExternalConflict();
  if (isOpen && !wasOpen_) {
    const std::string suggested =
        suggestedCopyPath(workspace.sceneDocument().path()).string();
    const std::size_t count = std::min(suggested.size(), copyPath_.size() - 1);
    std::fill(copyPath_.begin(), copyPath_.end(), '\0');
    std::copy_n(suggested.data(), count, copyPath_.data());
    ImGui::OpenPopup("Scene changed on disk");
  }
  wasOpen_ = isOpen;

  if (!ImGui::BeginPopupModal("Scene changed on disk", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextWrapped("The scene on disk changed after it was opened. The "
                     "editor will not overwrite that external version.");
  ImGui::Spacing();

  const auto resolve = [&](const ExternalChangeDecision decision,
                           const std::filesystem::path &copyPath,
                           const char *success) {
    std::string error;
    if (workspace.resolveExternalChange(decision, copyPath, error)) {
      notice = success;
      wasOpen_ = false;
      ImGui::CloseCurrentPopup();
    } else {
      notice = error;
    }
  };

  if (ImGui::Button("Reload from disk"))
    resolve(ExternalChangeDecision::ReloadFromDisk, {},
            "Reloaded the external scene");
  ImGui::SameLine();
  if (ImGui::Button("Keep editing"))
    resolve(ExternalChangeDecision::KeepEditing, {},
            "Kept the in-memory scene; Save will ask again");
  ImGui::SameLine();
  if (ImGui::Button("Cancel"))
    resolve(ExternalChangeDecision::Cancel, {}, "Save cancelled");

  ImGui::Separator();
  ImGui::TextUnformatted("Save the in-memory scene to a new path:");
  ImGui::SetNextItemWidth(520.0F);
  ImGui::InputText("##scene-copy-path", copyPath_.data(), copyPath_.size());
  if (ImGui::Button("Save Copy"))
    resolve(ExternalChangeDecision::SaveCopy, copyPath_.data(),
            "Saved a copy; the external scene was not changed");

  ImGui::EndPopup();
}

} // namespace demi::editor
