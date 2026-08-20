#include "editor/EditorGameViewPanel.h"

#include "editor/EditorPanelStyle.h"

#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <imgui.h>
#include <imgui/imgui.h>

#include <algorithm>

namespace demi::editor {

void drawEditorGameView(const EditorPlaySession &session, const ImVec2 position,
                        const ImVec2 size, const std::uint16_t textureIndex,
                        EditorViewportArea &area, bool &focused) {
  beginEditorPanel("GameView", position, size,
                   ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse |
                       ImGuiWindowFlags_NoBackground);
  const ImVec4 stateColor = session.state() == EditorPlayState::Failed
                                ? ImVec4{0.95F, 0.34F, 0.38F, 1.0F}
                            : session.state() == EditorPlayState::Paused
                                ? ImVec4{0.95F, 0.72F, 0.30F, 1.0F}
                                : ImVec4{0.35F, 0.85F, 0.55F, 1.0F};
  if (session.state() == EditorPlayState::Failed) {
    ImGui::TextColored({0.95F, 0.34F, 0.38F, 1.0F}, "Runtime failed");
    ImGui::TextWrapped("%.*s", static_cast<int>(session.failure().size()),
                       session.failure().data());
  }
  const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  area = {};
  focused = false;
  if (available.x >= 1.0F && available.y >= 1.0F) {
    area = {
        .x =
            static_cast<std::uint16_t>(std::clamp(canvasMin.x, 0.0F, 65535.0F)),
        .y =
            static_cast<std::uint16_t>(std::clamp(canvasMin.y, 0.0F, 65535.0F)),
        .width =
            static_cast<std::uint16_t>(std::clamp(available.x, 1.0F, 65535.0F)),
        .height = static_cast<std::uint16_t>(
            std::clamp(available.y, 1.0F, 65535.0F))};
    if (textureIndex != UINT16_MAX) {
      const bgfx::Caps *caps = bgfx::getCaps();
      const bool flipVertically = caps != nullptr && caps->originBottomLeft;
      ImGui::Image(bgfx::TextureHandle{textureIndex}, available,
                   flipVertically ? ImVec2{0.0F, 1.0F} : ImVec2{0.0F, 0.0F},
                   flipVertically ? ImVec2{1.0F, 0.0F} : ImVec2{1.0F, 1.0F});
    } else {
      ImGui::InvisibleButton("game-canvas", available);
    }
    focused = ImGui::IsWindowFocused();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const std::string_view state = editorPlayStateLabel(session.state());
    const ImVec2 textSize =
        ImGui::CalcTextSize(state.data(), state.data() + state.size());
    const ImVec2 badgeMin{canvasMin.x + 9.0F, canvasMin.y + 9.0F};
    const ImVec2 badgeMax{badgeMin.x + textSize.x + 18.0F,
                          badgeMin.y + textSize.y + 10.0F};
    draw->AddRectFilled(badgeMin, badgeMax, IM_COL32(36, 39, 47, 235), 3.0F);
    draw->AddText({badgeMin.x + 9.0F, badgeMin.y + 5.0F},
                  ImGui::ColorConvertFloat4ToU32(stateColor), state.data(),
                  state.data() + state.size());
    if (!session.isEmbedded()) {
      draw->AddText({canvasMin.x + 18.0F, canvasMin.y + 52.0F},
                    IM_COL32(145, 149, 162, 255),
                    "Start embedded Play to render the game here.");
    }
  }
  ImGui::End();
}

void drawRuntimeHierarchy(const runtime::World &world, const ImVec2 position,
                          const ImVec2 size, std::string &selectedEntityId) {
  beginEditorPanel("RuntimeHierarchy", position, size);
  editorSectionTitle("RUNTIME", "read-only entities");
  if (!selectedEntityId.empty() &&
      runtime::findEntity(world, selectedEntityId) == nullptr)
    selectedEntityId.clear();
  for (const runtime::Entity &entity : world.entities) {
    const std::string label = entity.name.empty() ? entity.id : entity.name;
    if (ImGui::Selectable(label.c_str(), selectedEntityId == entity.id))
      selectedEntityId = entity.id;
  }
  ImGui::End();
}

void drawRuntimeInspector(const runtime::World &world, const ImVec2 position,
                          const ImVec2 size,
                          const std::string &selectedEntityId) {
  beginEditorPanel("RuntimeInspector", position, size);
  editorSectionTitle("RUNTIME INSPECTOR", "read-only");
  const runtime::Entity *entity = runtime::findEntity(world, selectedEntityId);
  if (entity == nullptr) {
    ImGui::TextDisabled("Select a runtime entity.");
    ImGui::End();
    return;
  }
  ImGui::TextUnformatted(entity->name.c_str());
  ImGui::TextDisabled("%s", entity->id.c_str());
  ImGui::Separator();
  ImGui::Text("Enabled: %s", entity->enabled ? "true" : "false");
  ImGui::Text("Layer: %s",
              entity->layer.empty() ? "Default" : entity->layer.c_str());
  for (const runtime::scene_loading::ComponentDescriptor &descriptor :
       runtime::scene_loading::componentDescriptors()) {
    if (!descriptor.contains(*entity))
      continue;
    if (!ImGui::CollapsingHeader(
            std::string(descriptor.editor.displayName).c_str(),
            ImGuiTreeNodeFlags_DefaultOpen))
      continue;
    const nlohmann::json value = descriptor.serialize(*entity);
    if (value.empty()) {
      ImGui::TextDisabled("Runtime-owned component");
      continue;
    }
    for (const auto &[name, field] : value.items()) {
      ImGui::TextDisabled("%s", name.c_str());
      ImGui::SameLine(118.0F);
      ImGui::TextWrapped("%s", field.dump().c_str());
    }
  }
  ImGui::End();
}

} // namespace demi::editor
