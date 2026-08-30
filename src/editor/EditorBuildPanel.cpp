#include "editor/EditorBuildPanel.h"

#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string_view>
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

template <std::size_t Size>
void copyText(std::array<char, Size> &destination, std::string_view value) {
  destination.fill('\0');
  const std::size_t count = std::min(value.size(), destination.size() - 1);
  std::memcpy(destination.data(), value.data(), count);
}

} // namespace

void EditorBuildPanel::syncBuildSettings(const EditorWorkspace &workspace) {
  const runtime::ProjectBuildSettings current =
      workspace.projectDocument().buildSettings();
  const std::string source =
      std::string(current.authored ? "authored:" : "default:") +
      runtime::projectBuildSettingsJson(current).dump();
  if (settings_.initialized && (settings_.dirty || settings_.source == source))
    return;

  copyText(settings_.applicationId,
           current.applicationId.empty()
               ? "com.example." + current.executableName
               : current.applicationId);
  copyText(settings_.displayName, current.displayName);
  copyText(settings_.executableName, current.executableName);
  copyText(settings_.versionName, current.versionName);
  copyText(settings_.icon, current.icon);
  copyText(settings_.splash, current.splash);
  settings_.newPermission.fill('\0');
  settings_.versionCode = current.versionCode;
  settings_.windowWidth = current.window.width;
  settings_.windowHeight = current.window.height;
  settings_.windowMode = current.window.mode;
  settings_.orientation = current.android.orientation;
  settings_.minimumSdk = current.android.minimumSdk;
  settings_.arm64 = std::ranges::find(current.android.abis, "arm64-v8a") !=
                    current.android.abis.end();
  settings_.x86_64 = std::ranges::find(current.android.abis, "x86_64") !=
                     current.android.abis.end();
  settings_.permissions = current.android.permissions;
  settings_.source = source;
  settings_.initialized = true;
  settings_.dirty = false;
}

runtime::ProjectBuildSettings EditorBuildPanel::editedBuildSettings() const {
  runtime::ProjectBuildSettings result;
  result.authored = true;
  result.applicationId = settings_.applicationId.data();
  result.displayName = settings_.displayName.data();
  result.executableName = settings_.executableName.data();
  result.versionName = settings_.versionName.data();
  result.versionCode = settings_.versionCode;
  result.icon = settings_.icon.data();
  result.splash = settings_.splash.data();
  result.window = {.width = settings_.windowWidth,
                   .height = settings_.windowHeight,
                   .mode = settings_.windowMode};
  result.android.orientation = settings_.orientation;
  result.android.minimumSdk = settings_.minimumSdk;
  if (settings_.arm64)
    result.android.abis.push_back("arm64-v8a");
  if (settings_.x86_64)
    result.android.abis.push_back("x86_64");
  result.android.permissions = settings_.permissions;
  return result;
}

void EditorBuildPanel::drawBuildSettings(EditorWorkspace &workspace,
                                         std::string &notice) {
  const auto changed = [&](const bool value) {
    settings_.dirty = settings_.dirty || value;
  };
  const auto assetPicker = [&](const char *label, auto &buffer) {
    const char *preview = buffer[0] == '\0' ? "None" : buffer.data();
    if (!ImGui::BeginCombo(label, preview))
      return;
    if (ImGui::Selectable("None", buffer[0] == '\0')) {
      buffer.fill('\0');
      settings_.dirty = true;
    }
    for (const EditorAssetRecord &asset : workspace.assetIndex().assets()) {
      if (asset.manifest.type != "Texture2D" &&
          asset.manifest.type != "SvgTexture2D")
        continue;
      if (ImGui::Selectable(asset.manifest.id.c_str(),
                            asset.manifest.id == buffer.data())) {
        copyText(buffer, asset.manifest.id);
        settings_.dirty = true;
      }
    }
    ImGui::EndCombo();
  };

  if (ImGui::CollapsingHeader("Application", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SetNextItemWidth(-1.0F);
    changed(ImGui::InputText("Application ID", settings_.applicationId.data(),
                             settings_.applicationId.size()));
    ImGui::SetNextItemWidth(-1.0F);
    changed(ImGui::InputText("Display Name", settings_.displayName.data(),
                             settings_.displayName.size()));
    ImGui::SetNextItemWidth(-1.0F);
    changed(ImGui::InputText("Executable", settings_.executableName.data(),
                             settings_.executableName.size()));
    ImGui::SetNextItemWidth(180.0F);
    changed(ImGui::InputText("Version", settings_.versionName.data(),
                             settings_.versionName.size()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    changed(ImGui::InputInt("Version Code", &settings_.versionCode));
    assetPicker("Icon", settings_.icon);
    assetPicker("Splash", settings_.splash);
  }

  if (ImGui::CollapsingHeader("Linux / Desktop",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SetNextItemWidth(120.0F);
    changed(ImGui::InputInt("Width", &settings_.windowWidth));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    changed(ImGui::InputInt("Height", &settings_.windowHeight));
    if (ImGui::BeginCombo("Window Mode", settings_.windowMode.c_str())) {
      for (const char *mode : {"windowed", "borderless", "fullscreen"}) {
        if (ImGui::Selectable(mode, settings_.windowMode == mode)) {
          settings_.windowMode = mode;
          settings_.dirty = true;
        }
      }
      ImGui::EndCombo();
    }
  }

  if (ImGui::CollapsingHeader("Android", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginCombo("Orientation", settings_.orientation.c_str())) {
      for (const char *orientation : {"unspecified", "portrait", "landscape",
                                      "portrait_sensor",
                                      "landscape_sensor"}) {
        if (ImGui::Selectable(orientation,
                              settings_.orientation == orientation)) {
          settings_.orientation = orientation;
          settings_.dirty = true;
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(120.0F);
    changed(ImGui::InputInt("Minimum SDK", &settings_.minimumSdk));
    changed(ImGui::Checkbox("ARM64 (arm64-v8a)", &settings_.arm64));
    ImGui::SameLine();
    changed(ImGui::Checkbox("x86_64", &settings_.x86_64));

    ImGui::TextDisabled("Declared permissions");
    std::optional<std::size_t> removePermission;
    for (std::size_t index = 0; index < settings_.permissions.size(); ++index) {
      ImGui::PushID(static_cast<int>(index));
      ImGui::TextUnformatted(settings_.permissions[index].c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Remove"))
        removePermission = index;
      ImGui::PopID();
    }
    if (removePermission) {
      settings_.permissions.erase(
          settings_.permissions.begin() +
          static_cast<std::ptrdiff_t>(*removePermission));
      settings_.dirty = true;
    }
    ImGui::SetNextItemWidth(390.0F);
    ImGui::InputTextWithHint("##permission", "android.permission.INTERNET",
                             settings_.newPermission.data(),
                             settings_.newPermission.size());
    ImGui::SameLine();
    const std::string permission = settings_.newPermission.data();
    ImGui::BeginDisabled(permission.empty());
    if (ImGui::Button("Add##permission")) {
      if (std::ranges::find(settings_.permissions, permission) !=
          settings_.permissions.end()) {
        notice = "That Android permission is already declared.";
      } else {
        settings_.permissions.push_back(permission);
        settings_.newPermission.fill('\0');
        settings_.dirty = true;
      }
    }
    ImGui::EndDisabled();
  }

  ImGui::Spacing();
  ImGui::BeginDisabled(!settings_.dirty);
  if (ImGui::Button("Apply Settings", {140.0F, 28.0F})) {
    std::string error;
    if (workspace.setProjectBuildSettings(editedBuildSettings(), error)) {
      notice = "Build settings updated";
      settings_.dirty = false;
      settings_.initialized = false;
      syncBuildSettings(workspace);
    } else {
      notice = error;
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!settings_.dirty);
  if (ImGui::Button("Reset Changes", {120.0F, 28.0F})) {
    settings_.dirty = false;
    settings_.initialized = false;
    syncBuildSettings(workspace);
    notice = "Discarded unapplied build setting changes";
  }
  ImGui::EndDisabled();
}

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
  syncBuildSettings(workspace);
  ImGui::SetNextWindowSize({640.0F, 760.0F}, ImGuiCond_Appearing);
  if (!ImGui::Begin("Build Project", &show_,
                    ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return;
  }
  editorSectionTitle("Build");
  ImGui::BeginDisabled(settings_.dirty);
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
  ImGui::EndDisabled();
  if (settings_.dirty) {
    ImGui::SameLine();
    ImGui::TextDisabled("Apply the pending settings before building.");
  }
  ImGui::Separator();
  ImGui::TextDisabled("Targets");
  ImGui::Checkbox("Linux (64-bit)", &linuxTarget_);
  ImGui::Checkbox("Android (ARM64)", &androidTarget_);
  ImGui::Separator();
  editorSectionTitle("Build Settings");
  drawBuildSettings(workspace, notice);
  if (operation.running || operation.result) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", operation.progress.message.c_str());
    ImGui::ProgressBar(operation.progress.fraction, {-1.0F, 0.0F});
    if (!operation.running && operation.result &&
        operation.result->succeeded()) {
      ImGui::TextUnformatted("Artifact");
      ImGui::SameLine();
      ImGui::TextWrapped("%s", operation.result->artifact.string().c_str());
    }
  }
  ImGui::Spacing();
  ImGui::Separator();
  if (operation.running) {
    if (ImGui::Button("Cancel", {-1.0F, 30.0F})) {
      operations_.cancel();
      notice = "Cancelling project operation";
    }
  } else {
    const bool hasTarget = linuxTarget_ || androidTarget_;
    ImGui::BeginDisabled(!hasTarget || settings_.dirty);
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
