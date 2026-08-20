#include "editor/EditorHierarchyPanel.h"

#include "editor/EditorChrome.h"
#include "editor/EditorIsoGridCell.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace demi::editor {
namespace {

constexpr const char *EntityPayload = "DEMI_SCENE_ENTITY";

bool containsCaseInsensitive(const std::string_view value,
                             const std::string_view filter) {
  if (filter.empty())
    return true;
  const auto lower = [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  };
  std::string haystack(value);
  std::string needle(filter);
  std::ranges::transform(haystack, haystack.begin(), lower);
  std::ranges::transform(needle, needle.begin(), lower);
  return haystack.find(needle) != std::string::npos;
}

std::string_view entityParent(const runtime::Entity &entity) {
  if (const auto *transform = entity.component<runtime::Transform3DComponent>())
    return transform->parent;
  if (const auto *transform = entity.component<runtime::Transform2DComponent>())
    return transform->parent;
  if (const auto *transform =
          entity.component<runtime::IsoTransformComponent>())
    return transform->parent;
  return {};
}

bool hasChildren(const runtime::World &world, const std::string_view parent) {
  const runtime::Entity *entity =
      runtime::findEntity(world, std::string(parent));
  const auto *grid = entity == nullptr
                         ? nullptr
                         : entity->component<runtime::IsoGridComponent>();
  return (grid != nullptr && !grid->cellTextures.empty()) ||
         std::ranges::any_of(world.entities, [parent](const auto &candidate) {
           return entityParent(candidate) == parent;
         });
}

struct HierarchyAction {
  enum class Kind {
    Create,
    Duplicate,
    Reparent,
    Delete,
    DeleteSelection,
    DeleteGridCell
  };
  Kind kind = Kind::Create;
  std::string entityId;
  std::optional<std::string> parentId;
  std::optional<EditorIsoGridCell> gridCell;
};

void acceptEntityDrop(std::optional<HierarchyAction> &pending,
                      std::optional<std::string> parentId) {
  if (!ImGui::BeginDragDropTarget())
    return;
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EntityPayload);
      payload != nullptr && payload->Data != nullptr && payload->DataSize > 1) {
    const auto *data = static_cast<const char *>(payload->Data);
    std::string entityId(data, static_cast<std::size_t>(payload->DataSize - 1));
    if (!parentId.has_value() || entityId != *parentId) {
      pending = HierarchyAction{.kind = HierarchyAction::Kind::Reparent,
                                .entityId = std::move(entityId),
                                .parentId = std::move(parentId)};
    }
  }
  ImGui::EndDragDropTarget();
}

bool applyHierarchyAction(EditorWorkspace &workspace,
                          const HierarchyAction &action, std::string &notice) {
  std::string error;
  bool succeeded = false;
  switch (action.kind) {
  case HierarchyAction::Kind::Create:
    succeeded = workspace.createEntity(error, action.parentId);
    notice = succeeded ? "Entity created" : error;
    break;
  case HierarchyAction::Kind::Duplicate:
    succeeded = workspace.duplicateEntity(action.entityId, error);
    notice = succeeded ? "Entity subtree duplicated" : error;
    break;
  case HierarchyAction::Kind::Reparent:
    succeeded =
        workspace.reparentEntity(action.entityId, action.parentId, error);
    notice = succeeded
                 ? (action.parentId.has_value() ? "Entity reparented"
                                                : "Entity moved to scene root")
                 : error;
    break;
  case HierarchyAction::Kind::Delete:
    succeeded = workspace.deleteEntity(action.entityId, error);
    notice = succeeded ? "Entity subtree deleted" : error;
    break;
  case HierarchyAction::Kind::DeleteSelection:
    succeeded = workspace.deleteEntities(workspace.selectedEntityIds(), error);
    notice = succeeded ? "Selected entity subtrees deleted" : error;
    break;
  case HierarchyAction::Kind::DeleteGridCell:
    if (action.gridCell)
      workspace.selectIsoGridCell(*action.gridCell);
    succeeded = workspace.deleteSelectedIsoGridCell(error);
    notice = succeeded ? "Painted cell cleared" : error;
    break;
  }
  return succeeded;
}

void drawPaintedCells(EditorWorkspace &workspace, const runtime::Entity &entity,
                      const std::string_view filter,
                      std::optional<HierarchyAction> &pending) {
  const std::vector<EditorIsoGridCell> cells =
      paintedIsoGridCells(workspace.project().world, entity.id);
  if (cells.empty())
    return;
  const std::string groupLabel =
      "Painted Cells (" + std::to_string(cells.size()) + ")";
  if (!ImGui::TreeNodeEx(("##painted-" + entity.id).c_str(),
                         ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
                         groupLabel.c_str()))
    return;
  for (const EditorIsoGridCell &cell : cells) {
    const std::string cellLabel = "Cell " + isoGridCellKey(cell.x, cell.y);
    if (!containsCaseInsensitive(cellLabel, filter))
      continue;
    const bool selected = workspace.selectedIsoGridCell() == cell;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected)
      flags |= ImGuiTreeNodeFlags_Selected;
    const std::string id =
        "##cell-" + entity.id + "-" + isoGridCellKey(cell.x, cell.y);
    ImGui::TreeNodeEx(id.c_str(), flags, "%s", cellLabel.c_str());
    if (ImGui::IsItemClicked())
      workspace.selectIsoGridCell(cell);
    if (ImGui::BeginPopupContextItem(id.c_str())) {
      if (ImGui::MenuItem("Clear painted cell"))
        pending = HierarchyAction{.kind = HierarchyAction::Kind::DeleteGridCell,
                                  .gridCell = cell};
      ImGui::EndPopup();
    }
  }
  ImGui::TreePop();
}

void drawEntityNode(EditorWorkspace &workspace, const runtime::Entity &entity,
                    const runtime::World &world, const std::string_view filter,
                    std::optional<HierarchyAction> &pending,
                    std::string &notice) {
  const bool childMatch =
      std::ranges::any_of(world.entities, [&](const auto &candidate) {
        return entityParent(candidate) == entity.id &&
               containsCaseInsensitive(candidate.name, filter);
      });
  const bool paintedCellMatch = std::ranges::any_of(
      paintedIsoGridCells(world, entity.id),
      [&](const EditorIsoGridCell &cell) {
        return containsCaseInsensitive("Cell " + isoGridCellKey(cell.x, cell.y),
                                       filter);
      });
  if (!containsCaseInsensitive(entity.name, filter) && !childMatch &&
      !paintedCellMatch)
    return;

  const bool entityHasChildren = hasChildren(world, entity.id);
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_SpanAvailWidth |
                             ImGuiTreeNodeFlags_DefaultOpen;
  if (!entityHasChildren)
    flags |= ImGuiTreeNodeFlags_Leaf;
  if (workspace.isEntitySelected(entity.id))
    flags |= ImGuiTreeNodeFlags_Selected;
  if (!entity.enabled)
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  const bool open =
      ImGui::TreeNodeEx(entity.id.c_str(), flags, "%s", entity.name.c_str());
  if (!entity.enabled)
    ImGui::PopStyleColor();
  const ImVec2 rowMin = ImGui::GetItemRectMin();
  const ImVec2 rowMax = ImGui::GetItemRectMax();
  const ImVec2 visibilityCenter{rowMax.x - 13.0F, (rowMin.y + rowMax.y) * 0.5F};
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const bool overVisibility = mouse.x >= visibilityCenter.x - 10.0F &&
                              mouse.x <= visibilityCenter.x + 10.0F &&
                              mouse.y >= rowMin.y && mouse.y <= rowMax.y;
  drawEditorGlyph(
      *ImGui::GetWindowDrawList(), EditorIcon::Eye, visibilityCenter,
      entity.enabled ? IM_COL32(169, 173, 183, 255) : IM_COL32(85, 88, 98, 255),
      0.72F);
  if (overVisibility)
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (overVisibility && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    std::string error;
    const bool changed =
        workspace.editValue({.entityId = entity.id, .field = "enabled"},
                            !entity.enabled, false, error);
    notice = changed ? (entity.enabled ? "Entity disabled" : "Entity enabled")
                     : error;
  } else if (ImGui::IsItemClicked()) {
    if (ImGui::GetIO().KeyCtrl)
      workspace.toggleEntitySelection(entity.id);
    else
      workspace.selectEntity(entity.id);
  }

  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(EntityPayload, entity.id.c_str(),
                              entity.id.size() + 1);
    ImGui::TextUnformatted(entity.name.c_str());
    ImGui::TextDisabled("Drop on an entity to parent, or Scene for root");
    ImGui::EndDragDropSource();
  }
  acceptEntityDrop(pending, entity.id);

  if (!pending.has_value() && ImGui::BeginPopupContextItem(entity.id.c_str())) {
    if (ImGui::MenuItem("Duplicate subtree"))
      pending = HierarchyAction{.kind = HierarchyAction::Kind::Duplicate,
                                .entityId = entity.id};
    if (!entityParent(entity).empty() && ImGui::MenuItem("Move to scene root"))
      pending = HierarchyAction{.kind = HierarchyAction::Kind::Reparent,
                                .entityId = entity.id};
    if (ImGui::MenuItem(entityHasChildren ? "Delete subtree" : "Delete"))
      pending = HierarchyAction{.kind = HierarchyAction::Kind::Delete,
                                .entityId = entity.id};
    ImGui::EndPopup();
  }

  if (open) {
    for (const runtime::Entity &candidate : world.entities)
      if (entityParent(candidate) == entity.id)
        drawEntityNode(workspace, candidate, world, filter, pending, notice);
    drawPaintedCells(workspace, entity, filter, pending);
    ImGui::TreePop();
  }
}

} // namespace

void EditorHierarchyPanel::draw(EditorWorkspace &workspace,
                                const ImVec2 position, const ImVec2 size,
                                std::string &notice) {
  beginEditorPanel("Hierarchy", position, size);
  editorSectionTitle("Scene", workspace.project().world.name.c_str());
  std::optional<HierarchyAction> pending;
  ImGui::SetNextItemWidth(-1.0F);
  ImGui::InputTextWithHint("##hierarchy-search", "Search entities",
                           filter_.data(), filter_.size());
  ImGui::Spacing();
  if (ImGui::SmallButton("+ Add Entity"))
    pending = HierarchyAction{.kind = HierarchyAction::Kind::Create};
  ImGui::Spacing();

  const bool sceneOpen =
      ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen |
                                     ImGuiTreeNodeFlags_SpanAvailWidth);
  acceptEntityDrop(pending, std::nullopt);
  if (sceneOpen) {
    for (const runtime::Entity &entity : workspace.project().world.entities)
      if (entityParent(entity).empty())
        drawEntityNode(workspace, entity, workspace.project().world,
                       filter_.data(), pending, notice);
    ImGui::TreePop();
  }

  const bool handlesKeyboard = ImGui::IsWindowFocused() &&
                               !ImGui::GetIO().WantTextInput &&
                               !workspace.selectedEntityId().empty();
  if (handlesKeyboard) {
    const std::string selected(workspace.selectedEntityId());
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
      const nlohmann::json *entity = workspace.sceneDocument().entity(selected);
      const std::string name =
          entity == nullptr ? selected : entity->value("name", selected);
      rename_.fill('\0');
      std::copy_n(name.data(), std::min(name.size(), rename_.size() - 1),
                  rename_.data());
      renamingEntityId_ = selected;
      ImGui::OpenPopup("Rename Entity");
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
      if (workspace.selectedIsoGridCell())
        pending = HierarchyAction{.kind = HierarchyAction::Kind::DeleteGridCell,
                                  .gridCell = workspace.selectedIsoGridCell()};
      else
        pending =
            HierarchyAction{.kind = HierarchyAction::Kind::DeleteSelection,
                            .entityId = selected};
    } else if (ImGui::GetIO().KeyCtrl &&
               ImGui::IsKeyPressed(ImGuiKey_D, false)) {
      pending = HierarchyAction{.kind = HierarchyAction::Kind::Duplicate,
                                .entityId = selected};
    } else if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift &&
               ImGui::IsKeyPressed(ImGuiKey_N, false)) {
      pending = HierarchyAction{.kind = HierarchyAction::Kind::Create,
                                .parentId = selected};
    } else if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
      if (workspace.viewDimension() ==
          EditorSceneViewDimension::TwoDimensional) {
        if (workspace.selectedIsoGridCell())
          (void)workspace.sceneView2D().frameGridCell(
              workspace.project().world, *workspace.selectedIsoGridCell());
        else
          (void)workspace.sceneView2D().frameEntity(workspace.project().world,
                                                    selected);
      } else {
        (void)workspace.sceneView().frameEntity(workspace.project().world,
                                                selected);
      }
      notice = "Framed selected entity";
    }
  }

  if (ImGui::BeginPopupModal("Rename Entity", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetKeyboardFocusHere();
    const bool submitted =
        ImGui::InputText("Name", rename_.data(), rename_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    if ((submitted || ImGui::Button("Rename")) &&
        renamingEntityId_.has_value()) {
      std::string error;
      notice =
          workspace.editValue({.entityId = *renamingEntityId_, .field = "name"},
                              std::string(rename_.data()), false, error)
              ? "Entity renamed"
              : error;
      renamingEntityId_.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      renamingEntityId_.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::End();

  if (pending.has_value())
    (void)applyHierarchyAction(workspace, *pending, notice);
}

} // namespace demi::editor
