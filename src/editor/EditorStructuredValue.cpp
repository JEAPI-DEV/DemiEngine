#include "editor/EditorStructuredValue.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace demi::editor {
namespace {
using Json = nlohmann::json;
constexpr std::array TypeNames{"Object",  "Array",   "Text", "Number",
                               "Integer", "Boolean", "Null"};

int valueType(const Json &value) {
  if (value.is_object())
    return 0;
  if (value.is_array())
    return 1;
  if (value.is_string())
    return 2;
  if (value.is_number_integer())
    return 4;
  if (value.is_number())
    return 3;
  if (value.is_boolean())
    return 5;
  return 6;
}

Json initialValue(int type) {
  switch (type) {
  case 0:
    return Json::object();
  case 1:
    return Json::array();
  case 2:
    return "";
  case 3:
    return 0.0;
  case 4:
    return 0;
  case 5:
    return false;
  default:
    return nullptr;
  }
}

void capture(StructuredValueEdit &edit, bool changed) {
  if (changed) {
    edit.changed = true;
    edit.continuous = ImGui::IsItemActive();
  }
  edit.finished |= ImGui::IsItemDeactivatedAfterEdit();
}

void structuralEdit(StructuredValueEdit &edit) {
  edit.changed = true;
  edit.continuous = false;
  edit.finished = true;
}

bool drawFieldDialog(StructuredValueState &state, const Json &container,
                     const std::string &oldKey, bool adding) {
  if (container.is_object()) {
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##field-name", &state.key);
  }
  ImGui::TextUnformatted("Type");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo("##type", TypeNames[state.type])) {
    for (int option = 0; option < static_cast<int>(TypeNames.size());
         ++option) {
      if (ImGui::Selectable(TypeNames[option], option == state.type))
        state.type = option;
    }
    ImGui::EndCombo();
  }
  if (!adding)
    ImGui::TextWrapped("Changing the type replaces the current value.");
  if (!state.error.empty())
    ImGui::TextWrapped("%s", state.error.c_str());
  if (ImGui::Button(adding ? "Add" : "Apply")) {
    if (container.is_object() && state.key.empty())
      state.error = "Enter a field name.";
    else if (container.is_object() && state.key != oldKey &&
             container.contains(state.key))
      state.error = "A field with that name already exists.";
    else {
      ImGui::CloseCurrentPopup();
      return true;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel"))
    ImGui::CloseCurrentPopup();
  return false;
}

void drawScalar(Json &value, StructuredValueEdit &edit) {
  ImGui::SetNextItemWidth(-1);
  bool changed = false;
  if (value.is_boolean()) {
    bool candidate = value.get<bool>();
    changed = ImGui::Checkbox("##value", &candidate);
    if (changed)
      value = candidate;
  } else if (value.is_number_unsigned()) {
    auto candidate = value.get<std::uint64_t>();
    changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &candidate);
    if (changed)
      value = candidate;
  } else if (value.is_number_integer()) {
    auto candidate = value.get<std::int64_t>();
    changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &candidate);
    if (changed)
      value = candidate;
  } else if (value.is_number()) {
    double candidate = value.get<double>();
    changed = ImGui::InputDouble("##value", &candidate, 0, 0, "%.9g");
    if (changed)
      value = candidate;
  } else if (value.is_string()) {
    auto candidate = value.get<std::string>();
    changed = ImGui::InputText("##value", &candidate);
    if (changed)
      value = std::move(candidate);
  } else {
    ImGui::TextDisabled("Null — use Edit to choose a type");
  }
  capture(edit, changed);
}

void drawValue(Json &value, StructuredValueState &state, int vectorWidth,
               StructuredValueEdit &edit, bool root, bool readOnly) {
  if (!value.is_structured()) {
    ImGui::BeginDisabled(readOnly);
    drawScalar(value, edit);
    ImGui::EndDisabled();
    return;
  }
  const bool object = value.is_object();
  const auto flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                     (root ? ImGuiTreeNodeFlags_DefaultOpen : 0);
  if (!ImGui::TreeNodeEx("##collection", flags, "%zu %s", value.size(),
                         object ? "fields" : "items"))
    return;
  // Paging bounds per-frame widget work, not the amount of authored data.
  constexpr std::size_t PageSize = 32;
  auto *storage = ImGui::GetStateStorage();
  const auto pageId = ImGui::GetID("page");
  const int lastPage =
      value.empty() ? 0 : static_cast<int>((value.size() - 1) / PageSize);
  int page = std::clamp(storage->GetInt(pageId), 0, lastPage);
  if (lastPage > 0) {
    ImGui::BeginDisabled(page == 0);
    if (ImGui::SmallButton("Previous"))
      --page;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("%d / %d", page + 1, lastPage + 1);
    ImGui::SameLine();
    ImGui::BeginDisabled(page == lastPage);
    if (ImGui::SmallButton("Next"))
      ++page;
    ImGui::EndDisabled();
  }
  storage->SetInt(pageId, page);
  const auto begin = std::min(value.size(), std::size_t(page) * PageSize);
  const auto end = std::min(value.size(), begin + PageSize);
  auto entry = value.begin();
  std::advance(entry, static_cast<Json::difference_type>(begin));
  for (std::size_t index = begin; index < end; ++index, ++entry) {
    const std::string key = object ? entry.key() : std::to_string(index);
    ImGui::PushID(key.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("%s", key.c_str());
    ImGui::TextDisabled("%s", TypeNames[valueType(*entry)]);
    ImGui::BeginDisabled(readOnly);
    if (ImGui::SmallButton("Edit")) {
      state.key = key;
      state.type = valueType(*entry);
      state.error.clear();
      ImGui::OpenPopup("Edit field");
    }
    bool edited = false;
    if (ImGui::BeginPopup("Edit field")) {
      if (drawFieldDialog(state, value, key, false)) {
        const auto replacement =
            state.type == valueType(*entry) ? *entry : initialValue(state.type);
        if (object && key != state.key) {
          value[state.key] = replacement;
          value.erase(key);
        } else {
          *entry = replacement;
        }
        edited = true;
      }
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    const bool erase = ImGui::SmallButton("Remove");
    bool moved = false;
    if (!object) {
      ImGui::SameLine();
      ImGui::BeginDisabled(index == 0);
      if (ImGui::SmallButton("Up")) {
        std::swap(value[index], value[index - 1]);
        moved = true;
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(index + 1 == value.size());
      if (ImGui::SmallButton("Down")) {
        std::swap(value[index], value[index + 1]);
        moved = true;
      }
      ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    if (erase || moved || edited) {
      if (erase) {
        if (object)
          value.erase(key);
        else
          value.erase(value.begin() +
                      static_cast<Json::difference_type>(index));
      }
      structuralEdit(edit);
      ImGui::PopID();
      break;
    }
    if (!object && vectorWidth > 0 && entry->is_array() &&
        entry->size() == static_cast<std::size_t>(vectorWidth) &&
        std::ranges::all_of(
            *entry, [](const Json &item) { return item.is_number(); })) {
      std::array<float, 3> values{};
      for (int axis = 0; axis < vectorWidth; ++axis)
        values[axis] = (*entry)[axis].get<float>();
      ImGui::SetNextItemWidth(-1);
      ImGui::BeginDisabled(readOnly);
      const bool changed =
          vectorWidth == 2
              ? ImGui::InputFloat2("##value", values.data(), "%.5g")
              : ImGui::InputFloat3("##value", values.data(), "%.5g");
      if (changed)
        for (int axis = 0; axis < vectorWidth; ++axis)
          (*entry)[axis] = values[axis];
      capture(edit, changed);
      ImGui::EndDisabled();
    } else {
      drawValue(*entry, state, 0, edit, false, readOnly);
    }
    ImGui::PopID();
  }
  ImGui::Separator();
  ImGui::BeginDisabled(readOnly);
  if (ImGui::Button(object ? "Add field..." : "Add item...")) {
    state.key.clear();
    state.type = 2;
    state.error.clear();
    ImGui::OpenPopup("Add field");
  }
  if (ImGui::BeginPopup("Add field")) {
    if (drawFieldDialog(state, value, {}, true)) {
      if (object) {
        value[state.key] = initialValue(state.type);
        const auto index = std::distance(value.begin(), value.find(state.key));
        storage->SetInt(pageId, static_cast<int>(index / PageSize));
      } else {
        value.push_back(vectorWidth > 0
                            ? Json(std::vector<double>(vectorWidth, 0))
                            : initialValue(state.type));
        storage->SetInt(pageId,
                        static_cast<int>((value.size() - 1) / PageSize));
      }
      structuralEdit(edit);
    }
    ImGui::EndPopup();
  }
  ImGui::EndDisabled();
  ImGui::TreePop();
}
} // namespace

StructuredValueEdit drawStructuredValue(Json &value,
                                        StructuredValueState &state,
                                        int vectorWidth, bool readOnly) {
  StructuredValueEdit edit;
  drawValue(value, state, vectorWidth, edit, true, readOnly);
  return edit;
}
} // namespace demi::editor
