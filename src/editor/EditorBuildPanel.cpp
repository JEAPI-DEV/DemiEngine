#include "editor/EditorBuildPanel.h"

#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace demi::editor {
namespace {

std::filesystem::path editorRuntimeExecutable() {
  std::error_code error;
  const auto editor = std::filesystem::read_symlink("/proc/self/exe", error);
  return error
             ? std::filesystem::path(DEMI_SOURCE_DIR) / "build/linux-debug/demi"
             : editor.parent_path() / "demi";
}

build::ProjectOperationRequest
operationRequest(EditorWorkspace &workspace,
                 const build::ProjectOperation operation) {
  return {.operation = operation,
          .projectFile = workspace.projectPath(),
          .engineRoot = DEMI_SOURCE_DIR,
          .runtimeExecutable = editorRuntimeExecutable()};
}

bool saveBeforeOperation(EditorWorkspace &workspace, std::string &notice) {
  std::string error;
  if (workspace.sceneDocument().isDirty() && !workspace.save(error)) {
    notice = error;
    return false;
  }
  if (workspace.projectDocument().isDirty() && !workspace.saveProject(error)) {
    notice = error;
    return false;
  }
  return true;
}

} // namespace

void EditorBuildPanel::draw(EditorWorkspace &workspace, std::string &notice) {
  const EditorProjectOperationSnapshot operation = operations_.snapshot();
  if (!operation.running && operation.result &&
      operation.generation != handledOperation_) {
    handledOperation_ = operation.generation;
    if (operation.result->succeeded()) {
      notice =
          "Project operation complete: " + operation.result->artifact.string();
      workspace.refreshAssetMetadata();
    } else if (operation.result->stage ==
               build::ProjectOperationStage::Cancelled) {
      notice = "Project operation cancelled";
    } else {
      notice = operation.result->diagnostics.empty()
                   ? "Project operation failed"
                   : operation.result->diagnostics.front().message;
    }
  }
  if (!show_)
    return;
  ImGui::SetNextWindowSize({420.0F, 390.0F}, ImGuiCond_Appearing);
  if (!ImGui::Begin("Build Project", &show_,
                    ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return;
  }
  editorSectionTitle("Build");
  if (ImGui::SmallButton("Validate") &&
      saveBeforeOperation(workspace, notice)) {
    std::string error;
    notice = operations_.start(
                 operationRequest(workspace, build::ProjectOperation::Validate),
                 error)
                 ? "Project validation started"
                 : error;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Cook") && saveBeforeOperation(workspace, notice)) {
    std::string error;
    notice =
        operations_.start(
            operationRequest(workspace, build::ProjectOperation::CookLinux),
            error)
            ? "Linux cook started"
            : error;
  }
  ImGui::Separator();
  ImGui::TextDisabled("Targets");
  ImGui::Checkbox("Linux (64-bit)", &linuxTarget_);
  ImGui::Checkbox("Android (ARM64)", &androidTarget_);
  ImGui::Separator();
  ImGui::TextDisabled("Build Settings");
  ImGui::TextDisabled("Configuration");
  ImGui::SameLine();
  ImGui::TextUnformatted("Debug");
  if (operation.running || operation.result) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", operation.progress.message.c_str());
    ImGui::ProgressBar(operation.progress.fraction, {-1.0F, 0.0F});
  }
  ImGui::SetCursorPosY(
      std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 48.0F));
  if (operation.running) {
    if (ImGui::Button("Cancel", {-1.0F, 30.0F})) {
      operations_.cancel();
      notice = "Cancelling project operation";
    }
  } else {
    const bool hasTarget = linuxTarget_ || androidTarget_;
    ImGui::BeginDisabled(!hasTarget);
    if (ImGui::Button("Build Project", {-1.0F, 30.0F}) &&
        saveBeforeOperation(workspace, notice)) {
      std::vector<build::ProjectOperationRequest> requests;
      if (linuxTarget_)
        requests.push_back(
            operationRequest(workspace, build::ProjectOperation::PackageLinux));
      if (androidTarget_)
        requests.push_back(operationRequest(
            workspace, build::ProjectOperation::PackageAndroid));
      std::string error;
      notice = operations_.start(std::move(requests), error)
                   ? "Project build started"
                   : error;
    }
    ImGui::EndDisabled();
  }
  ImGui::End();
}

} // namespace demi::editor
