#pragma once
#include "editor/EditorSelection.h"
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime {
struct World;
struct Entity;
} // namespace demi::runtime
namespace demi::editor {

// Immutable projection rebuilt when authored or preview revisions change. All
// relationships and descendant search visibility are resolved before
// presentation walks rows.
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
    bool hasChildren = false;
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
// Retains the expensive graph and open-row projection independently of frame
// presentation. Callers supply explicit mutation revisions, never collection
// sizes or storage addresses as substitutes for content changes.
class EditorHierarchyProjection {
public:
  enum class RowKind { Entity, PaintedGroup, PaintedCell };
  struct Row {
    RowKind kind = RowKind::Entity;
    std::size_t node = 0;
    std::size_t cell = 0;
    int depth = 0;
  };
  void refresh(const runtime::World &world, const nlohmann::json &document,
               std::uint64_t contentRevision, std::uint64_t previewRevision,
               std::string_view filter);
  [[nodiscard]] const EditorHierarchyIndex &index() const { return *index_; }
  [[nodiscard]] bool entityOpen(const std::string &id) const;
  [[nodiscard]] bool paintedCellsOpen(const std::string &id) const;
  void setEntityOpen(const std::string &id, bool open);
  void setPaintedCellsOpen(const std::string &id, bool open);
  [[nodiscard]] const std::vector<Row> &rows();
  [[nodiscard]] std::uint64_t indexBuildCount() const {
    return indexBuildCount_;
  }
  [[nodiscard]] std::uint64_t rowBuildCount() const { return rowBuildCount_; }

private:
  const runtime::World *world_ = nullptr;
  std::optional<EditorHierarchyIndex> index_;
  std::uint64_t contentRevision_ = 0;
  std::uint64_t previewRevision_ = 0;
  std::uint64_t indexBuildCount_ = 0;
  std::uint64_t rowBuildCount_ = 0;
  std::string filter_;
  std::unordered_set<std::string> closedEntities_;
  std::unordered_set<std::string> openPaintedGroups_;
  std::vector<Row> rows_;
  bool rowsDirty_ = true;
};
} // namespace demi::editor
