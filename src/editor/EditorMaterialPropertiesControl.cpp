#include "editor/EditorMaterialPropertiesControl.h"
#include "editor/EditorColorControl.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>

namespace demi::editor {

StructuredValueEdit drawEditorMaterialProperties(nlohmann::json &properties) {
  StructuredValueEdit result;
  if (!properties.is_object())
    properties = nlohmann::json::object();
  if (properties.empty())
    ImGui::TextDisabled("No overrides. Using the default surface.");
  for (auto entry = properties.begin(); entry != properties.end(); ++entry) {
    ImGui::PushID(entry.key().c_str());
    ImGui::TextUnformatted(entry.key().c_str());
    ImGui::SameLine();
    const bool remove = ImGui::SmallButton("Remove");
    ImGui::SetNextItemWidth(-1.0F);
    bool changed = false;
    const bool scalar = entry.key() == "metallic" ||
                        entry.key() == "roughness" || entry.key() == "opacity";
    if (scalar && entry.value().is_number()) {
      float value = entry.value().get<float>();
      changed = ImGui::SliderFloat("##value", &value, 0, 1, "%.3f");
      if (changed)
        entry.value() = value;
    } else if (entry.key() == "base_color" && entry.value().is_array() &&
               entry.value().size() == 4 &&
               std::ranges::all_of(entry.value(), [](const auto &channel) {
                 return channel.is_number();
               })) {
      auto value = entry.value().get<std::array<float, 4>>();
      changed = drawEditorColorControl("##value", value.data());
      if (changed)
        entry.value() = value;
    } else {
      ImGui::TextWrapped("Unsupported value; preserved until you remove it.");
    }
    result.changed |= changed;
    result.continuous |= changed && ImGui::IsItemActive();
    result.finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
    if (remove) {
      properties.erase(entry);
      result = {.changed = true, .finished = true};
      break;
    }
  }
  if (ImGui::Button("Add material override"))
    ImGui::OpenPopup("material-override");
  if (ImGui::BeginPopup("material-override")) {
    struct Property {
      const char *key;
      const char *label;
      float value;
    };
    constexpr Property choices[]{{"base_color", "Base colour", 1},
                                 {"metallic", "Metallic", 0},
                                 {"roughness", "Roughness", .8F},
                                 {"opacity", "Opacity", 1}};
    for (const auto &choice : choices) {
      if (ImGui::MenuItem(choice.label, nullptr, false,
                          !properties.contains(choice.key))) {
        properties[choice.key] = std::string_view(choice.key) == "base_color"
                                     ? nlohmann::json::array({1, 1, 1, 1})
                                     : nlohmann::json(choice.value);
        result = {.changed = true, .finished = true};
      }
    }
    ImGui::EndPopup();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "Override the material asset for this mesh only. Explicit\n"
        "Metallic, Roughness and Opacity fields take precedence.");
  return result;
}
} // namespace demi::editor
