#include "editor/EditorRunPanel.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"
#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <regex>

namespace demi::editor {
namespace {
std::filesystem::path runtimeForProfile(int profile) {
  const auto directory = runtime::platform::applicationDirectory();
#ifdef _WIN32
  constexpr auto executable = "demi.exe";
  constexpr auto platform = "windows";
#else
  constexpr auto executable = "demi";
  constexpr auto platform = "linux";
#endif
  const auto configuration = profile == 0 ? "debug" : "release";
  auto sibling = directory.parent_path() /
                 (std::string(platform) + "-" + configuration) / executable;
  std::error_code error;
  if (std::filesystem::is_regular_file(sibling, error))
    return sibling;
  return std::filesystem::path(DEMI_SOURCE_DIR) / "build" /
         (std::string(platform) + "-" + configuration) / executable;
}
} // namespace
void EditorRunPanel::draw(EditorWorkspace &workspace, bool embeddedRunning,
                          const std::function<bool(std::string &)> &saveAll,
                          std::string &notice) {
  const bool wasRunning = process_.running();
  process_.poll();
  if (process_.running() && testing_ &&
      std::chrono::steady_clock::now() - started_ >
          std::chrono::seconds(timeoutSeconds_)) {
    process_.stop();
    result_ = "Test timed out";
  } else if (wasRunning && !process_.running()) {
    const bool succeeded = process_.exitCode().value_or(-1) == 0;
    if (testing_) {
      static const std::regex summary(
          R"(\[test\] SUMMARY passed=(\d+) failed=(\d+))");
      std::smatch match;
      const bool reported =
          std::regex_search(process_.output(), match, summary);
      result_ = succeeded && reported && match[2] == "0"
                    ? "Tests passed (" + match[1].str() + ")"
                : reported ? "Tests failed"
                           : "Test run ended without a summary";
    } else {
      result_ = succeeded ? "Run finished" : "Run failed";
    }
  }
  if (!show_)
    return;
  prepareEditorDialog({.preferredEm = {54, 38}, .minimumEm = {36, 24}});
  if (!ImGui::Begin("Run & Test", &show_)) {
    ImGui::End();
    return;
  }
  if (profile_ < 0) {
#ifdef NDEBUG
    profile_ = 1;
#else
    profile_ = 0;
#endif
    executable_ = runtimeForProfile(profile_).string();
  }
  ImGui::TextUnformatted("Standalone runtime");
  ImGui::TextWrapped("Run the actual Debug or Release executable. Embedded "
                     "Play uses the editor's own build.");
  ImGui::BeginDisabled(process_.running());
  if (ImGui::Combo("Configuration", &profile_, "Debug\0Release\0"))
    executable_ = runtimeForProfile(profile_).string();
  if (ImGui::CollapsingHeader("Runtime location")) {
    ImGui::InputText("Executable", &executable_);
    ImGui::TextWrapped(
        "Select a matching demi CLI build if it is installed elsewhere.");
  }
  std::error_code fileError;
  const bool available =
      std::filesystem::is_regular_file(executable_, fileError);
  if (!available)
    ImGui::TextWrapped("Runtime is not built: %s", executable_.c_str());
  ImGui::InputInt("Test frame limit", &maxFrames_);
  ImGui::InputInt("Test timeout (seconds)", &timeoutSeconds_);
  maxFrames_ = std::clamp(maxFrames_, 1, 10000000);
  timeoutSeconds_ = std::clamp(timeoutSeconds_, 1, 3600);
  const bool hasTests = std::filesystem::is_regular_file(
      workspace.projectPath().parent_path() / "scripts/tests/e2e.lua");
  ImGui::BeginDisabled(!available || embeddedRunning);
  const bool run = ImGui::Button("Run project");
  ImGui::SameLine();
  ImGui::BeginDisabled(!hasTests);
  const bool test = ImGui::Button("Run E2E tests");
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  if (embeddedRunning)
    ImGui::TextDisabled("Stop embedded Play before starting another run.");
  if (!hasTests)
    ImGui::TextDisabled("Add scripts/tests/e2e.lua to enable project tests.");
  if (run || test) {
    std::string error;
    if (saveAll(error)) {
      const auto size = ImGui::GetMainViewport()->Size;
      std::vector<std::string> arguments{
          executable_,
          "run",
          "--project",
          workspace.projectPath().string(),
          "--window-size",
          std::to_string(
              int(size.x * ImGui::GetIO().DisplayFramebufferScale.x)) +
              "x" +
              std::to_string(
                  int(size.y * ImGui::GetIO().DisplayFramebufferScale.y))};
      if (test)
        arguments.insert(arguments.end(), {"--e2e-tests", "--max-frames",
                                           std::to_string(maxFrames_)});
      if (process_.start(arguments, error)) {
        testing_ = test;
        started_ = std::chrono::steady_clock::now();
        result_ = test ? "Tests running" : "Game running";
      }
    }
    if (!error.empty())
      notice = result_ = error;
  }
  if (process_.running() && ImGui::Button("Stop")) {
    process_.stop();
    result_ = "Cancelled";
  }
  ImGui::Separator();
  ImGui::TextWrapped("%s", result_.c_str());
  ImGui::BeginChild("run-output", {0, 0}, ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
  const bool followOutput = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
  ImGui::TextUnformatted(process_.output().c_str());
  if (followOutput)
    ImGui::SetScrollHereY(1.0F);
  ImGui::EndChild();
  ImGui::End();
}
} // namespace demi::editor
