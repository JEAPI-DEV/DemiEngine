#include "editor/EditorStructuredValue.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace demi::editor {
namespace {
using Json = nlohmann::json;

void capture(StructuredValueEdit &edit, bool changed) {
  if (changed) {
    edit.changed = true;
    edit.continuous = ImGui::IsItemActive();
  }
  edit.finished = edit.finished || ImGui::IsItemDeactivatedAfterEdit();
}

bool stringInput(const char *label, std::string &value,
                 ImGuiInputTextFlags flags = 0) {
  std::vector<char> buffer(value.size() + 256, '\0');
  std::copy(value.begin(), value.end(), buffer.begin());
  if (!ImGui::InputText(label, buffer.data(), buffer.size(), flags))
    return false;
  value = buffer.data();
  return true;
}

bool chooseType(Json &value) {
  bool changed = false;
  if (ImGui::BeginCombo("##type", value.type_name())) {
    for (const char *name :
         {"object", "array", "string", "number", "boolean", "null"})
      if (ImGui::Selectable(name)) {
        const std::string type(name);
        if (type == "object")
          value = Json::object();
        else if (type == "array")
          value = Json::array();
        else if (type == "string")
          value = "";
        else if (type == "number")
          value = 0.0;
        else if (type == "boolean")
          value = false;
        else
          value = nullptr;
        changed = true;
      }
    ImGui::EndCombo();
  }
  return changed;
}

void drawValue(Json &value, int vectorWidth, unsigned depth,
               StructuredValueEdit &edit, bool readOnly) {
  if (depth > 16) {
    ImGui::TextDisabled("Nested value exceeds the 16-level editor limit.");
    return;
  }
  if (!value.is_structured()) {
    ImGui::BeginDisabled(readOnly);
    bool changed = false;
    if (value.is_boolean()) {
      bool v = value.get<bool>();
      changed = ImGui::Checkbox("##value", &v);
      if (changed)
        value = v;
    } else if (value.is_number_unsigned()) {
      auto v = value.get<std::uint64_t>();
      changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &v);
      if (changed)
        value = v;
    } else if (value.is_number_integer()) {
      auto v = value.get<std::int64_t>();
      changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &v);
      if (changed)
        value = v;
    } else if (value.is_number()) {
      double v = value.get<double>();
      changed = ImGui::InputDouble("##value", &v, 0, 0, "%.3f");
      if (changed)
        value = v;
    } else if (value.is_string()) {
      auto v = value.get<std::string>();
      changed = stringInput("##value", v);
      if (changed)
        value = std::move(v);
    } else
      changed = chooseType(value);
    capture(edit, changed);
    ImGui::EndDisabled();
    return;
  }

  const bool object = value.is_object();
  if (!ImGui::TreeNodeEx("##collection", ImGuiTreeNodeFlags_SpanAvailWidth,
                         "%zu %s", value.size(), object ? "fields" : "items"))
    return;
  constexpr int PageSize = 64;
  auto *storage = ImGui::GetStateStorage();
  const auto pageId = ImGui::GetID("page");
  const int lastPage =
      value.empty() ? 0 : static_cast<int>((value.size() - 1) / PageSize);
  int page = std::clamp(storage->GetInt(pageId, 0), 0, lastPage);
  if (lastPage > 0) {
    ImGui::BeginDisabled(page == 0);
    if (ImGui::SmallButton("Previous"))
      --page;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("Page %d / %d", page + 1, lastPage + 1);
    ImGui::SameLine();
    ImGui::BeginDisabled(page == lastPage);
    if (ImGui::SmallButton("Next"))
      ++page;
    ImGui::EndDisabled();
  }
  storage->SetInt(pageId, page);
  const auto begin = std::min(value.size(), std::size_t(page * PageSize));
  const auto end = std::min(value.size(), begin + PageSize);
  auto iterator = value.begin();
  std::advance(iterator, static_cast<std::ptrdiff_t>(begin));
  for (std::size_t index = begin; index < end; ++index, ++iterator) {
    const std::string key = object ? iterator.key() : std::to_string(index);
    ImGui::PushID(key.c_str());
    ImGui::TextUnformatted(key.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(readOnly);
    bool erase = ImGui::SmallButton("Remove");
    std::string renamed = key;
    bool rename = false;
    if (object) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Rename"))
        ImGui::OpenPopup("Rename field");
      if (ImGui::BeginPopup("Rename field")) {
        ImGui::TextUnformatted("New key (Enter to apply)");
        if (stringInput("##key", renamed,
                        ImGuiInputTextFlags_EnterReturnsTrue) &&
            !renamed.empty() && !value.contains(renamed)) {
          rename = true;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }
    ImGui::EndDisabled();
    if (erase || rename) {
      if (rename)
        value[renamed] = *iterator;
      if (object)
        value.erase(key);
      else
        value.erase(value.begin() + static_cast<Json::difference_type>(index));
      edit.changed = true;
      edit.continuous = false;
      edit.finished = true;
      ImGui::PopID();
      break;
    }
    ImGui::SetNextItemWidth(-1);
    if (!object && vectorWidth && iterator->is_array() &&
        iterator->size() == std::size_t(vectorWidth)) {
      ImGui::BeginDisabled(readOnly);
      std::array<float, 3> v{};
      for (int axis = 0; axis < vectorWidth; ++axis)
        v[axis] = (*iterator)[axis].get<float>();
      const bool changed =
          vectorWidth == 2 ? ImGui::InputFloat2("##value", v.data(), "%.3f")
                           : ImGui::InputFloat3("##value", v.data(), "%.3f");
      if (changed)
        for (int axis = 0; axis < vectorWidth; ++axis)
          (*iterator)[axis] = v[axis];
      capture(edit, changed);
      ImGui::EndDisabled();
    } else {
      drawValue(*iterator, 0, depth + 1, edit, readOnly);
      if (!readOnly && ImGui::BeginPopupContextItem("Value type")) {
        if (chooseType(*iterator)) {
          edit.changed = true;
          edit.finished = true;
          edit.continuous = false;
        }
        ImGui::EndPopup();
      }
    }
    ImGui::PopID();
  }
  ImGui::BeginDisabled(readOnly);
  if (ImGui::SmallButton(object ? "Add field" : "Add item")) {
    if (object) {
      std::string key = "new_field";
      for (int suffix = 2; value.contains(key); ++suffix)
        key = "new_field_" + std::to_string(suffix);
      value[key] = nullptr;
    } else
      value.push_back(vectorWidth ? Json(std::vector<double>(vectorWidth, 0))
                                  : Json{});
    storage->SetInt(pageId, static_cast<int>((value.size() - 1) / PageSize));
    edit.changed = true;
    edit.continuous = false;
    edit.finished = true;
  }
  ImGui::EndDisabled();
  ImGui::TreePop();
}
} // namespace

StructuredValueEdit drawStructuredValue(nlohmann::json &value, int vectorWidth,
                                        bool readOnly) {
  StructuredValueEdit edit;
  drawValue(value, vectorWidth, 0, edit, readOnly);
  return edit;
}
} // namespace demi::editor
