#include "editor/EditorInspectorPanel.h"

#include "editor/EditorInspectorModel.h"
#include "editor/EditorIsoGridInspector.h"
#include "editor/EditorLuaComponentMetadata.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorScenePreview.h"
#include "editor/EditorStructuredValue.h"
#include "editor/EditorWorkspace.h"
#include "demi/runtime/scene/EntityPresets.h"

#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <optional>
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

bool descriptorHasMatchingProperty(const ComponentDescriptor &descriptor,
                                   const std::string_view query) {
  if (query.empty())
    return true;
  if (descriptor.fields.empty())
    return editorComponentMatchesSearch(
        query, descriptor.name, descriptor.editor.displayName,
        descriptor.editor.category);
  return std::ranges::any_of(descriptor.fields, [&](const auto &field) {
    return field.editorVisible &&
           editorPropertyMatchesSearch(query, descriptor, field);
  });
}

bool scriptHasMatchingProperty(const EditorLuaComponentMetadata &metadata,
                               const std::string_view query) {
  if (query.empty() ||
      editorComponentMatchesSearch(query, metadata.module, metadata.displayName,
                                   metadata.category))
    return true;
  return std::ranges::any_of(metadata.propertySchema.items(),
                             [&](const auto &property) {
    const nlohmann::json &definition = property.value();
    if (!definition.is_object())
      return false;
    return editorComponentMatchesSearch(
        query, property.key(), definition.value("label", std::string{}),
        definition.value("description", std::string{}));
  });
}

bool genericComponentHasMatchingProperty(const nlohmann::json &component,
                                         const std::string_view componentName,
                                         const std::string_view query) {
  if (query.empty() ||
      editorComponentMatchesSearch(query, componentName, componentName, {}))
    return true;
  return std::ranges::any_of(component.items(), [&](const auto &property) {
    return editorComponentMatchesSearch(query, property.key(), {}, {});
  });
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

bool drawVectorEditor(const ComponentFieldDescriptor *field,
                      std::array<float, 4> &values, int count);
bool beginPropertyTable(const char *id);
void drawPropertyLabel(std::string_view label, EditorPropertyOrigin origin,
                       const ComponentFieldDescriptor *field = nullptr);

nlohmann::json initialScriptPropertyValue(const std::string_view type,
                                          const nlohmann::json &definition) {
  if (definition.contains("default"))
    return definition["default"];
  if (type == "boolean")
    return false;
  if (type == "integer")
    return 0;
  if (type == "number")
    return definition.value("minimum", 0.0);
  if (type == "string" || type == "asset" || type == "entity")
    return "";
  if (type == "enum" && definition.contains("values") &&
      definition["values"].is_array() && !definition["values"].empty())
    return definition["values"].front();
  if (type == "vec2")
    return nlohmann::json::array({0.0, 0.0});
  if (type == "vec3")
    return nlohmann::json::array({0.0, 0.0, 0.0});
  if (type == "color")
    return nlohmann::json::array({1.0, 1.0, 1.0, 1.0});
  return nullptr;
}

void drawScriptProperties(EditorWorkspace &workspace,
                          const std::string_view entityId,
                          const nlohmann::json &component,
                          const EditorLuaComponentMetadata &metadata,
                          std::string &notice, const bool prefabEntity,
                          const std::string_view query,
                          StructuredValueState &structuredState) {
  if (!metadata.description.empty())
    ImGui::TextWrapped("%s", metadata.description.c_str());
  nlohmann::json properties =
      component.value("properties", nlohmann::json::object());
  const SceneValueTarget propertiesTarget{
      .entityId = std::string(entityId),
      .component = "LuaScript",
      .field = "properties"};
  const bool propertiesAreExplicit =
      workspace.hasExplicitValue(propertiesTarget);
  if (prefabEntity && propertiesAreExplicit &&
      ImGui::SmallButton("Reset Lua property overrides")) {
    std::string error;
    notice = workspace.removeValue(propertiesTarget, error)
                 ? "Lua property overrides removed"
                 : error;
  }
  if (!beginPropertyTable("##script-properties"))
    return;

  if (query.empty() || editorComponentMatchesSearch(
                           query, metadata.module, "Module", {})) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    drawPropertyLabel("Module", prefabEntity ? EditorPropertyOrigin::Inherited
                                               : EditorPropertyOrigin::Authored);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", metadata.module.c_str());
  }
  ImGui::EndTable();
  for (const auto &[name, definition] : metadata.propertySchema.items()) {
    if (!definition.is_object())
      continue;
    const std::string type = definition.value("type", "");
    const std::string label = definition.value("label", name);
    if (!editorComponentMatchesSearch(
            query, name, label,
            definition.value("description", std::string{})) &&
        !editorComponentMatchesSearch(query, metadata.module,
                                      metadata.displayName,
                                      metadata.category))
      continue;
    const bool hasAuthoredValue = properties.contains(name);
    const bool hasDefault = definition.contains("default");
    const bool hasValue = hasAuthoredValue || hasDefault;
    nlohmann::json value = hasAuthoredValue
                               ? properties[name]
                               : definition.value("default", nlohmann::json{});
    EditorPropertyOrigin origin = EditorPropertyOrigin::Missing;
    if (!hasValue)
      origin = EditorPropertyOrigin::Missing;
    else if (prefabEntity)
      origin = propertiesAreExplicit ? EditorPropertyOrigin::Override
                                     : EditorPropertyOrigin::Inherited;
    else if (hasAuthoredValue)
      origin = EditorPropertyOrigin::Authored;
    else if (hasDefault)
      origin = EditorPropertyOrigin::Default;
    ImGui::PushID(name.c_str());
    const bool collection = type == "object" || type == "array" ||
                            (value.is_structured() && type != "vec2" &&
                             type != "vec3" && type != "color");
    if (!collection && !beginPropertyTable("##script-properties")) {
      ImGui::PopID();
      continue;
    }
    if (!collection) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
    } else {
      ImGui::Spacing();
      ImGui::Separator();
    }
    drawPropertyLabel(label, origin);
    const std::string description = definition.value("description", "");
    if (!description.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("?");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", description.c_str());
    }
    if (!collection)
      ImGui::TableSetColumnIndex(1);
    if (!hasValue) {
      if (ImGui::Button("Add value")) {
        properties[name] = initialScriptPropertyValue(type, definition);
        (void)commit(workspace, propertiesTarget, properties, notice);
      }
      if (!collection)
        ImGui::EndTable();
      ImGui::PopID();
      continue;
    }
    ImGui::SetNextItemWidth(-1.0F);
    bool changed = false;
    std::optional<StructuredValueEdit> structured;
    if (type == "boolean" && value.is_boolean()) {
      bool edited = value.get<bool>();
      changed = ImGui::Checkbox("##value", &edited);
      value = edited;
    } else if (type == "number" && value.is_number()) {
      double edited = value.get<double>();
      changed = ImGui::InputDouble("##value", &edited);
      if (definition.contains("minimum"))
        edited = std::max(edited, definition["minimum"].get<double>());
      if (definition.contains("maximum"))
        edited = std::min(edited, definition["maximum"].get<double>());
      value = edited;
    } else if (type == "integer" && value.is_number_integer()) {
      int edited = value.get<int>();
      changed = ImGui::InputInt("##value", &edited);
      value = edited;
    } else if ((type == "string" || type == "asset" || type == "entity") &&
               value.is_string()) {
      std::string edited = value.get<std::string>();
      changed = inputString("##value", edited);
      value = std::move(edited);
    } else if (type == "enum" && value.is_string() &&
               definition.contains("values") &&
               definition["values"].is_array()) {
      std::string edited = value.get<std::string>();
      if (ImGui::BeginCombo("##value", edited.c_str())) {
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
        changed = type == "color"
                      ? ImGui::ColorEdit4("##value", edited.data())
                      : drawVectorEditor(nullptr, edited,
                                         static_cast<int>(count));
        value = nlohmann::json::array();
        for (std::size_t index = 0; index < count; ++index)
          value.push_back(edited[index]);
      }
    } else if (value.is_structured()) {
      structured = drawStructuredValue(value, structuredState);
      changed = structured->changed;
    } else {
      ImGui::TextDisabled("%s", value.dump().c_str());
    }
    if (changed) {
      properties[name] = std::move(value);
      if (structured) {
        std::string error;
        const bool accepted = workspace.editValue(propertiesTarget, properties,
                                                  structured->continuous,
                                                  error);
        notice = accepted ? "Scene modified" : error;
      } else {
        (void)commit(workspace, propertiesTarget, properties, notice);
      }
      finishEdit(workspace);
    }
    if (structured && structured->finished) workspace.endContinuousEdit();
    if (!collection)
      ImGui::TableSetColumnIndex(2);
    if (!prefabEntity && hasAuthoredValue && ImGui::SmallButton("Reset")) {
      properties.erase(name);
      (void)commit(workspace, propertiesTarget, properties, notice);
    }
    if (!collection)
      ImGui::EndTable();
    ImGui::PopID();
  }
}

void drawInlineIssue(const EditorWorkspace &workspace,
                     const SceneValueTarget &target) {
  const std::string *issue = workspace.sceneDocument().issueFor(target);
  if (issue != nullptr)
    ImGui::TextColored({0.95F, 0.34F, 0.38F, 1.0F}, "%s", issue->c_str());
}

nlohmann::json initialFieldValue(const ComponentDescriptor &descriptor,
                                 const ComponentFieldDescriptor &field) {
  const auto &defaults = runtime::scene_loading::componentDefaults(descriptor);
  if (const auto value = defaults.find(field.name); value != defaults.end())
    return *value;

  // These are editing scaffolds, not claims about omitted runtime values.
  // Authoring presence-sensitive sugar (for example atlas={}) is a real edit.
  switch (field.type) {
  case ComponentFieldType::Object:
    return nlohmann::json::object();
  case ComponentFieldType::Vec2Array:
  case ComponentFieldType::Vec3Array:
    return nlohmann::json::array();
  case ComponentFieldType::Boolean:
    return false;
  case ComponentFieldType::Integer:
    return field.allowedIntegers.empty() ? 0 : field.allowedIntegers.front();
  case ComponentFieldType::Number:
    return field.hasMinimum ? field.minimum : 0.0;
  case ComponentFieldType::String:
    return field.allowedValues.empty()
               ? nlohmann::json("")
               : nlohmann::json(field.allowedValues.front());
  case ComponentFieldType::Vec2:
    return nlohmann::json::array({0.0, 0.0});
  case ComponentFieldType::Vec3:
    return nlohmann::json::array({0.0, 0.0, 0.0});
  case ComponentFieldType::Color:
    return nlohmann::json::array({1.0, 1.0, 1.0, 1.0});
  }
  return nullptr;
}

void drawFieldHelp(const ComponentFieldDescriptor &field) {
  if (!ImGui::IsItemHovered())
    return;
  if (field.editor.help.empty() && !field.restartRequired &&
      !runtime::scene_loading::componentFieldEditorReadOnly(field))
    return;
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0F);
  if (!field.editor.help.empty())
    ImGui::TextWrapped("%s", field.editor.help.data());
  if (field.restartRequired)
    ImGui::TextDisabled("Requires a runtime restart.");
  if (runtime::scene_loading::componentFieldEditorReadOnly(field))
    ImGui::TextDisabled("Read-only in the generic inspector.");
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
}

bool beginPropertyTable(const char *id) {
  const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp |
                                ImGuiTableFlags_PadOuterX |
                                ImGuiTableFlags_BordersInnerV;
  if (!ImGui::BeginTable(id, 3, flags))
    return false;
  ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch,
                          0.32F);
  ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.68F);
  const float resetWidth = ImGui::CalcTextSize("Reset").x +
                           ImGui::GetStyle().FramePadding.x * 2.0F;
  ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed,
                          resetWidth);
  return true;
}

void drawPropertyOrigin(const EditorPropertyOrigin origin) {
  const std::string_view label = editorPropertyOriginLabel(origin);
  if (origin == EditorPropertyOrigin::Override) {
    ImGui::TextColored({0.64F, 0.51F, 0.94F, 1.0F}, "%.*s",
                       static_cast<int>(label.size()), label.data());
    return;
  }
  if (origin == EditorPropertyOrigin::Missing) {
    ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "%.*s",
                       static_cast<int>(label.size()), label.data());
    return;
  }
  ImGui::TextDisabled("%.*s", static_cast<int>(label.size()), label.data());
}

void drawPropertyLabel(const std::string_view label,
                       const EditorPropertyOrigin origin,
                       const ComponentFieldDescriptor *field) {
  ImGui::AlignTextToFramePadding();
  const bool highlighted = origin == EditorPropertyOrigin::Override ||
                           origin == EditorPropertyOrigin::Missing;
  if (highlighted)
    ImGui::PushStyleColor(ImGuiCol_Text, origin == EditorPropertyOrigin::Override
        ? ImVec4{0.72F, 0.60F, 0.96F, 1.0F} : ImVec4{0.95F, 0.72F, 0.38F, 1.0F});
  ImGui::TextWrapped("%.*s", static_cast<int>(label.size()), label.data());
  if (highlighted)
    ImGui::PopStyleColor();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
    drawPropertyOrigin(origin);
    if (field && !field->editor.help.empty())
      ImGui::TextWrapped("%s", field->editor.help.data());
    if (field && field->restartRequired)
      ImGui::TextDisabled("Requires a runtime restart.");
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

bool drawVectorEditor(const ComponentFieldDescriptor *field,
                      std::array<float, 4> &values, const int count) {
  static constexpr std::array<const char *, 4> AxisLabels{
      "X", "Y", "Z", "W"};
  static const std::array<ImVec4, 4> AxisColors{
      ImVec4{0.92F, 0.38F, 0.42F, 1.0F},
      ImVec4{0.45F, 0.78F, 0.48F, 1.0F},
      ImVec4{0.40F, 0.62F, 0.94F, 1.0F},
      ImVec4{0.72F, 0.58F, 0.90F, 1.0F}};
  bool changed = false;
  const float available = ImGui::GetContentRegionAvail().x;
  const float axisWidth = available / static_cast<float>(count);
  const bool stackAxes = axisWidth < ImGui::GetFontSize() * 2.4F;
  if (stackAxes) {
    if (!ImGui::BeginTable("##vector-axes", 2,
                           ImGuiTableFlags_SizingStretchProp))
      return false;
    ImGui::TableSetupColumn("Axis", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::CalcTextSize("W").x);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    for (int index = 0; index < count; ++index) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(AxisColors[index], "%s", AxisLabels[index]);
      ImGui::TableSetColumnIndex(1);
      ImGui::PushID(index);
      ImGui::SetNextItemWidth(-1.0F);
      changed |= ImGui::InputFloat("##axis", &values[index], 0.0F, 0.0F,
                                   "%.5g");
      ImGui::PopID();
    }
    ImGui::EndTable();
  } else {
    if (!ImGui::BeginTable("##vector-axes", count,
                           ImGuiTableFlags_SizingStretchSame))
      return false;
    ImGui::TableNextRow();
    for (int index = 0; index < count; ++index) {
      ImGui::TableSetColumnIndex(index);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(AxisColors[index], "%s", AxisLabels[index]);
    }
    ImGui::TableNextRow();
    for (int index = 0; index < count; ++index) {
      ImGui::TableSetColumnIndex(index);
      ImGui::PushID(index);
      ImGui::SetNextItemWidth(-1.0F);
      changed |= ImGui::InputFloat("##axis", &values[index], 0.0F, 0.0F,
                                   "%.5g");
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (changed && field != nullptr)
    for (int index = 0; index < count; ++index)
      clampNumericValue(values[index], *field);
  return changed;
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
        workspace.sources(), target.component);
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
                    StructuredValueState &structuredState,
                    const std::vector<SceneValueTarget> *targets = nullptr,
                    const bool mixed = false) {
  (void)mixed;
  if (runtime::scene_loading::componentFieldEditorReadOnly(field)) {
    if (value.is_structured()) {
      auto preview = value;
      (void)drawStructuredValue(preview, structuredState, 0, true);
    } else ImGui::TextWrapped("%s", value.dump().c_str());
    return true;
  }
  if (field.nullable && field.type == ComponentFieldType::Number) {
    if (value.is_null()) {
      ImGui::TextUnformatted("None");
      ImGui::SameLine();
      if (ImGui::SmallButton("Set"))
        return targets ? commitMany(workspace, *targets, 0.0, notice)
                       : commit(workspace, target, 0.0, notice);
      return true;
    }
    if (ImGui::SmallButton("None"))
      return targets ? commitMany(workspace, *targets, nullptr, notice)
                     : commit(workspace, target, nullptr, notice);
    ImGui::SameLine();
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
    std::int64_t edited = value.get<std::int64_t>();
    if (!field.allowedIntegers.empty()) {
      const auto label = std::to_string(edited);
      if (ImGui::BeginCombo("##value", label.c_str())) {
        for (const int option : field.allowedIntegers) {
          if (ImGui::Selectable(std::to_string(option).c_str(),
                                edited == option)) {
            edited = option;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
    } else {
      changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &edited);
    }
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
    if (field.type == ComponentFieldType::Color)
      changed = ImGui::ColorEdit4("##value", edited.data());
    else
      changed = drawVectorEditor(&field, edited, count);
    replacement = nlohmann::json::array();
    for (int index = 0; index < count; ++index)
      replacement.push_back(edited[index]);
    break;
  }
  case ComponentFieldType::Object:
  case ComponentFieldType::Vec2Array:
  case ComponentFieldType::Vec3Array: {
    int vectorSize = 0;
    if (field.type == ComponentFieldType::Vec2Array)
      vectorSize = 2;
    else if (field.type == ComponentFieldType::Vec3Array)
      vectorSize = 3;
    const auto edit = drawStructuredValue(replacement, structuredState, vectorSize);
    bool accepted = true;
    if (edit.changed) {
      std::string error;
      accepted = targets ? workspace.editValues(*targets, replacement, error)
                         : workspace.editValue(target, replacement,
                                               edit.continuous, error);
      notice = accepted ? "Scene modified" : error;
    }
    if (edit.finished)
      workspace.endContinuousEdit();
    return accepted;
  }
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
                         nlohmann::json component,
                         const ComponentDescriptor &descriptor,
                         std::string &notice, const bool prefabEntity,
                         const std::string_view query,
                         StructuredValueState &structuredState) {
  // Field commits may replace the scene document and preview world. Keep this
  // component snapshot alive until every row for the frame has been drawn.
  if (!descriptor.editor.help.empty()) {
    ImGui::TextWrapped("%s", descriptor.editor.help.data());
    ImGui::Spacing();
  }
  bool showAdvanced = false;
  const bool hasAdvanced = std::ranges::any_of(
      descriptor.fields, [](const auto &field) { return field.editor.advanced; });
  if (hasAdvanced && query.empty()) {
    const auto key = ImGui::GetID("advanced-fields");
    showAdvanced = ImGui::GetStateStorage()->GetBool(key, false);
    if (ImGui::Checkbox("Advanced fields", &showAdvanced)) {
      ImGui::GetStateStorage()->SetBool(key, showAdvanced);
    }
  }
  for (const ComponentFieldDescriptor &field : descriptor.fields) {
    if (field.editor.advanced && !showAdvanced && query.empty())
      continue;
    if (!field.editorVisible ||
        !editorPropertyMatchesSearch(query, descriptor, field))
      continue;
    const SceneValueTarget target{.entityId = std::string(entityId),
                                  .component = componentName,
                                  .field = std::string(field.name)};
    const EditorPropertyPresentation presentation =
        editorPropertyPresentation(descriptor, field, component, prefabEntity,
                                   workspace.hasExplicitValue(target));
    ImGui::PushID(field.name.data());
    const bool collection = field.type == ComponentFieldType::Object ||
                            field.type == ComponentFieldType::Vec2Array ||
                            field.type == ComponentFieldType::Vec3Array;
    if (!collection && !beginPropertyTable("##component-properties")) {
      ImGui::PopID();
      continue;
    }
    if (!collection) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
    } else {
      ImGui::Spacing();
      ImGui::Separator();
    }
    const std::string label =
        runtime::scene_loading::componentFieldEditorLabel(field);
    drawPropertyLabel(label, presentation.origin, &field);

    if (!collection)
      ImGui::TableSetColumnIndex(1);
    if (presentation.requiresExplicitAdd) {
      if (runtime::scene_loading::componentFieldEditorReadOnly(field)) {
        ImGui::TextDisabled("No authored value");
      } else if (ImGui::Button("Add value")) {
        (void)commit(workspace, target, initialFieldValue(descriptor, field),
                     notice);
      }
    } else {
      ImGui::SetNextItemWidth(-1.0F);
      (void)drawFieldValue(workspace, target, field, presentation.value, notice, structuredState);
    }
    drawInlineIssue(workspace, target);

    if (!collection)
      ImGui::TableSetColumnIndex(2);
    if (presentation.canReset) {
      if (ImGui::SmallButton("Reset")) {
        std::string error;
        if (!workspace.removeValue(target, error))
          notice = error;
        else
          notice = prefabEntity ? "Prefab override removed"
                                : "Optional field reset to its default";
      }
    }
    if (!collection)
      ImGui::EndTable();
    ImGui::PopID();
  }
}

void drawMultiSelection(EditorWorkspace &workspace, std::string &notice,
                        const std::string_view query,
                        StructuredValueState &structuredState) {
  ImGui::Text("%zu entities selected", workspace.selectedEntityIds().size());
  ImGui::TextDisabled(
      "Only authored fields common to every selection are shown.");
  ImGui::Separator();
  std::string currentComponent;
  bool tableOpen = false;
  bool hasMatchingField = false;
  for (const EditorCommonField &common : editorCommonFields(
           workspace.sceneDocument().json(), workspace.selectedEntityIds())) {
    if (!editorPropertyMatchesSearch(query, *common.component, *common.field))
      continue;
    hasMatchingField = true;
    const std::string componentName(common.component->name);
    if (currentComponent != componentName) {
      if (tableOpen)
        ImGui::EndTable();
      currentComponent = componentName;
      ImGui::Spacing();
      ImGui::TextUnformatted(common.component->editor.displayName.data());
      ImGui::Separator();
      ImGui::PushID(componentName.c_str());
      tableOpen = beginPropertyTable("##multi-properties");
      ImGui::PopID();
      if (!tableOpen)
        continue;
    }
    if (!tableOpen)
      continue;
    ImGui::PushID(componentName.c_str());
    ImGui::PushID(common.field->name.data());
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const std::string label =
        runtime::scene_loading::componentFieldEditorLabel(*common.field);
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s", label.c_str());
    if (!common.field->editor.help.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("?");
      drawFieldHelp(*common.field);
    }
    ImGui::TextDisabled("%s", common.mixed ? "Mixed" : "Authored");
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0F);
    (void)drawFieldValue(workspace, common.targets.front(), *common.field,
                         common.value, notice, structuredState, &common.targets, common.mixed);
    if (common.mixed && ImGui::IsItemHovered())
      ImGui::SetTooltip(
          "The selected entities have different values. Editing applies one "
          "value to all of them.");
    ImGui::PopID();
    ImGui::PopID();
  }
  if (tableOpen)
    ImGui::EndTable();
  if (!query.empty() && !hasMatchingField)
    ImGui::TextDisabled("No matching common properties.");
}

void drawGenericComponent(const nlohmann::json &component,
                          const std::string_view componentName,
                          const std::string_view query) {
  if (!beginPropertyTable("##generic-properties"))
    return;
  for (const auto &[name, value] : component.items()) {
    if (!editorComponentMatchesSearch(query, componentName, componentName,
                                      {}) &&
        !editorComponentMatchesSearch(query, name, name, {}))
      continue;
    ImGui::PushID(name.c_str());
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    drawPropertyLabel(name, EditorPropertyOrigin::Authored);
    ImGui::TableSetColumnIndex(1);
    const std::string rendered =
        value.is_string() ? value.get<std::string>() : value.dump();
    ImGui::TextWrapped("%s", rendered.c_str());
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void drawEntityHeader(EditorWorkspace &workspace, const nlohmann::json &entity,
                      std::string &notice, const bool prefabEntity) {
  const std::string id = entity.value("id", std::string{});
  ImGui::TextDisabled("%s", id.c_str());
  if (beginPropertyTable("##entity-properties")) {
    const auto drawOrigin = [prefabEntity](const bool isExplicit) {
      if (prefabEntity)
        return isExplicit ? EditorPropertyOrigin::Override
                          : EditorPropertyOrigin::Inherited;
      return isExplicit ? EditorPropertyOrigin::Authored
                        : EditorPropertyOrigin::Default;
    };

    const SceneValueTarget nameTarget{.entityId = id, .field = "name"};
    const bool nameIsExplicit = workspace.hasExplicitValue(nameTarget);
    ImGui::PushID("name");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    drawPropertyLabel("Name", drawOrigin(nameIsExplicit));
    ImGui::TableSetColumnIndex(1);
    std::string name = entity.value("name", id);
    ImGui::SetNextItemWidth(-1.0F);
    if (inputString("##value", name))
      (void)commit(workspace, nameTarget, name, notice);
    finishEdit(workspace);
    drawInlineIssue(workspace, nameTarget);
    ImGui::TableSetColumnIndex(2);
    if (prefabEntity && nameIsExplicit && ImGui::SmallButton("Reset")) {
      std::string error;
      notice = workspace.removeValue(nameTarget, error)
                   ? "Prefab name override removed"
                   : error;
    }
    ImGui::PopID();

    const SceneValueTarget enabledTarget{.entityId = id, .field = "enabled"};
    const bool enabledIsExplicit = workspace.hasExplicitValue(enabledTarget);
    ImGui::PushID("enabled");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    drawPropertyLabel("Enabled", drawOrigin(enabledIsExplicit));
    ImGui::TableSetColumnIndex(1);
    bool enabled = entity.value("enabled", true);
    if (ImGui::Checkbox("##value", &enabled))
      (void)commit(workspace, enabledTarget, enabled, notice);
    drawInlineIssue(workspace, enabledTarget);
    ImGui::TableSetColumnIndex(2);
    if (enabledIsExplicit && ImGui::SmallButton("Reset")) {
      std::string error;
      notice = workspace.removeValue(enabledTarget, error)
                   ? "Enabled reset to its default"
                   : error;
    }
    ImGui::PopID();

    const SceneValueTarget layerTarget{.entityId = id, .field = "layer"};
    const bool layerIsExplicit = workspace.hasExplicitValue(layerTarget);
    ImGui::PushID("layer");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    drawPropertyLabel("Layer", drawOrigin(layerIsExplicit));
    ImGui::TableSetColumnIndex(1);
    std::string layer = entity.value("layer", "Default");
    ImGui::SetNextItemWidth(-1.0F);
    if (inputString("##value", layer))
      (void)commit(workspace, layerTarget, layer, notice);
    finishEdit(workspace);
    drawInlineIssue(workspace, layerTarget);
    ImGui::TableSetColumnIndex(2);
    if (layerIsExplicit && ImGui::SmallButton("Reset")) {
      std::string error;
      notice = workspace.removeValue(layerTarget, error)
                   ? "Layer reset to its default"
                   : error;
    }
    ImGui::PopID();
    ImGui::EndTable();
  }
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
                        std::string &notice, bool *open) {
  if (!beginEditorPanel("Inspector", position, size, open)) {
    ImGui::End();
    return;
  }
  const float panelWidth = ImGui::GetContentRegionAvail().x;
  if (workspace.sceneDocument().isDirty()) {
    ImGui::SetCursorPosX(std::max(panelWidth - 68.0F, 0.0F));
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
  ImGui::SetNextItemWidth(-1.0F);
  ImGui::InputTextWithHint("##property-search", "Filter properties...",
                           state.propertySearch.data(),
                           state.propertySearch.size());
  const std::string_view propertyQuery(state.propertySearch.data());
  ImGui::Separator();
  if (workspace.selectedEntityIds().size() > 1) {
    drawMultiSelection(workspace, notice, propertyQuery, state.structuredValues);
    ImGui::End();
    return;
  }

  const std::string selectedId = selected->id;
  const nlohmann::json *entity = workspace.sceneDocument().entity(selectedId);
  const bool prefabEntity =
      entity == nullptr && !selected->prefabInstance.empty();
  nlohmann::json effectiveEntity;
  if (prefabEntity) {
    effectiveEntity = editorPreviewEntityJson(*selected);
    entity = &effectiveEntity;
    ImGui::TextColored({0.64F, 0.51F, 0.94F, 1.0F}, "Prefab instance: %s",
                       selected->prefabInstance.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("· %s", selected->prefabLocalId.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped(
        "Changes below affect only this instance in the current document. "
        "Open the shared source prefab to change every instance.");
    ImGui::PopStyleColor();
    if (ImGui::Button("Open source prefab")) {
      const auto path = workspace.prefabSourcePath(selectedId);
      if (path)
        state.openRequest = *path;
      else
        notice = "Could not resolve the source prefab for this instance.";
      ImGui::End();
      return;
    }
    ImGui::Separator();
  }
  if (entity == nullptr) {
    ImGui::TextDisabled("The selected entity has no authored source.");
    ImGui::End();
    return;
  }
  // A commit can replace scene-document storage and rebuild the preview world.
  // This snapshot is the sole backing storage for every entity/component/value
  // reference retained while drawing the rest of this frame.
  nlohmann::json entitySnapshot = *entity;
  if (entitySnapshot.contains("preset")) {
    const auto resolved = runtime::scene_loading::expandEntityPreset(entitySnapshot);
    entitySnapshot["components"] = resolved.value("components", nlohmann::json::object());
  }
  entity = &entitySnapshot;
  drawEntityHeader(workspace, *entity, notice, prefabEntity);
  nlohmann::json presetComponents = nlohmann::json::object();
  if (entity->contains("preset") && !prefabEntity) {
    presetComponents = runtime::scene_loading::expandEntityPreset(
        {{"preset", (*entity)["preset"]}}).value("components", nlohmann::json::object());
    ImGui::TextDisabled("Preset: %s", (*entity)["preset"].get<std::string>().c_str());
    if (ImGui::Button("Unpack preset")) {
      std::string error;
      notice = workspace.unpackPreset(selectedId, error) ? "Preset converted to independent components" : error;
      ImGui::End();
      return;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Keep the current values as independent components, so components can be removed. Undo restores the preset.");
  }
  if (entity->contains("components") &&
      (*entity)["components"].contains("PrefabPlacement3D")) {
    ImGui::TextWrapped(
        "Streamed prefab placement. Move/rotate/scale this entity to place "
        "the instance. Preview meshes are not saved or simulated; edit their "
        "source prefab below.");
    if (ImGui::Button("Open source prefab")) {
      const auto path = runtime::composition::resolvePrefabReference(
          workspace.sceneDocument().path(),
          (*entity)["components"]["PrefabPlacement3D"].value(
              "prefab", std::string{}));
      if (path)
        state.openRequest = *path;
      else
        notice = "Could not resolve placement prefab";
      ImGui::End();
      return;
    }
  }
  const EditorLuaComponentCatalog luaComponents = discoverEditorLuaComponents(
      workspace.project().project.projectDirectory, workspace.sources());

  const auto components = entity->find("components");
  bool anyVisibleComponent = false;
  if (components != entity->end() && components->is_object()) {
    std::vector<std::string> componentOrder;
    const char *spatial = transformComponentName(*entity);
    if (spatial)
      componentOrder.emplace_back(spatial);
    for (const auto &[name, value] : components->items()) {
      (void)value;
      if (!spatial || name != spatial)
        componentOrder.push_back(name);
    }
    for (const auto &name : componentOrder) {
      const auto &component = components->at(name);
      if (!component.is_object())
        continue;
      const ComponentDescriptor *descriptor =
          runtime::scene_loading::findComponentDescriptor(name);
      const EditorLuaComponentMetadata *luaMetadata =
          name == "LuaScript" ? scriptMetadata(luaComponents, component)
                              : nullptr;
      bool matchesFilter = false;
      if (luaMetadata != nullptr)
        matchesFilter = scriptHasMatchingProperty(*luaMetadata, propertyQuery);
      else if (descriptor != nullptr)
        matchesFilter = descriptorHasMatchingProperty(*descriptor,
                                                      propertyQuery);
      else
        matchesFilter = genericComponentHasMatchingProperty(
            component, name, propertyQuery);
      if (!matchesFilter)
        continue;
      anyVisibleComponent = true;
      const char *title = name.c_str();
      if (luaMetadata != nullptr)
        title = luaMetadata->displayName.c_str();
      else if (descriptor != nullptr)
        title = descriptor->editor.displayName.data();
      ImGui::PushID(name.c_str());
      ImGui::PushStyleColor(ImGuiCol_Header, {0.115F, 0.119F, 0.139F, 1.0F});
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                            {0.16F, 0.16F, 0.19F, 1.0F});
      ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.19F, 0.17F, 0.24F, 1.0F});
      bool componentOpen = false;
      bool componentMenuRequested = false;
      if (ImGui::BeginTable("##component-header", 2,
                            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Component",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("...").x +
                                    ImGui::GetStyle().FramePadding.x * 2.0F);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        componentOpen = ImGui::CollapsingHeader(
            title, ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::TableSetColumnIndex(1);
        if (ImGui::SmallButton("..."))
          componentMenuRequested = true;
        ImGui::EndTable();
      }
      ImGui::PopStyleColor(3);
      if (componentMenuRequested)
        ImGui::OpenPopup("component-menu");
      bool removeRequested = false;
      bool revertRequested = false;
      if (ImGui::BeginPopup("component-menu")) {
        const bool inheritedFromPreset = presetComponents.contains(name);
        if (prefabEntity) {
          revertRequested = ImGui::MenuItem("Revert component overrides");
          ImGui::Separator();
        }
        removeRequested =
            ImGui::MenuItem(prefabEntity ? "Remove component from this instance"
                                         : "Remove component",
                            nullptr, false, !inheritedFromPreset);
        if (!prefabEntity && inheritedFromPreset)
          ImGui::TextDisabled("Unpack the preset to remove its components.");
        ImGui::EndPopup();
      }
      if (revertRequested) {
        std::string error;
        const bool reverted =
            workspace.revertComponentOverride(selectedId, name, error);
        notice =
            reverted ? "Component reverted to the shared prefab source" : error;
        ImGui::PopID();
        ImGui::End();
        return;
      }
      if (removeRequested) {
        std::string error;
        const bool removed =
            workspace.removeComponent(selectedId, name, error);
        notice = removed
                     ? (prefabEntity ? "Component removed from this instance"
                                     : "Component removed")
                     : error;
        ImGui::PopID();
        ImGui::End();
        return;
      }
      if (!componentOpen) {
        ImGui::PopID();
        continue;
      }
      drawInlineIssue(workspace, {.entityId = selectedId, .component = name});
      if (luaMetadata != nullptr)
        drawScriptProperties(workspace, selectedId, component, *luaMetadata,
                             notice, prefabEntity, propertyQuery, state.structuredValues);
      else if (descriptor == nullptr || descriptor->fields.empty())
        drawGenericComponent(component, name, propertyQuery);
      else
        drawComponentFields(workspace, selectedId, name, component,
                            *descriptor, notice, prefabEntity, propertyQuery, state.structuredValues);
      ImGui::PopID();
    }
  }
  if (!propertyQuery.empty() && !anyVisibleComponent)
    ImGui::TextDisabled("No matching properties.");

  if (prefabEntity) {
    const std::vector<std::string> removedComponents =
        workspace.removedComponentOverrides(selectedId);
    if (!removedComponents.empty()) {
      ImGui::Spacing();
      ImGui::SeparatorText("Removed components");
      std::string restoreComponent;
      if (ImGui::BeginTable("##removed-components", 2,
                            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Component",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("Restore").x +
                                    ImGui::GetStyle().FramePadding.x * 2.0F);
        for (const std::string &name : removedComponents) {
          const ComponentDescriptor *descriptor =
              runtime::scene_loading::findComponentDescriptor(name);
          const char *displayName = descriptor != nullptr
                                        ? descriptor->editor.displayName.data()
                                        : name.c_str();
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(displayName);
          ImGui::TableSetColumnIndex(1);
          ImGui::PushID(name.c_str());
          if (ImGui::SmallButton("Restore"))
            restoreComponent = name;
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      if (!restoreComponent.empty()) {
        std::string error;
        const bool restored = workspace.revertComponentOverride(
            selectedId, restoreComponent, error);
        notice = restored ? "Component restored from the shared prefab source"
                          : error;
        ImGui::End();
        return;
      }
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
        if (descriptor.editor.initialReferenceField.empty()) {
          std::string error;
          const bool added =
              workspace.addComponent(selectedId, descriptor.name, error);
          notice = added ? (prefabEntity ? "Component added to this instance"
                                         : "Component added")
                         : error;
        } else {
          state.pendingComponentEntity = selectedId;
          state.pendingComponent = descriptor.name;
          state.pendingReferenceField =
              descriptor.editor.initialReferenceField;
          notice = "Choose the initial reference to add this component.";
        }
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
              workspace.addScriptComponent(selectedId, metadata, error);
          notice =
              added ? metadata.displayName +
                          (prefabEntity ? " added to this instance" : " added")
                    : error;
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
  if (!state.pendingComponent.empty()) {
    if (state.pendingComponentEntity != selectedId) {
      state.pendingComponentEntity.clear();
      state.pendingComponent.clear();
      state.pendingReferenceField.clear();
    } else {
      const ComponentDescriptor *pendingDescriptor =
          runtime::scene_loading::findComponentDescriptor(
              state.pendingComponent);
      const ComponentFieldDescriptor *pendingField = nullptr;
      if (pendingDescriptor != nullptr) {
        const auto found = std::ranges::find(
            pendingDescriptor->fields, state.pendingReferenceField,
            &ComponentFieldDescriptor::name);
        if (found != pendingDescriptor->fields.end())
          pendingField = &*found;
      }
      if (pendingDescriptor == nullptr || pendingField == nullptr ||
          pendingField->referenceKind == runtime::ComponentReferenceKind::None) {
        notice = "The component's initial reference metadata is invalid.";
        state.pendingComponentEntity.clear();
        state.pendingComponent.clear();
        state.pendingReferenceField.clear();
      } else {
        const auto references = editorReferenceChoices(
            pendingField->referenceKind,
            workspace.project().project.projectDirectory,
            workspace.sceneDocument().path(), workspace.sceneDocument().json(),
            workspace.sources(), pendingDescriptor->name);
        ImGui::TextWrapped("Choose %s before adding %s.",
                           runtime::scene_loading::componentFieldEditorLabel(
                               *pendingField)
                               .c_str(),
                           pendingDescriptor->editor.displayName.data());
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("##initial-component-reference",
                              "Select reference...")) {
          for (const EditorReferenceChoice &reference : references) {
            if (!ImGui::Selectable(reference.label.c_str()))
              continue;
            std::string error;
            const bool added = workspace.addComponent(
                selectedId, pendingDescriptor->name,
                {{std::string(pendingField->name), reference.id}}, error);
            notice = added ? (prefabEntity ? "Component added to this instance"
                                           : "Component added")
                           : error;
            if (added) {
              state.pendingComponentEntity.clear();
              state.pendingComponent.clear();
              state.pendingReferenceField.clear();
            }
            ImGui::EndCombo();
            ImGui::End();
            return;
          }
          ImGui::EndCombo();
        }
        if (references.empty())
          ImGui::TextDisabled("No compatible references are available.");
        if (ImGui::SmallButton("Cancel component add")) {
          state.pendingComponentEntity.clear();
          state.pendingComponent.clear();
          state.pendingReferenceField.clear();
        }
      }
    }
  }
  ImGui::End();
}

} // namespace demi::editor
