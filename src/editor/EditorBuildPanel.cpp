#include "editor/EditorBuildPanel.h"

#include "demi/runtime/platform/RuntimeCapabilities.h"
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

constexpr EditorDialogLayoutSpec BuildLayout{
    .preferredEm = {68.0F, 52.0F},
    .minimumEm = {42.0F, 32.0F},
};

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
          .runtimeExecutable = editorRuntimeExecutable(),
          .hostFeatures = runtime::hostRuntimeFeatures()};
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
  const auto assetPicker = [&](const char *id, auto &buffer) {
    const char *preview = buffer[0] == '\0' ? "None" : buffer.data();
    ImGui::SetNextItemWidth(-1.0F);
    if (!ImGui::BeginCombo(id, preview))
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
  const auto beginPropertyTable = [](const char *id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
      return false;
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetFontSize() * 12.0F);
    ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
    return true;
  };
  const auto propertyRow = [](const char *label, const auto &drawControl) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    drawControl();
  };

  if (ImGui::CollapsingHeader("Application", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (beginPropertyTable("application-settings")) {
      propertyRow("Application ID", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        changed(ImGui::InputText("##application-id",
                                 settings_.applicationId.data(),
                                 settings_.applicationId.size()));
      });
      propertyRow("Display name", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        changed(ImGui::InputText("##display-name", settings_.displayName.data(),
                                 settings_.displayName.size()));
      });
      propertyRow("Executable", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        changed(ImGui::InputText("##executable",
                                 settings_.executableName.data(),
                                 settings_.executableName.size()));
      });
      propertyRow("Version", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        changed(ImGui::InputText("##version", settings_.versionName.data(),
                                 settings_.versionName.size()));
      });
      propertyRow("Version code", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        changed(ImGui::InputInt("##version-code", &settings_.versionCode));
      });
      propertyRow("Icon", [&] { assetPicker("##icon", settings_.icon); });
      propertyRow("Splash", [&] { assetPicker("##splash", settings_.splash); });
      ImGui::EndTable();
    }
  }

  if (ImGui::CollapsingHeader("Linux / Desktop",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    if (beginPropertyTable("desktop-settings")) {
      propertyRow("Window width", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        changed(ImGui::InputInt("##window-width", &settings_.windowWidth));
      });
      propertyRow("Window height", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        changed(ImGui::InputInt("##window-height", &settings_.windowHeight));
      });
      propertyRow("Window mode", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("##window-mode", settings_.windowMode.c_str())) {
          for (const char *mode : {"windowed", "borderless", "fullscreen"}) {
            if (ImGui::Selectable(mode, settings_.windowMode == mode)) {
              settings_.windowMode = mode;
              settings_.dirty = true;
            }
          }
          ImGui::EndCombo();
        }
      });
      ImGui::EndTable();
    }
  }

  if (ImGui::CollapsingHeader("Android", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (beginPropertyTable("android-settings")) {
      propertyRow("Orientation", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("##orientation", settings_.orientation.c_str())) {
          for (const char *orientation :
               {"unspecified", "portrait", "landscape", "portrait_sensor",
                "landscape_sensor"}) {
            if (ImGui::Selectable(orientation,
                                  settings_.orientation == orientation)) {
              settings_.orientation = orientation;
              settings_.dirty = true;
            }
          }
          ImGui::EndCombo();
        }
      });
      propertyRow("Minimum SDK", [&] {
        ImGui::SetNextItemWidth(-1.0F);
        changed(ImGui::InputInt("##minimum-sdk", &settings_.minimumSdk));
      });
      propertyRow("Architectures", [&] {
        changed(ImGui::Checkbox("ARM64 (arm64-v8a)", &settings_.arm64));
        ImGui::SameLine();
        changed(ImGui::Checkbox("x86_64", &settings_.x86_64));
      });
      ImGui::EndTable();
    }

    ImGui::TextDisabled("Declared permissions");
    std::optional<std::size_t> removePermission;
    if (ImGui::BeginTable("android-permissions", 2,
                          ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableSetupColumn("Permission", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
      for (std::size_t index = 0; index < settings_.permissions.size();
           ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(settings_.permissions[index].c_str());
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Remove"))
          removePermission = index;
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (removePermission) {
      settings_.permissions.erase(
          settings_.permissions.begin() +
          static_cast<std::ptrdiff_t>(*removePermission));
      settings_.dirty = true;
    }
    if (ImGui::BeginTable("add-android-permission", 2,
                          ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Permission", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0F);
      ImGui::InputTextWithHint("##permission", "android.permission.INTERNET",
                               settings_.newPermission.data(),
                               settings_.newPermission.size());
      ImGui::TableNextColumn();
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
      ImGui::EndTable();
    }
  }

  ImGui::Spacing();
  ImGui::BeginDisabled(!settings_.dirty);
  if (ImGui::Button("Apply Settings")) {
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
  if (ImGui::Button("Reset Changes")) {
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
  prepareEditorDialog(BuildLayout);
  if (!ImGui::Begin("Build Project", &show_)) {
    ImGui::End();
    return;
  }
  const float footerHeight =
      ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0F;
  ImGui::BeginChild("build-project-content", {0.0F, -footerHeight},
                    ImGuiChildFlags_None,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar);
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
  ImGui::Checkbox("Android", &androidTarget_);
  if (androidTarget_) {
    if (ImGui::BeginCombo("Configuration",
                          releaseBuild_ ? "Release" : "Debug")) {
      if (ImGui::Selectable("Debug", !releaseBuild_)) {
        releaseBuild_ = false;
        androidBundle_ = false;
      }
      if (ImGui::Selectable("Release", releaseBuild_))
        releaseBuild_ = true;
      ImGui::EndCombo();
    }
    if (releaseBuild_ &&
        ImGui::BeginCombo("Android Output",
                          androidBundle_ ? "App Bundle (.aab)" : "APK")) {
      if (ImGui::Selectable("APK", !androidBundle_))
        androidBundle_ = false;
      if (ImGui::Selectable("App Bundle (.aab)", androidBundle_))
        androidBundle_ = true;
      ImGui::EndCombo();
    }
    if (releaseBuild_)
      ImGui::TextDisabled(
          "Signing uses DEMI_ANDROID_KEYSTORE, KEYSTORE_PASSWORD, KEY_ALIAS, "
          "and KEY_PASSWORD");
  }
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
  ImGui::EndChild();
  if (operation.running) {
    if (ImGui::Button("Cancel", {-1.0F, ImGui::GetFrameHeight()})) {
      operations_.cancel();
      notice = "Cancelling project operation";
    }
  } else {
    const bool hasTarget = linuxTarget_ || androidTarget_;
    ImGui::BeginDisabled(!hasTarget || settings_.dirty);
    if (ImGui::Button("Build Project", {-1.0F, ImGui::GetFrameHeight()}) &&
        saveBeforeOperation(workspace, notice)) {
      std::vector<build::ProjectOperationRequest> requests;
      if (linuxTarget_)
        requests.push_back(
            operationRequest(workspace, build::ProjectOperation::PackageLinux));
      if (androidTarget_)
        requests.push_back(operationRequest(
            workspace, !releaseBuild_ ? build::ProjectOperation::PackageAndroid
                       : androidBundle_
                           ? build::ProjectOperation::BundleAndroidRelease
                           : build::ProjectOperation::PackageAndroidRelease));
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
