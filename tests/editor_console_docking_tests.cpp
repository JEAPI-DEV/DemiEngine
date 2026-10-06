#include "editor/EditorConsolePanel.h"
#include "editor/EditorDockingState.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace demi::editor;

constexpr std::array WindowNames{"Console", "Lua Console", "Profiler", "Debug"};

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

class ImGuiFixture {
public:
  ImGuiFixture() {
    ImGui::SetAllocatorFunctions(
        [](std::size_t bytes, void *) { return std::malloc(bytes); },
        [](void *memory, void *) { std::free(memory); });
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = {1600, 1000};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    require(pixels != nullptr && width > 0 && height > 0,
            "Could not build the ImGui font atlas");
  }

  ~ImGuiFixture() { ImGui::DestroyContext(); }
  ImGuiFixture(const ImGuiFixture &) = delete;
  ImGuiFixture &operator=(const ImGuiFixture &) = delete;
};

void renderPanels(EditorConsolePanel &panel, EditorWorkspace &workspace,
                  EditorPlaySession &playSession,
                  EditorPanelVisibility &visibility,
                  const ImGuiID dockspace = 0) {
  ImGui::NewFrame();
  if (dockspace != 0) {
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({1500, 900}, ImGuiCond_Always);
    ImGui::Begin("Diagnostic docking host", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::DockSpace(dockspace);
    ImGui::End();
  }
  std::string notice;
  panel.draw(workspace, playSession, {0, 0}, {650, 420}, {}, notice,
             visibility);
  ImGui::Render();
  require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0,
          "Diagnostic windows produced an ImGui scope error");
  require(ImGui::GetCurrentContext()->CurrentTabBarStack.empty(),
          "Diagnostic windows left an internal tab scope open");
}

void checkSeparateWindows(EditorConsolePanel &panel, EditorWorkspace &workspace,
                          EditorPlaySession &playSession,
                          EditorPanelVisibility &visibility) {
  visibility.console = true;
  visibility.luaConsole = true;
  visibility.profiler = true;
  visibility.debug = true;
  renderPanels(panel, workspace, playSession, visibility);
  renderPanels(panel, workspace, playSession, visibility);

  std::array<ImGuiID, WindowNames.size()> ids{};
  for (std::size_t index = 0; index < WindowNames.size(); ++index) {
    const auto *window = ImGui::FindWindowByName(WindowNames[index]);
    require(window != nullptr && window->Active,
            std::string("Missing independent window: ") + WindowNames[index]);
    require((window->Flags & ImGuiWindowFlags_NoDocking) == 0 &&
                (window->Flags & ImGuiWindowFlags_ChildWindow) == 0,
            std::string("Window is not independently dockable: ") +
                WindowNames[index]);
    require(window->ParentWindow == nullptr,
            "A diagnostic tool is still nested inside another window");
    ids[index] = window->ID;
    for (std::size_t previous = 0; previous < index; ++previous)
      require(ids[previous] != ids[index],
              "Diagnostic tools share an ImGui window identity");
  }

  // Closing one tool must not hide the others, including the stopped profiler
  // path, which used to end a nested tab instead of its own window.
  const std::array flags{&visibility.console, &visibility.luaConsole,
                         &visibility.profiler, &visibility.debug};
  for (std::size_t hidden = 0; hidden < flags.size(); ++hidden) {
    *flags[hidden] = false;
    renderPanels(panel, workspace, playSession, visibility);
    for (std::size_t index = 0; index < WindowNames.size(); ++index) {
      const auto *window = ImGui::FindWindowByName(WindowNames[index]);
      require(window->Active == (index != hidden),
              "Closing a diagnostic tool changed another tool's visibility");
      require(window->ID == ids[index],
              "A diagnostic window identity changed after closing a tool");
    }
    *flags[hidden] = true;
  }
}

void checkIndependentDocking(EditorConsolePanel &panel,
                             EditorWorkspace &workspace,
                             EditorPlaySession &playSession,
                             EditorPanelVisibility &visibility) {
  const ImGuiID dockspace = ImHashStr("ConsoleDockingTest");
  ImGui::NewFrame();
  ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({1500, 900}, ImGuiCond_Always);
  ImGui::Begin("Diagnostic docking host", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, {1500, 900});
  ImGui::DockBuilderSetNodePos(dockspace, {0, 0});
  ImGuiID right = 0;
  ImGuiID left = 0;
  ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.5F, &left, &right);
  std::array<ImGuiID, WindowNames.size()> nodes{};
  ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.5F, &nodes[0], &nodes[1]);
  ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5F, &nodes[2], &nodes[3]);
  for (std::size_t index = 0; index < WindowNames.size(); ++index)
    ImGui::DockBuilderDockWindow(WindowNames[index], nodes[index]);
  ImGui::DockBuilderFinish(dockspace);
  ImGui::DockSpace(dockspace);
  ImGui::End();
  std::string notice;
  panel.draw(workspace, playSession, {0, 0}, {650, 420}, {}, notice,
             visibility);
  ImGui::Render();
  require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0,
          "Building the diagnostic dockspace produced an ImGui scope error");

  renderPanels(panel, workspace, playSession, visibility, dockspace);
  renderPanels(panel, workspace, playSession, visibility, dockspace);
  for (std::size_t index = 0; index < WindowNames.size(); ++index) {
    const auto *window = ImGui::FindWindowByName(WindowNames[index]);
    require(window != nullptr && window->DockId == nodes[index],
            "A diagnostic tool could not dock separately from the other tools");
    require(window->DockNode != nullptr,
            "The independent tool has no live docking node");
  }
}

} // namespace

int main() {
  try {
    ImGuiFixture imgui;
    EditorWorkspace workspace;
    EditorPlaySession playSession;
    EditorConsolePanel panel;
    EditorPanelVisibility visibility;
    checkSeparateWindows(panel, workspace, playSession, visibility);
    checkIndependentDocking(panel, workspace, playSession, visibility);
    std::cout << "Console docking tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
