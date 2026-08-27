#include "editor/EditorInspectorPanel.h"

#include "editor/EditorChrome.h"
#include "editor/EditorInspectorModel.h"
#include "editor/EditorIsoGridInspector.h"
#include "editor/EditorLuaComponentMetadata.h"
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

const EditorLuaComponentMetadata *
scriptMetadata(const EditorLuaComponentCatalog &catalog,
               const nlohmann::json &luaScriptComponent) {
  const std::string module = luaScriptComponent.value("module", "");
  const auto found = std::ranges::find(catalog.components, module,
                                       &EditorLuaComponentMetadata::module);
  return found == catalog.components.end() ? nullptr : &*found;
}

bool inputString(const char *label, std::string &value) {
  std::array<char, 512> buffer{};
  const std::size_t count = std::min(value.size(), buffer.size() - 1);
  std::copy_n(value.data(), count, buffer.data());
  if (!ImGui::InputText(label, buffer.data(), buffer.size()))
    return false;
  value = buffer.data();
  return true;
}

template <typename Value>
void clampNumericValue(Value &value, const ComponentFieldDescriptor &field) {
  if (field.hasMinimum)
    value = std::max(value, static_cast<Value>(field.minimum));
  if (field.hasMaximum)
    value = std::min(value, static_cast<Value>(field.maximum));
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

void drawScriptProperties(EditorWorkspace &workspace,
                          const std::string_view entityId,
                          const nlohmann::json &component,
                          const EditorLuaComponentMetadata &metadata,
                          std::string &notice) {
  if (!metadata.description.empty())
    ImGui::TextDisabled("%s", metadata.description.c_str());
  ImGui::TextDisabled("Module");
  ImGui::SameLine(112.0F);
  ImGui::TextUnformatted(metadata.module.c_str());
  nlohmann::json properties =
      component.value("properties", nlohmann::json::object());
  for (const auto &[name, definition] : metadata.propertySchema.items()) {
    if (!definition.is_object())
      continue;
    nlohmann::json value = properties.contains(name)
                               ? properties[name]
                               : definition.value("default", nlohmann::json{});
    const std::string type = definition.value("type", "");
    const std::string label = definition.value("label", name);
    ImGui::PushID(name.c_str());
    bool changed = false;
    if (type == "boolean" && value.is_boolean()) {
      bool edited = value.get<bool>();
      changed = ImGui::Checkbox(label.c_str(), &edited);
      value = edited;
    } else if (type == "number" && value.is_number()) {
      double edited = value.get<double>();
      changed = ImGui::InputDouble(label.c_str(), &edited);
      if (definition.contains("minimum"))
        edited = std::max(edited, definition["minimum"].get<double>());
      if (definition.contains("maximum"))
        edited = std::min(edited, definition["maximum"].get<double>());
      value = edited;
    } else if (type == "integer" && value.is_number_integer()) {
      int edited = value.get<int>();
      changed = ImGui::InputInt(label.c_str(), &edited);
      value = edited;
    } else if ((type == "string" || type == "asset" || type == "entity") &&
               value.is_string()) {
      std::string edited = value.get<std::string>();
      changed = inputString(label.c_str(), edited);
      value = std::move(edited);
    } else if (type == "enum" && value.is_string() &&
               definition.contains("values") &&
               definition["values"].is_array()) {
      std::string edited = value.get<std::string>();
      if (ImGui::BeginCombo(label.c_str(), edited.c_str())) {
        for (const nlohmann::json &choice : definition["values"])
          if (choice.is_string() &&
              ImGui::Selectable(choice.get_ref<const std::string &>().c_str(),
                                choice == edited)) {
            edited = choice.get<std::string>();
            changed = true;
          }
        ImGui::EndCombo();
      }
      value = std::move(edited);
    } else if ((type == "vec2" || type == "vec3" || type == "color") &&
               value.is_array()) {
      const std::size_t count = type == "vec2" ? 2U : type == "vec3" ? 3U : 4U;
      std::array<float, 4> edited{};
      if (value.size() == count &&
          std::ranges::all_of(
              value, [](const auto &item) { return item.is_number(); })) {
        for (std::size_t index = 0; index < count; ++index)
          edited[index] = value[index].get<float>();
        changed =
            type == "vec2"   ? ImGui::InputFloat2(label.c_str(), edited.data())
            : type == "vec3" ? ImGui::InputFloat3(label.c_str(), edited.data())
                             : ImGui::ColorEdit4(label.c_str(), edited.data());
        value = nlohmann::json::array();
        for (std::size_t index = 0; index < count; ++index)
          value.push_back(edited[index]);
      }
    } else {
      ImGui::TextDisabled("%s: %s", label.c_str(), value.dump().c_str());
    }
    if (changed) {
      properties[name] = std::move(value);
      commit(workspace,
             {.entityId = std::string(entityId),
              .component = "LuaScript",
              .field = "properties"},
             std::move(properties), notice);
      finishEdit(workspace);
    }
    ImGui::PopID();
  }
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
  if (ImGui::BeginCombo("##value",
                        selected.empty() ? "None" : selected.c_str())) {
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
  return !changed || (targets == nullptr
                          ? commit(workspace, target, selected, notice)
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
  return !changed || (targets == nullptr
                          ? commit(workspace, target, selected, notice)
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
  switch (field.type) {
  case ComponentFieldType::Boolean: {
    bool edited = value.get<bool>();
    changed = ImGui::Checkbox("##value", &edited);
    replacement = edited;
    break;
  }
  case ComponentFieldType::Integer: {
    int edited = value.get<int>();
    changed = ImGui::InputInt("##value", &edited, 0);
    if (changed)
      clampNumericValue(edited, field);
    replacement = edited;
    break;
  }
  case ComponentFieldType::Number: {
    float edited = value.get<float>();
    changed = ImGui::InputFloat("##value", &edited, 0.0F, 0.0F, "%.3f");
    if (changed)
      clampNumericValue(edited, field);
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
      changed = ImGui::InputFloat2("##value", edited.data(), "%.3f");
    else if (field.type == ComponentFieldType::Vec3)
      changed = ImGui::InputFloat3("##value", edited.data(), "%.3f");
    else
      changed = ImGui::ColorEdit4("##value", edited.data());
    if (changed && field.type != ComponentFieldType::Color)
      for (int index = 0; index < count; ++index)
        clampNumericValue(edited[index], field);
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
      !changed || (targets == nullptr
                       ? commit(workspace, target, replacement, notice)
                       : commitMany(workspace, *targets, replacement, notice));
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
  ImGui::TextDisabled(
      "Only authored fields common to every selection are shown.");
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
        ImGui::SetTooltip(
            "The selected entities have different values. "
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
      notice =
          workspace.removeValue({.entityId = id, .field = "enabled"}, error)
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
                        const ImVec2 size, EditorInspectorPanelState &state,
                        std::string &notice) {
  beginEditorPanel("Inspector", position, size);
  (void)editorStageTab("Inspector", true, {76.0F, 25.0F});
  ImGui::SameLine(0.0F, 2.0F);
  ImGui::BeginDisabled();
  (void)editorStageTab("Lighting", false, {70.0F, 25.0F});
  ImGui::EndDisabled();
  if (workspace.sceneDocument().isDirty()) {
    ImGui::SameLine(size.x - 68.0F);
    ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "Unsaved");
  }
  ImGui::Separator();
  if (drawIsoGridCellInspector(workspace, notice)) {
    ImGui::End();
    return;
  }
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
  const EditorLuaComponentCatalog luaComponents = discoverEditorLuaComponents(
      workspace.project().project.projectDirectory, workspace.sources());

  const auto components = entity->find("components");
  if (components != entity->end() && components->is_object()) {
    for (const auto &[name, component] : components->items()) {
      if (!component.is_object())
        continue;
      const ComponentDescriptor *descriptor =
          runtime::scene_loading::findComponentDescriptor(name);
      const EditorLuaComponentMetadata *luaMetadata =
          name == "LuaScript" ? scriptMetadata(luaComponents, component)
                              : nullptr;
      const char *title =
          luaMetadata != nullptr  ? luaMetadata->displayName.c_str()
          : descriptor == nullptr ? name.c_str()
                                  : descriptor->editor.displayName.data();
      ImGui::PushID(name.c_str());
      ImGui::PushStyleColor(ImGuiCol_Header, {0.115F, 0.119F, 0.139F, 1.0F});
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                            {0.16F, 0.16F, 0.19F, 1.0F});
      ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.19F, 0.17F, 0.24F, 1.0F});
      const bool componentOpen =
          ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
      ImGui::PopStyleColor(3);
      ImGui::SameLine(ImGui::GetWindowWidth() - 36.0F);
      if (ImGui::SmallButton("..."))
        ImGui::OpenPopup("component-menu");
      bool removeRequested = false;
      if (ImGui::BeginPopup("component-menu")) {
        removeRequested = ImGui::MenuItem("Remove component");
        ImGui::EndPopup();
      }
      if (removeRequested) {
        std::string error;
        const bool removed =
            workspace.removeComponent(selected->id, name, error);
        notice = removed ? "Component removed" : error;
        ImGui::PopID();
        ImGui::End();
        return;
      }
      if (!componentOpen) {
        ImGui::PopID();
        continue;
      }
      drawInlineIssue(workspace, {.entityId = selected->id, .component = name});
      if (luaMetadata != nullptr)
        drawScriptProperties(workspace, selected->id, component, *luaMetadata,
                             notice);
      else if (descriptor == nullptr || descriptor->fields.empty())
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
    if (ImGui::IsWindowAppearing()) {
      state.componentSearch.fill('\0');
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##component-search", "Search components...",
                             state.componentSearch.data(),
                             state.componentSearch.size());
    ImGui::Separator();
    const std::string_view query(state.componentSearch.data());
    bool anyResult = false;
    std::string_view category;
    for (const EditorComponentChoice &choice :
         editorComponentChoices(*entity)) {
      const ComponentDescriptor &descriptor = *choice.descriptor;
      if (!editorComponentMatchesSearch(query, descriptor.name,
                                        descriptor.editor.displayName,
                                        descriptor.editor.category))
        continue;
      anyResult = true;
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
      if (!choice.compatible &&
          ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", choice.incompatibility.c_str());
      if (!choice.compatible)
        ImGui::EndDisabled();
    }
    if (!luaComponents.components.empty()) {
      const bool hasLuaScript = components != entity->end() &&
                                components->is_object() &&
                                components->contains("LuaScript");
      std::string_view luaCategory;
      for (const EditorLuaComponentMetadata &metadata :
           luaComponents.components) {
        if (!editorComponentMatchesSearch(query, metadata.module,
                                          metadata.displayName,
                                          metadata.category))
          continue;
        anyResult = true;
        if (luaCategory != metadata.category) {
          luaCategory = metadata.category;
          const std::string heading = "Lua · " + metadata.category;
          ImGui::SeparatorText(heading.c_str());
        }
        if (hasLuaScript)
          ImGui::BeginDisabled();
        if (ImGui::Selectable(metadata.displayName.c_str())) {
          std::string error;
          const bool added =
              workspace.addScriptComponent(selected->id, metadata, error);
          notice = added ? metadata.displayName + " added" : error;
          if (hasLuaScript)
            ImGui::EndDisabled();
          ImGui::EndCombo();
          ImGui::End();
          return;
        }
        if (hasLuaScript &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          ImGui::SetTooltip(
              "This entity already has a Lua component. Multiple scripts per "
              "entity are not supported yet.");
        if (hasLuaScript)
          ImGui::EndDisabled();
      }
    }
    if (!anyResult)
      ImGui::TextDisabled("No matching components.");
    ImGui::EndCombo();
  }
  ImGui::End();
}

} // namespace demi::editor
