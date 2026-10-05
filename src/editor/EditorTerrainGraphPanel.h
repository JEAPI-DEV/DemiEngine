#pragma once

#include "demi/runtime/terrain/TerrainGraphRegistry.h"
#include "editor/EditorCommands.h"
#include "editor/EditorGraphCanvasView.h"
#include "editor/EditorTerrainGraphDocument.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ImNodesEditorContext;
struct ImNodesContext;

namespace demi::editor {

class EditorWorkspace;
class EditorKeyBindings;

// ImGui/imnodes presentation for the workspace's currently bound terrain draft.
// The shell owns the Stage tab and calls draw() inside its content region.
class EditorTerrainGraphPanel {
public:
  ~EditorTerrainGraphPanel();
  EditorTerrainGraphPanel() = default;
  EditorTerrainGraphPanel(const EditorTerrainGraphPanel &) = delete;
  EditorTerrainGraphPanel &operator=(const EditorTerrainGraphPanel &) = delete;

  void requestOpen() { open_ = true; }
  void open(const EditorWorkspace &workspace);
  [[nodiscard]] bool openNodeSettings(const EditorWorkspace &workspace,
                                      std::string_view nodeId,
                                      std::string &error);
  void close() { open_ = false; }
  [[nodiscard]] bool isOpen() const { return open_; }
  [[nodiscard]] float canvasZoom() const { return canvasView_.zoom(); }
  // Accepts the stable module ID, e.g. "terrain:height_blend".
  [[nodiscard]] bool acceptModulePayload(std::string_view moduleId);
  [[nodiscard]] bool executeCommand(EditorWorkspace &workspace,
                                    EditorCommand command, std::string &error);
  void setKeyBindings(const EditorKeyBindings &bindings) {
    keyBindings_ = &bindings;
  }
  void draw(EditorWorkspace &workspace, std::string &notice);
  void releaseUiResources() noexcept;

private:
  struct Pin {
    std::string node;
    std::string port;
    bool output = false;
  };
  void bind(const EditorWorkspace &workspace);
  int uiId(std::string_view key);
  void addPendingNode(EditorWorkspace &workspace, float x, float y,
                      std::string &notice);
  void drawPalette(EditorWorkspace &workspace, std::string &notice);
  void drawCanvas(EditorWorkspace &workspace, std::string &notice);
  void drawNode(EditorWorkspace &workspace, const nlohmann::json &node,
                const std::unordered_map<std::string, std::string> &errors,
                std::string &notice);
  void drawInputSource(EditorWorkspace &workspace, std::string_view nodeId,
                       const runtime::TerrainGraphPort &port,
                       std::string &notice);
  void drawParameter(EditorWorkspace &workspace, std::string_view nodeId,
                     const nlohmann::json &parameters,
                     const runtime::TerrainGraphParameter &definition,
                     std::string &notice);
  [[nodiscard]] std::optional<nlohmann::json>
  drawRiverPath(std::string_view nodeId, const nlohmann::json &path,
                const nlohmann::json &defaultPath);
  void clearWaypointEdits(std::string_view nodeId);
  void drawDiagnostics(const nlohmann::json &graph);
  void drawSettingsOverlay(EditorWorkspace &workspace, ImVec2 origin,
                           ImVec2 size, std::string &notice);
  void readSelection(const nlohmann::json &graph);
  void selectNodes(std::vector<std::string> nodes);
  void applyPendingSelection(const nlohmann::json &graph);
  void drawCommandButton(EditorWorkspace &workspace, EditorCommand command,
                         std::string &notice, bool enabled);
  [[nodiscard]] std::string commandTooltip(EditorCommand command) const;

  EditorTerrainGraphDocument document_;
  std::string binding_;
  std::string entityId_;
  std::string selectedNode_;
  std::string selectedLink_;
  std::vector<std::string> selectedNodes_;
  std::vector<std::string> selectedLinks_;
  bool selectionPending_ = false;
  const EditorKeyBindings *keyBindings_ = nullptr;
  std::string lastPasteClipboard_;
  float nextPasteOffset_ = 32.0F;
  std::string pendingNodeType_;
  std::unordered_map<std::string, int> uiIds_;
  std::unordered_map<int, Pin> pins_;
  std::unordered_map<int, std::string> links_;
  std::unordered_map<std::string, nlohmann::json> shownPositions_;
  std::unordered_map<std::string, std::string> textEdits_;
  std::unordered_map<std::string, nlohmann::json> numericEdits_;
  std::unordered_map<std::string, std::array<double, 3>> waypointEdits_;
  int nextUiId_ = 1;
  ImNodesContext *context_ = nullptr;
  ImNodesEditorContext *editorContext_ = nullptr;
  bool open_ = false;
  bool showSettings_ = true;
  std::string settingsNode_;
  bool showLocalPalette_ = false;
  bool settingsExpanded_ = true;
  float settingsWidth_ = 380.0F;
  EditorGraphCanvasView canvasView_;
  bool resetZoomRequested_ = false;
  bool frameSelectionRequested_ = false;
};

} // namespace demi::editor
