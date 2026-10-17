#pragma once
#include "editor/EditorSelection.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace demi::runtime {
struct World;
struct Entity;
} // namespace demi::runtime
namespace demi::editor {

// Immutable projection rebuilt once per panel frame. All relationships and
// descendant search visibility are resolved before presentation walks rows.
class EditorHierarchyIndex {
public:
  struct Node {
    const runtime::Entity *entity = nullptr;
    std::optional<std::size_t> parent;
    std::vector<std::size_t> children;
    std::vector<EditorIsoGridCell> paintedCells;
    bool authored = false;
    bool hiddenPreview = false;
    bool visible = false;
  };
  EditorHierarchyIndex(const runtime::World &world,
                        const nlohmann::json &authoredDocument,
                        std::string_view filter);
  void filter(std::string_view text);
  const Node *find(std::string_view id) const;
  const Node &node(std::size_t index) const { return nodes_[index]; }
  const std::vector<std::size_t> &roots() const { return roots_; }
  std::size_t size() const { return nodes_.size(); }

private:
  std::vector<Node> nodes_;
  std::unordered_map<std::string_view, std::size_t> byId_;
  std::vector<std::size_t> roots_;
  std::vector<std::size_t> traversal_;
};
} // namespace demi::editor
