#include "editor/EditorInspectorPanel.h"

#include "editor/EditorInspectorModel.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/ComponentRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace demi::editor {
namespace {

using runtime::ComponentFieldDescriptor;
using runtime::ComponentFieldType;
using runtime::scene_loading::ComponentDescriptor;

bool inputString(const char *label, std::string &value) {
  std::array<char, 512> buffer{};
  const std::size_t count = std::min(value.size(), buffer.size() - 1);
  std::copy_n(value.data(), count, buffer.data());
  if (!ImGui::InputText(label, buffer.data(), buffer.size()))
    return false;
  value = buffer.data();
  return true;
}

void finishEdit(EditorWorkspace &workspace) {
  if (ImGui::IsItemDeactivatedAfterEdit())
    workspace.endContinuousEdit();
}

bool commit(EditorWorkspace &workspace, const SceneValueTarget &target,
            nlohmann::json value, std::string &notice) {
  std::string error;
  const bool continuous = ImGui::IsItemActive();
  if (!workspace.editValue(target, std::move(value), continuous, error)) {
    notice = error;
    return false;
  }
  notice = "Scene modified";
  return true;
}

bool commitMany(EditorWorkspace &workspace,
                const std::vector<SceneValueTarget> &targets,
                nlohmann::json value, std::string &notice) {
  std::string error;
  if (!workspace.editValues(targets, std::move(value), error)) {
    notice = error;
    return false;
  }
  notice = "Scene modified";
  return true;
}

void drawInlineIssue(const EditorWorkspace &workspace,
                     const SceneValueTarget &target) {
  const std::string *issue = workspace.sceneDocument().issueFor(target);
  if (issue != nullptr)
    ImGui::TextColored({0.95F, 0.34F, 0.38F, 1.0F}, "%s", issue->c_str());
}

nlohmann::json defaultValue(const ComponentDescriptor &descriptor,
                            const ComponentFieldDescriptor &field) {
  return runtime::scene_loading::componentFieldDefault(descriptor, field);
}

void drawFieldHelp(const ComponentFieldDescriptor &field) {
  if (!ImGui::IsItemHovered())
    return;
  if (field.editor.help.empty() && !field.restartRequired &&
      !runtime::scene_loading::componentFieldEditorReadOnly(field))
    return;
  ImGui::BeginTooltip();
  if (!field.editor.help.empty())
    ImGui::TextWrapped("%s", field.editor.help.data());
  if (field.restartRequired)
    ImGui::TextDisabled("Requires a runtime restart.");
  if (runtime::scene_loading::componentFieldEditorReadOnly(field))
    ImGui::TextDisabled("Read-only in the generic inspector.");
  ImGui::EndTooltip();
}

bool drawReferenceString(EditorWorkspace &workspace,
                         const SceneValueTarget &target,
                         const ComponentFieldDescriptor &field,
                         const nlohmann::json &value, std::string &notice,
                         const std::vector<SceneValueTarget> *targets) {
  std::string selected = value.get<std::string>();
  bool changed = false;
  if (ImGui::BeginCombo("##value", selected.empty() ? "None" : selected.c_str())) {
    const auto choices = editorReferenceChoices(
        field.referenceKind, workspace.project().project.projectDirectory,
        workspace.sceneDocument().path(), workspace.sceneDocument().json(),
        workspace.sources());
    if (field.nullable && ImGui::Selectable("None", selected.empty())) {
      selected.clear();
      changed = true;
    }
    for (const EditorReferenceChoice &choice : choices) {
      const bool isSelected = selected == choice.id;
      if (ImGui::Selectable(choice.label.c_str(), isSelected)) {
        selected = choice.id;
        changed = true;
      }
      if (isSelected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return !changed ||
         (targets == nullptr ? commit(workspace, target, selected, notice)
                             : commitMany(workspace, *targets, selected, notice));
}

bool drawAllowedString(EditorWorkspace &workspace,
                       const SceneValueTarget &target,
                       const ComponentFieldDescriptor &field,
                       const nlohmann::json &value, std::string &notice,
                       const std::vector<SceneValueTarget> *targets) {
  std::string selected = value.get<std::string>();
  bool changed = false;
  if (ImGui::BeginCombo("##value", selected.c_str())) {
    for (const std::string_view option : field.allowedValues) {
      const bool isSelected = selected == option;
      if (ImGui::Selectable(option.data(), isSelected)) {
        selected = option;
        changed = true;
      }
      if (isSelected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return !changed ||
         (targets == nullptr ? commit(workspace, target, selected, notice)
                             : commitMany(workspace, *targets, selected, notice));
}

bool drawFieldValue(EditorWorkspace &workspace, const SceneValueTarget &target,
                    const ComponentFieldDescriptor &field,
                    const nlohmann::json &value, std::string &notice,
                    const std::vector<SceneValueTarget> *targets = nullptr,
                    const bool mixed = false) {
  (void)mixed;
  if (runtime::scene_loading::componentFieldEditorReadOnly(field)) {
    ImGui::TextWrapped("%s", value.dump().c_str());
    return true;
  }
  bool changed = false;
  nlohmann::json replacement = value;
  const float minimum =
      field.hasMinimum ? static_cast<float>(field.minimum) : 0.0F;
  const float maximum =
      field.hasMaximum ? static_cast<float>(field.maximum) : 0.0F;
  const float step = static_cast<float>(
      runtime::scene_loading::componentFieldEditorStep(field));

  switch (field.type) {
  case ComponentFieldType::Boolean: {
    bool edited = value.get<bool>();
    changed = ImGui::Checkbox("##value", &edited);
    replacement = edited;
    break;
  }
  case ComponentFieldType::Integer: {
    int edited = value.get<int>();
    changed =
        ImGui::DragInt("##value", &edited, step,
                       field.hasMinimum ? static_cast<int>(field.minimum) : 0,
                       field.hasMaximum ? static_cast<int>(field.maximum) : 0);
    replacement = edited;
    break;
  }
  case ComponentFieldType::Number: {
    float edited = value.get<float>();
    changed =
        ImGui::DragFloat("##value", &edited, step, minimum, maximum, "%.3f");
    replacement = edited;
    break;
  }
  case ComponentFieldType::String: {
    if (field.referenceKind != runtime::ComponentReferenceKind::None) {
      const bool accepted =
          drawReferenceString(workspace, target, field, value, notice, targets);
      return accepted;
    }
    if (!field.allowedValues.empty()) {
      const bool accepted =
          drawAllowedString(workspace, target, field, value, notice, targets);
      return accepted;
    }
    std::string edited = value.get<std::string>();
    changed = inputString("##value", edited);
    replacement = std::move(edited);
    break;
  }
  case ComponentFieldType::Vec2:
  case ComponentFieldType::Vec3:
  case ComponentFieldType::Color: {
    const int count = field.type == ComponentFieldType::Vec2   ? 2
                      : field.type == ComponentFieldType::Vec3 ? 3
                                                               : 4;
    std::array<float, 4> edited{};
    for (int index = 0; index < count; ++index)
      edited[index] = value[index].get<float>();
    if (field.type == ComponentFieldType::Vec2)
      changed = ImGui::DragFloat2("##value", edited.data(), step, minimum,
                                  maximum, "%.3f");
    else if (field.type == ComponentFieldType::Vec3)
      changed = ImGui::DragFloat3("##value", edited.data(), step, minimum,
                                  maximum, "%.3f");
    else
      changed = ImGui::ColorEdit4("##value", edited.data());
    replacement = nlohmann::json::array();
    for (int index = 0; index < count; ++index)
      replacement.push_back(edited[index]);
    break;
  }
  case ComponentFieldType::Object:
  case ComponentFieldType::Vec2Array:
  case ComponentFieldType::Vec3Array:
    ImGui::TextWrapped("%s", value.dump().c_str());
    return true;
  }

  const bool accepted = !changed ||
                        (targets == nullptr
                             ? commit(workspace, target, replacement, notice)
                             : commitMany(workspace, *targets, replacement,
                                          notice));
  finishEdit(workspace);
  return accepted;
}

void drawComponentFields(EditorWorkspace &workspace,
                         const std::string_view entityId,
                         const std::string &componentName,
                         const nlohmann::json &component,
                         const ComponentDescriptor &descriptor,
                         std::string &notice) {
  for (const ComponentFieldDescriptor &field : descriptor.fields) {
    if (!field.editorVisible)
      continue;
    const auto value = component.find(field.name);
    const SceneValueTarget target{.entityId = std::string(entityId),
                                  .component = componentName,
                                  .field = std::string(field.name)};
    if (value == component.end()) {
      if (!field.required) {
        ImGui::PushID(field.name.data());
        const std::string label =
            runtime::scene_loading::componentFieldEditorLabel(field);
        ImGui::TextDisabled("%s (default)", label.c_str());
        drawFieldHelp(field);
        ImGui::SameLine(180.0F);
        if (ImGui::SmallButton("Author"))
          (void)commit(workspace, target, defaultValue(descriptor, field),
                       notice);
        drawInlineIssue(workspace, target);
        ImGui::PopID();
      }
      continue;
    }
    ImGui::PushID(field.name.data());
    ImGui::AlignTextToFramePadding();
    const std::string label =
        runtime::scene_loading::componentFieldEditorLabel(field);
    ImGui::TextDisabled("%s", label.c_str());
    drawFieldHelp(field);
    ImGui::SameLine(112.0F);
    ImGui::SetNextItemWidth(field.required ? -1.0F : -52.0F);
    (void)drawFieldValue(workspace, target, field, *value, notice);
    if (!field.required) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Reset")) {
        std::string error;
        notice = workspace.removeValue(target, error)
                     ? "Optional field reset to its default"
                     : error;
      }
    }
    drawInlineIssue(workspace, target);
    ImGui::PopID();
  }
}

void drawMultiSelection(EditorWorkspace &workspace, std::string &notice) {
  ImGui::Text("%zu entities selected", workspace.selectedEntityIds().size());
  ImGui::TextDisabled("Only authored fields common to every selection are shown.");
  ImGui::Separator();
  std::string currentComponent;
  for (const EditorCommonField &common : editorCommonFields(
           workspace.sceneDocument().json(), workspace.selectedEntityIds())) {
    const std::string componentName(common.component->name);
    if (currentComponent != componentName) {
      currentComponent = componentName;
      ImGui::Spacing();
      ImGui::TextUnformatted(common.component->editor.displayName.data());
      ImGui::Separator();
    }
    ImGui::PushID(componentName.c_str());
    ImGui::PushID(common.field->name.data());
    const std::string label =
        runtime::scene_loading::componentFieldEditorLabel(*common.field);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label.c_str());
    drawFieldHelp(*common.field);
    ImGui::SameLine(112.0F);
    ImGui::SetNextItemWidth(-1.0F);
    (void)drawFieldValue(workspace, common.targets.front(), *common.field,
                         common.value, notice, &common.targets, common.mixed);
    if (common.mixed) {
      ImGui::SameLine();
      ImGui::TextDisabled("Mixed");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The selected entities have different values. "
                          "Editing this field applies one value to all of them.");
    }
    ImGui::PopID();
    ImGui::PopID();
  }
}

void drawGenericComponent(const nlohmann::json &component) {
  for (const auto &[name, value] : component.items()) {
    ImGui::TextDisabled("%s", name.c_str());
    ImGui::SameLine(112.0F);
    const std::string rendered =
        value.is_string() ? value.get<std::string>() : value.dump();
    ImGui::TextWrapped("%s", rendered.c_str());
  }
}

void drawEntityHeader(EditorWorkspace &workspace, const nlohmann::json &entity,
                      std::string &notice) {
  const std::string id = entity.value("id", std::string{});
  std::string name = entity.value("name", id);
  ImGui::SetNextItemWidth(-1.0F);
  if (inputString("##entity-name", name))
    (void)commit(workspace, {.entityId = id, .field = "name"}, name, notice);
  finishEdit(workspace);
  drawInlineIssue(workspace, {.entityId = id, .field = "name"});
  ImGui::TextDisabled("%s", id.c_str());

  bool enabled = entity.value("enabled", true);
  if (entity.contains("enabled")) {
    if (ImGui::Checkbox("Enabled", &enabled))
      (void)commit(workspace, {.entityId = id, .field = "enabled"}, enabled,
                   notice);
    ImGui::SameLine();
    if (ImGui::SmallButton("Use default##enabled")) {
      std::string error;
      notice = workspace.removeValue({.entityId = id, .field = "enabled"},
                                     error)
                   ? "Enabled reset to its default"
                   : error;
    }
  } else {
    ImGui::TextDisabled("Enabled (default: true)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Author##enabled"))
      (void)commit(workspace, {.entityId = id, .field = "enabled"}, true,
                   notice);
  }
  drawInlineIssue(workspace, {.entityId = id, .field = "enabled"});

  if (entity.contains("layer")) {
    std::string layer = entity.value("layer", std::string{});
    ImGui::SetNextItemWidth(-1.0F);
    if (inputString("Layer", layer))
      (void)commit(workspace, {.entityId = id, .field = "layer"}, layer,
                   notice);
    finishEdit(workspace);
    ImGui::SameLine();
    if (ImGui::SmallButton("Use default##layer")) {
      std::string error;
      notice = workspace.removeValue({.entityId = id, .field = "layer"}, error)
                   ? "Layer reset to its default"
                   : error;
    }
  } else {
    ImGui::TextDisabled("Layer: Default");
    ImGui::SameLine();
    if (ImGui::SmallButton("Author##layer"))
      (void)commit(workspace, {.entityId = id, .field = "layer"}, "Default",
                   notice);
  }
  drawInlineIssue(workspace, {.entityId = id, .field = "layer"});
  if (const auto &issue = workspace.sceneDocument().issue();
      issue.has_value() && issue->target.entityId == id &&
      issue->target.field.empty())
    ImGui::TextColored({0.95F, 0.34F, 0.38F, 1.0F}, "%s",
                       issue->message.c_str());
  ImGui::Separator();
}

} // namespace

void drawInspectorPanel(EditorWorkspace &workspace, const ImVec2 position,
                        const ImVec2 size, std::string &notice) {
  beginEditorPanel("Inspector", position, size);
  editorSectionTitle("INSPECTOR",
                     workspace.sceneDocument().isDirty() ? "Unsaved" : nullptr);
  const runtime::Entity *selected = workspace.selectedEntity();
  if (selected == nullptr) {
    ImGui::TextDisabled("Select an entity to inspect its authored data.");
    ImGui::End();
    return;
  }
  if (workspace.selectedEntityIds().size() > 1) {
    drawMultiSelection(workspace, notice);
    ImGui::End();
    return;
  }

  const nlohmann::json *entity = workspace.sceneDocument().entity(selected->id);
  if (entity == nullptr) {
    ImGui::TextDisabled("This entity comes from an expanded prefab.");
    ImGui::TextWrapped("Open the prefab source to edit its authored fields.");
    ImGui::End();
    return;
  }
  drawEntityHeader(workspace, *entity, notice);

  const auto components = entity->find("components");
  if (components != entity->end() && components->is_object()) {
    for (const auto &[name, component] : components->items()) {
      if (!component.is_object())
        continue;
      const ComponentDescriptor *descriptor =
          runtime::scene_loading::findComponentDescriptor(name);
      const char *title = descriptor == nullptr
                              ? name.c_str()
                              : descriptor->editor.displayName.data();
      if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
        continue;
      ImGui::PushID(name.c_str());
      drawInlineIssue(workspace,
                      {.entityId = selected->id, .component = name});
      if (ImGui::SmallButton("Remove")) {
        std::string error;
        const bool removed = workspace.removeComponent(selected->id, name, error);
        notice = removed ? "Component removed" : error;
        ImGui::PopID();
        ImGui::End();
        return;
      }
      if (descriptor == nullptr || descriptor->fields.empty())
        drawGenericComponent(component);
      else
        drawComponentFields(workspace, selected->id, name, component,
                            *descriptor, notice);
      ImGui::PopID();
    }
  }

  ImGui::Spacing();
  ImGui::SetNextItemWidth(-1.0F);
  if (ImGui::BeginCombo("##add-component", "Add Component")) {
    std::string_view category;
    for (const EditorComponentChoice &choice : editorComponentChoices(*entity)) {
      const ComponentDescriptor &descriptor = *choice.descriptor;
      if (category != descriptor.editor.category) {
        category = descriptor.editor.category;
        ImGui::SeparatorText(category.data());
      }
      if (!choice.compatible)
        ImGui::BeginDisabled();
      if (ImGui::Selectable(descriptor.editor.displayName.data())) {
        std::string error;
        const bool added =
            workspace.addComponent(selected->id, descriptor.name, error);
        notice = added ? "Component added" : error;
        if (!choice.compatible)
          ImGui::EndDisabled();
        ImGui::EndCombo();
        ImGui::End();
        return;
      }
      if (!choice.compatible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", choice.incompatibility.c_str());
      if (!choice.compatible)
        ImGui::EndDisabled();
    }
    ImGui::EndCombo();
  }
  ImGui::End();
}

} // namespace demi::editor
