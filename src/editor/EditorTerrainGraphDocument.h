#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::editor {

// Edits only recipe.graph. The caller owns the recipe draft and its eventual
// Generate transaction; these snapshots are undo history for unapplied edits.
class EditorTerrainGraphDocument {
public:
  struct NodePosition {
    std::string nodeId;
    float x = 0;
    float y = 0;
  };
  void bind(std::string identity);
  void clearHistory();
  [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
  [[nodiscard]] bool undo(nlohmann::json &recipe);
  [[nodiscard]] bool redo(nlohmann::json &recipe);

  [[nodiscard]] bool replace(nlohmann::json &recipe, nlohmann::json graph,
                             std::string &error);
  [[nodiscard]] bool addNode(nlohmann::json &recipe, std::string_view type,
                             nlohmann::json parameters, float x, float y,
                             std::string &nodeId, std::string &error,
                             bool chooseOutputWhenEmpty = false);
  [[nodiscard]] bool duplicateNode(nlohmann::json &recipe,
                                   std::string_view nodeId,
                                   std::string &newNodeId, std::string &error);
  [[nodiscard]] std::optional<nlohmann::json>
  copySelection(const nlohmann::json &recipe,
                const std::vector<std::string> &nodeIds,
                std::string &error) const;
  [[nodiscard]] bool pasteSelection(nlohmann::json &recipe,
                                    const nlohmann::json &subgraph,
                                    float offsetX, float offsetY,
                                    std::vector<std::string> &newNodeIds,
                                    std::string &error);
  [[nodiscard]] bool duplicateSelection(nlohmann::json &recipe,
                                        const std::vector<std::string> &nodeIds,
                                        std::vector<std::string> &newNodeIds,
                                        std::string &error);
  [[nodiscard]] bool removeSelection(nlohmann::json &recipe,
                                     const std::vector<std::string> &nodeIds,
                                     const std::vector<std::string> &linkIds,
                                     std::string &error);
  [[nodiscard]] bool removeNode(nlohmann::json &recipe, std::string_view nodeId,
                                std::string &error);
  [[nodiscard]] bool connect(nlohmann::json &recipe, std::string_view fromNode,
                             std::string_view fromPort, std::string_view toNode,
                             std::string_view toPort, std::string &error);
  // Replace one input's source, or disconnect it when fromNode is empty.
  // Preserve existing link identity/order; record the entire edit as one Undo.
  // False with an empty error means unchanged, without consuming Undo/Redo.
  [[nodiscard]] bool
  setInputSource(nlohmann::json &recipe, std::string_view toNode,
                 std::string_view toPort, std::string_view fromNode,
                 std::string_view fromPort, std::string &error);
  [[nodiscard]] bool removeLink(nlohmann::json &recipe, std::string_view linkId,
                                std::string &error);
  [[nodiscard]] bool setParameter(nlohmann::json &recipe,
                                  std::string_view nodeId,
                                  std::string_view name, nlohmann::json value,
                                  std::string &error);
  [[nodiscard]] bool setPosition(nlohmann::json &recipe,
                                 std::string_view nodeId, float x, float y,
                                 std::string &error);
  [[nodiscard]] bool setPositions(nlohmann::json &recipe,
                                  const std::vector<NodePosition> &positions,
                                  std::string &error);
  [[nodiscard]] bool setOutput(nlohmann::json &recipe, std::string_view nodeId,
                               std::string &error);

private:
  struct Change {
    nlohmann::json before;
    nlohmann::json after;
  };
  [[nodiscard]] bool commit(nlohmann::json &recipe, nlohmann::json graph);
  std::string identity_;
  std::vector<Change> undo_;
  std::vector<Change> redo_;
};

} // namespace demi::editor
