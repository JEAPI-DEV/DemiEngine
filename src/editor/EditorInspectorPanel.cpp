#include "editor/EditorInspectorPanel.h"

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

bool drawAllowedString(EditorWorkspace &workspace,
                       const SceneValueTarget &target,
                       const ComponentFieldDescriptor &field,
                       const nlohmann::json &value, std::string &notice) {
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
  return !changed || commit(workspace, target, selected, notice);
}

bool drawFieldValue(EditorWorkspace &workspace, const SceneValueTarget &target,
                    const ComponentFieldDescriptor &field,
                    const nlohmann::json &value, std::string &notice) {
  bool changed = false;
  nlohmann::json replacement = value;
  const float minimum =
      field.hasMinimum ? static_cast<float>(field.minimum) : 0.0F;
  const float maximum =
      field.hasMaximum ? static_cast<float>(field.maximum) : 0.0F;

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
        ImGui::DragInt("##value", &edited, 1.0F,
                       field.hasMinimum ? static_cast<int>(field.minimum) : 0,
                       field.hasMaximum ? static_cast<int>(field.maximum) : 0);
    replacement = edited;
    break;
  }
  case ComponentFieldType::Number: {
    float edited = value.get<float>();
    changed =
        ImGui::DragFloat("##value", &edited, 0.05F, minimum, maximum, "%.3f");
    replacement = edited;
    break;
  }
  case ComponentFieldType::String: {
    if (!field.allowedValues.empty())
      return drawAllowedString(workspace, target, field, value, notice);
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
      changed = ImGui::DragFloat2("##value", edited.data(), 0.05F, minimum,
                                  maximum, "%.3f");
    else if (field.type == ComponentFieldType::Vec3)
      changed = ImGui::DragFloat3("##value", edited.data(), 0.05F, minimum,
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

  const bool accepted =
      !changed || commit(workspace, target, replacement, notice);
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
    if (value == component.end())
      continue;
    ImGui::PushID(field.name.data());
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", field.name.data());
    ImGui::SameLine(112.0F);
    ImGui::SetNextItemWidth(-1.0F);
    (void)drawFieldValue(workspace,
                         {.entityId = std::string(entityId),
                          .component = componentName,
                          .field = std::string(field.name)},
                         field, *value, notice);
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
  ImGui::TextDisabled("%s", id.c_str());

  bool enabled = entity.value("enabled", true);
  if (entity.contains("enabled")) {
    if (ImGui::Checkbox("Enabled", &enabled))
      (void)commit(workspace, {.entityId = id, .field = "enabled"}, enabled,
                   notice);
  } else {
    ImGui::TextDisabled("Enabled (default)");
  }

  if (entity.contains("layer")) {
    std::string layer = entity.value("layer", std::string{});
    ImGui::SetNextItemWidth(-1.0F);
    if (inputString("Layer", layer))
      (void)commit(workspace, {.entityId = id, .field = "layer"}, layer,
                   notice);
    finishEdit(workspace);
  } else {
    ImGui::TextDisabled("Layer: Default");
  }
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
      if (descriptor == nullptr || descriptor->fields.empty())
        drawGenericComponent(component);
      else
        drawComponentFields(workspace, selected->id, name, component,
                            *descriptor, notice);
      ImGui::PopID();
    }
  }
  ImGui::Spacing();
  disabledEditorButton("+ Add Component",
                       "Component add/remove commands are not implemented yet.",
                       {-1.0F, 0.0F});
  ImGui::End();
}

} // namespace demi::editor
