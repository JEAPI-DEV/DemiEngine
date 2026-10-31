#include "editor/EditorHierarchyIndex.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PrefabPlacement3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include "demi/runtime/terrain/TerrainVegetationWorld.h"
#include "editor/EditorIsoGridCell.h"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace demi::editor {
namespace {
std::string_view parentId(const runtime::Entity &entity) {
  if (const auto *value = entity.component<runtime::Transform3DComponent>()) {
    return value->parent;
  }
  if (const auto *value = entity.component<runtime::Transform2DComponent>()) {
    return value->parent;
  }
  if (const auto *value = entity.component<runtime::IsoTransformComponent>()) {
    return value->parent;
  }
  return {};
}
std::string lower(std::string_view text) {
  std::string result(text);
  for (auto &character : result) {
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return result;
}
std::unordered_set<std::string_view>
authoredIds(const nlohmann::json &document) {
  std::unordered_set<std::string_view> ids;
  std::vector<const nlohmann::json *> arrays;
  if (const auto entities = document.find("entities");
      entities != document.end() && entities->is_array()) {
    arrays.push_back(&*entities);
  }
  while (!arrays.empty()) {
    const auto *array = arrays.back();
    arrays.pop_back();
    for (const auto &entity : *array) {
      if (const auto id = entity.find("id");
          id != entity.end() && id->is_string()) {
        ids.insert(id->get_ref<const std::string &>());
      }
      if (const auto children = entity.find("children");
          children != entity.end() && children->is_array()) {
        arrays.push_back(&*children);
      }
    }
  }
  return ids;
}
} // namespace

EditorHierarchyIndex::EditorHierarchyIndex(const runtime::World &world,
                                           const nlohmann::json &document,
                                           std::string_view search) {
  const auto authored = authoredIds(document);
  nodes_.resize(world.entities.size());
  byId_.reserve(nodes_.size());
  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    const auto &entity = world.entities[index];
    nodes_[index].entity = &entity;
    nodes_[index].authored = authored.contains(entity.id);
    nodes_[index].paintedCells = paintedIsoGridCells(entity);
    byId_.try_emplace(entity.id, index);
  }
  for (auto &node : nodes_) {
    const auto parent = byId_.find(parentId(*node.entity));
    if (parent != byId_.end()) {
      node.parent = parent->second;
    }
  }
  // Break one edge per cycle using an iterative walk. Missing parents become
  // roots, so malformed previews remain inspectable and cannot recurse forever.
  std::vector<unsigned char> state(nodes_.size(), 0);
  for (std::size_t start = 0; start < nodes_.size(); ++start) {
    if (state[start] != 0) {
      continue;
    }
    std::vector<std::size_t> path;
    auto current = std::optional<std::size_t>(start);
    while (current && state[*current] == 0) {
      state[*current] = 1;
      path.push_back(*current);
      current = nodes_[*current].parent;
    }
    if (current && state[*current] == 1) {
      nodes_[*current].parent.reset();
    }
    for (const auto index : path) {
      state[index] = 2;
    }
  }
  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    if (nodes_[index].parent) {
      nodes_[*nodes_[index].parent].children.push_back(index);
    } else {
      roots_.push_back(index);
    }
  }
  traversal_ = roots_;
  for (std::size_t cursor = 0; cursor < traversal_.size(); ++cursor) {
    const auto index = traversal_[cursor];
    const auto &children = nodes_[index].children;
    traversal_.insert(traversal_.end(), children.begin(), children.end());
  }
  std::vector<std::string> owners(nodes_.size());
  for (const auto index : traversal_) {
    auto &node = nodes_[index];
    const auto &entity = *node.entity;
    if (entity.hasComponent<runtime::PrefabPlacement3DComponent>()) {
      owners[index] = entity.id;
    } else {
      owners[index] = runtime::terrainSurfaceOwner(entity).value_or("");
      if (const auto *cell = entity.component<runtime::TerrainVegetationCellOwner>()) {
        owners[index] = cell->regionOwner;
      }
      if (const auto *transform =
              entity.component<runtime::Transform3DComponent>()) {
        if (entity.id.starts_with(transform->parent + "/__masonry_preview/")) {
          owners[index] = transform->parent;
        }
        if (node.parent && !owners[*node.parent].empty()) {
          owners[index] = owners[*node.parent];
        }
      }
    }
    node.hiddenPreview =
        !node.authored && !owners[index].empty() && owners[index] != entity.id;
    if (node.parent && nodes_[*node.parent].hiddenPreview) {
      node.hiddenPreview = true;
    }
  }
  for (auto &node : nodes_) {
    node.hasChildren =
        !node.paintedCells.empty() ||
        std::ranges::any_of(node.children, [&](std::size_t child) {
          return !nodes_[child].hiddenPreview;
        });
  }
  filter(search);
}

void EditorHierarchyIndex::filter(std::string_view text) {
  const auto needle = lower(text);
  for (auto &node : nodes_) {
    node.visible = !node.hiddenPreview &&
                   (needle.empty() ||
                    lower(node.entity->name).find(needle) != std::string::npos);
    if (!node.hiddenPreview && !node.visible) {
      for (const auto &cell : node.paintedCells) {
        if (lower("Cell " + isoGridCellKey(cell.x, cell.y)).find(needle) !=
            std::string::npos) {
          node.visible = true;
          break;
        }
      }
    }
  }
  for (auto cursor = traversal_.rbegin(); cursor != traversal_.rend();
       ++cursor) {
    const auto &node = nodes_[*cursor];
    if (node.visible && node.parent && !nodes_[*node.parent].hiddenPreview) {
      nodes_[*node.parent].visible = true;
    }
  }
}
const EditorHierarchyIndex::Node *
EditorHierarchyIndex::find(std::string_view id) const {
  const auto found = byId_.find(id);
  return found == byId_.end() ? nullptr : &nodes_[found->second];
}
void EditorHierarchyProjection::refresh(const runtime::World &world,
                                        const nlohmann::json &document,
                                        std::uint64_t contentRevision,
                                        std::uint64_t previewRevision,
                                        std::string_view filter) {
  const bool rebuild = !index_ || world_ != &world ||
                       contentRevision_ != contentRevision ||
                       previewRevision_ != previewRevision;
  if (rebuild) {
    index_.emplace(world, document, filter);
    world_ = &world;
    contentRevision_ = contentRevision;
    previewRevision_ = previewRevision;
    ++indexBuildCount_;
    rowsDirty_ = true;
  } else if (filter_ != filter) {
    index_->filter(filter);
    rowsDirty_ = true;
  }
  filter_ = filter;
}

bool EditorHierarchyProjection::entityOpen(const std::string &id) const {
  return !closedEntities_.contains(id);
}
bool EditorHierarchyProjection::paintedCellsOpen(const std::string &id) const {
  return openPaintedGroups_.contains(id);
}
void EditorHierarchyProjection::setEntityOpen(const std::string &id,
                                              bool open) {
  const bool changed =
      open ? closedEntities_.erase(id) != 0 : closedEntities_.insert(id).second;
  rowsDirty_ = rowsDirty_ || changed;
}
void EditorHierarchyProjection::setPaintedCellsOpen(const std::string &id,
                                                    bool open) {
  const bool changed = open ? openPaintedGroups_.insert(id).second
                            : openPaintedGroups_.erase(id) != 0;
  rowsDirty_ = rowsDirty_ || changed;
}

const std::vector<EditorHierarchyProjection::Row> &
EditorHierarchyProjection::rows() {
  if (!rowsDirty_) {
    return rows_;
  }
  rows_.clear();
  std::vector<Row> pending;
  const auto &roots = index_->roots();
  for (auto cursor = roots.rbegin(); cursor != roots.rend(); ++cursor) {
    pending.push_back({.node = *cursor});
  }
  const auto needle = lower(filter_);
  while (!pending.empty()) {
    const Row row = pending.back();
    pending.pop_back();
    const auto &node = index_->node(row.node);
    if (!node.visible) {
      continue;
    }
    rows_.push_back(row);
    if (row.kind == RowKind::Entity && entityOpen(node.entity->id)) {
      if (!node.paintedCells.empty()) {
        pending.push_back({.kind = RowKind::PaintedGroup,
                           .node = row.node,
                           .depth = row.depth + 1});
      }
      for (auto child = node.children.rbegin(); child != node.children.rend();
           ++child) {
        pending.push_back({.node = *child, .depth = row.depth + 1});
      }
    } else if (row.kind == RowKind::PaintedGroup &&
               paintedCellsOpen(node.entity->id)) {
      for (std::size_t cell = node.paintedCells.size(); cell > 0; --cell) {
        const auto &value = node.paintedCells[cell - 1];
        const auto label = lower("Cell " + isoGridCellKey(value.x, value.y));
        if (needle.empty() || label.find(needle) != std::string::npos) {
          pending.push_back({.kind = RowKind::PaintedCell,
                             .node = row.node,
                             .cell = cell - 1,
                             .depth = row.depth + 1});
        }
      }
    }
  }
  rowsDirty_ = false;
  ++rowBuildCount_;
  return rows_;
}

} // namespace demi::editor
