#include "editor/EditorJsonInspector.h"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace demi::editor {
namespace {

std::string pointerToken(std::string value) {
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '~') {
      value.replace(index, 1, "~0");
      ++index;
    } else if (value[index] == '/') {
      value.replace(index, 1, "~1");
      ++index;
    }
  }
  return value;
}

void drawJsonTree(const nlohmann::json &value, const std::string &label,
                  const std::string &pointer, std::string &selection) {
  ImGui::PushID(pointer.c_str());
  if (value.is_object() || value.is_array()) {
    const bool open = ImGui::TreeNodeEx(
        "##node",
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen,
        "%s (%zu)", label.c_str(), value.size());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
      selection = pointer;
    if (open) {
      if (value.is_object())
        for (const auto &[name, child] : value.items())
          drawJsonTree(child, name, pointer + "/" + pointerToken(name),
                       selection);
      else
        for (std::size_t index = 0; index < value.size(); ++index)
          drawJsonTree(value[index], std::to_string(index),
                       pointer + "/" + std::to_string(index), selection);
      ImGui::TreePop();
    }
  } else {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selection == pointer)
      flags |= ImGuiTreeNodeFlags_Selected;
    ImGui::TreeNodeEx("##value", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked())
      selection = pointer;
  }
  ImGui::PopID();
}

void drawScalarEditor(EditorJsonDocument &document, const std::string &pointer,
                      std::array<char, 1024> &editBuffer,
                      std::string &editBufferPointer, std::string &notice) {
  if (pointer.empty()) {
    ImGui::TextDisabled("Select a scalar field in the source tree.");
    return;
  }
  const nlohmann::json *value = nullptr;
  try {
    value = &document.json().at(nlohmann::json::json_pointer(pointer));
  } catch (const nlohmann::json::exception &) {
    ImGui::TextDisabled("The selected field no longer exists.");
    return;
  }
  ImGui::TextWrapped("%s", pointer.c_str());
  ImGui::Separator();
  std::string error;
  if (value->is_boolean()) {
    bool edited = value->get<bool>();
    if (ImGui::Checkbox("Value", &edited))
      notice =
          document.set(pointer, edited, error) ? "Document modified" : error;
  } else if (value->is_number_integer()) {
    int edited = value->get<int>();
    if (ImGui::InputInt("Value", &edited))
      notice =
          document.set(pointer, edited, error) ? "Document modified" : error;
  } else if (value->is_number()) {
    double edited = value->get<double>();
    if (ImGui::InputDouble("Value", &edited, 0.1, 1.0, "%.4f"))
      notice =
          document.set(pointer, edited, error) ? "Document modified" : error;
  } else if (value->is_string()) {
    if (editBufferPointer != pointer) {
      editBuffer.fill('\0');
      const std::string current = value->get<std::string>();
      std::copy_n(current.data(),
                  std::min(current.size(), editBuffer.size() - 1),
                  editBuffer.data());
      editBufferPointer = pointer;
    }
    if (ImGui::InputText("Value", editBuffer.data(), editBuffer.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue))
      notice = document.set(pointer, std::string(editBuffer.data()), error)
                   ? "Document modified"
                   : error;
  } else if (value->is_null()) {
    ImGui::TextDisabled("null");
  } else if (value->is_array()) {
    if (ImGui::Button("Append item")) {
      nlohmann::json replacement = *value;
      replacement.push_back("");
      notice = document.set(pointer, std::move(replacement), error)
                   ? "Array item appended"
                   : error;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(value->empty());
    if (ImGui::Button("Remove last")) {
      nlohmann::json replacement = *value;
      replacement.erase(std::prev(replacement.end()));
      notice = document.set(pointer, std::move(replacement), error)
                   ? "Array item removed"
                   : error;
    }
    ImGui::EndDisabled();
  } else if (value->is_object()) {
    if (ImGui::Button("Add field")) {
      nlohmann::json replacement = *value;
      std::string name = "new_field";
      for (int suffix = 2; replacement.contains(name); ++suffix)
        name = "new_field_" + std::to_string(suffix);
      replacement[name] = "";
      notice = document.set(pointer, std::move(replacement), error)
                   ? "Object field added"
                   : error;
    }
  } else {
    ImGui::TextWrapped("Collection editing uses the specialized controls.");
  }
}

} // namespace

void drawEditorJsonSource(EditorJsonDocument &document,
                          std::string &selectedPointer,
                          std::array<char, 1024> &editBuffer,
                          std::string &editBufferPointer, std::string &notice) {
  ImGui::BeginChild("specialized-tree", {390.0F, 0.0F},
                    ImGuiChildFlags_Borders);
  drawJsonTree(document.json(), document.path().filename().string(), "",
               selectedPointer);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("specialized-properties", {0.0F, 0.0F},
                    ImGuiChildFlags_Borders);
  drawScalarEditor(document, selectedPointer, editBuffer, editBufferPointer,
                   notice);
  drawEditorDocumentDiagnostics(document.diagnostics());
  ImGui::EndChild();
}

void drawEditorDocumentDiagnostics(const Diagnostics &diagnostics) {
  for (const Diagnostic &diagnostic : diagnostics) {
    const ImVec4 color = diagnostic.severity == Severity::Error
                             ? ImVec4{0.95F, 0.34F, 0.38F, 1.0F}
                             : ImVec4{0.95F, 0.72F, 0.30F, 1.0F};
    ImGui::TextColored(color, "%s", diagnostic.code.c_str());
    ImGui::SameLine();
    ImGui::TextWrapped("%s", diagnostic.message.c_str());
  }
}

} // namespace demi::editor
