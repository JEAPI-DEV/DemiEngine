#include "editor/EditorHierarchyPanel.h"

#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

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
  return {};
}

bool hasChildren(const runtime::World &world, const std::string_view parent) {
  return std::ranges::any_of(world.entities, [parent](const auto &candidate) {
    return entityParent(candidate) == parent;
  });
}

struct HierarchyAction {
  enum class Kind { Create, Duplicate, Reparent, Delete };
  Kind kind = Kind::Create;
  std::string entityId;
  std::optional<std::string> parentId;
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
    succeeded = workspace.createEntity(error);
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
  }
  return succeeded;
}

void drawEntityNode(EditorWorkspace &workspace, const runtime::Entity &entity,
                    const runtime::World &world, const std::string_view filter,
                    std::optional<HierarchyAction> &pending) {
  const bool childMatch =
      std::ranges::any_of(world.entities, [&](const auto &candidate) {
        return entityParent(candidate) == entity.id &&
               containsCaseInsensitive(candidate.name, filter);
      });
  if (!containsCaseInsensitive(entity.name, filter) && !childMatch)
    return;

  const bool entityHasChildren = hasChildren(world, entity.id);
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_SpanAvailWidth |
                             ImGuiTreeNodeFlags_DefaultOpen;
  if (!entityHasChildren)
    flags |= ImGuiTreeNodeFlags_Leaf;
  if (workspace.selectedEntityId() == entity.id)
    flags |= ImGuiTreeNodeFlags_Selected;
  if (!entity.enabled)
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  const bool open =
      ImGui::TreeNodeEx(entity.id.c_str(), flags, "%s", entity.name.c_str());
  if (!entity.enabled)
    ImGui::PopStyleColor();
  if (ImGui::IsItemClicked())
    workspace.selectEntity(entity.id);

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
        drawEntityNode(workspace, candidate, world, filter, pending);
    ImGui::TreePop();
  }
}

} // namespace

void EditorHierarchyPanel::draw(EditorWorkspace &workspace,
                                const ImVec2 position, const ImVec2 size,
                                std::string &notice) {
  beginEditorPanel("Hierarchy", position, size);
  editorSectionTitle("SCENE", workspace.project().world.name.c_str());
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
                       filter_.data(), pending);
    ImGui::TreePop();
  }
  ImGui::End();

  if (pending.has_value())
    (void)applyHierarchyAction(workspace, *pending, notice);
}

} // namespace demi::editor
