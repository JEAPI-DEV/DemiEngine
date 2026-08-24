#include "editor/EditorAnimationMachinePanel.h"

#include "editor/EditorWorkspace.h"

#include "demi/runtime/animation/AnimationRuntime.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include <imgui.h>

#include <algorithm>
#include <nlohmann/json.hpp>

namespace demi::editor {

bool EditorAnimationMachinePanel::canOpen(
    const EditorWorkspace &workspace) const {
  const runtime::Entity *entity = workspace.selectedEntity();
  return entity != nullptr &&
         entity->component<runtime::AnimationStateMachineComponent>() !=
             nullptr;
}

void EditorAnimationMachinePanel::open(const EditorWorkspace &workspace) {
  if (!canOpen(workspace))
    return;
  entityId_ = std::string(workspace.selectedEntityId());
}

void EditorAnimationMachinePanel::draw(EditorWorkspace &workspace,
                                       std::string &notice) {
  if (entityId_.empty())
    return;
  ImGui::SetNextWindowSize({760.0F, 610.0F}, ImGuiCond_Appearing);
  if (!ImGui::Begin("Animation State Machine", nullptr,
                    ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return;
  }
  const runtime::Entity *entity =
      runtime::findEntity(workspace.project().world, entityId_);
  const auto *machine =
      entity == nullptr
          ? nullptr
          : entity->component<runtime::AnimationStateMachineComponent>();
  const nlohmann::json *component =
      workspace.sceneDocument().component(entityId_, "AnimationStateMachine");
  if (machine == nullptr || component == nullptr) {
    ImGui::TextDisabled("The animation state machine no longer exists.");
    if (ImGui::Button("Close")) {
      entityId_.clear();
    }
    ImGui::End();
    return;
  }

  ImGui::Text("%s", entity->name.c_str());
  const runtime::AnimationPreview preview = runtime::animationPreview(*machine);
  ImGui::Text("Runtime preview: %s  %.2f", preview.state.c_str(),
              preview.normalizedTime);
  ImGui::ProgressBar(preview.normalizedTime, {260.0F, 0.0F});
  for (const std::string &error : runtime::validateAnimationMachine(*machine))
    ImGui::TextColored({0.95F, 0.34F, 0.38F, 1.0F}, "%s", error.c_str());
  ImGui::Separator();

  nlohmann::json states = component->value("states", nlohmann::json::object());
  ImGui::TextUnformatted("States");
  std::optional<std::string> removeState;
  for (const auto &[name, state] : states.items()) {
    ImGui::PushID(name.c_str());
    ImGui::Text("%s", name.c_str());
    ImGui::SameLine(180.0F);
    ImGui::TextDisabled("%s  %.2fs%s",
                        state.value("model_clip_name", "").c_str(),
                        state.value("duration", 0.0F),
                        state.value("loop", true) ? " loop" : "");
    ImGui::SameLine(650.0F);
    if (ImGui::SmallButton("Remove"))
      removeState = name;
    ImGui::PopID();
  }
  if (removeState) {
    states.erase(*removeState);
    std::string error;
    notice = workspace.editValue({.entityId = entityId_,
                                  .component = "AnimationStateMachine",
                                  .field = "states"},
                                 std::move(states), false, error)
                 ? "Animation state removed"
                 : error;
    ImGui::End();
    return;
  }
  ImGui::SetNextItemWidth(140.0F);
  ImGui::InputTextWithHint("##state-name", "state name", stateName_.data(),
                           stateName_.size());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180.0F);
  ImGui::InputTextWithHint("##model-clip", "model clip", modelClip_.data(),
                           modelClip_.size());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100.0F);
  ImGui::InputFloat("##state-duration", &duration_, 0.1F, 1.0F, "%.2f");
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &loop_);
  ImGui::SameLine();
  const bool canAdd =
      stateName_[0] != '\0' && modelClip_[0] != '\0' && duration_ > 0.0F;
  ImGui::BeginDisabled(!canAdd);
  bool stateChanged = false;
  if (ImGui::Button("Add state")) {
    if (states.contains(stateName_.data())) {
      notice = "An animation state with that name already exists.";
    } else {
      states[stateName_.data()] = {{"model_clip_name", modelClip_.data()},
                                   {"duration", duration_},
                                   {"speed", 1.0},
                                   {"loop", loop_}};
      std::string error;
      if (workspace.editValue({.entityId = entityId_,
                               .component = "AnimationStateMachine",
                               .field = "states"},
                              std::move(states), false, error)) {
        notice = "Animation state added";
        stateName_.fill('\0');
        modelClip_.fill('\0');
        stateChanged = true;
      } else
        notice = error;
    }
  }
  ImGui::EndDisabled();
  if (stateChanged) {
    ImGui::End();
    return;
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Initial state");
  ImGui::SameLine();
  const std::string initial = component->value("initial_state", "");
  if (ImGui::BeginCombo("##initial-state", initial.c_str())) {
    for (const auto &[name, unused] : states.items()) {
      (void)unused;
      if (ImGui::Selectable(name.c_str(), initial == name)) {
        std::string error;
        notice = workspace.editValue({.entityId = entityId_,
                                      .component = "AnimationStateMachine",
                                      .field = "initial_state"},
                                     name, false, error)
                     ? "Initial animation state changed"
                     : error;
        ImGui::EndCombo();
        ImGui::End();
        return;
      }
    }
    ImGui::EndCombo();
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Transitions");
  for (const nlohmann::json &transition :
       component->value("transitions", nlohmann::json::array()))
    ImGui::BulletText("%s -> %s", transition.value("from", "*").c_str(),
                      transition.value("to", "").c_str());
  ImGui::SetNextItemWidth(150.0F);
  ImGui::InputTextWithHint("##transition-from", "from or *",
                           transitionFrom_.data(), transitionFrom_.size());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150.0F);
  ImGui::InputTextWithHint("##transition-to", "to", transitionTo_.data(),
                           transitionTo_.size());
  ImGui::SameLine();
  if (ImGui::Button("Add transition")) {
    auto transitions = component->value("transitions", nlohmann::json::array());
    transitions.push_back(
        {{"from", transitionFrom_[0] == '\0' ? "*" : transitionFrom_.data()},
         {"to", transitionTo_.data()},
         {"condition", "always"},
         {"blend_duration", 0.15}});
    std::string error;
    if (workspace.editValue({.entityId = entityId_,
                             .component = "AnimationStateMachine",
                             .field = "transitions"},
                            std::move(transitions), false, error)) {
      notice = "Animation transition added";
      transitionFrom_.fill('\0');
      transitionTo_.fill('\0');
    } else
      notice = error;
    ImGui::End();
    return;
  }

  ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 45.0F);
  if (ImGui::Button("Close")) {
    entityId_.clear();
  }
  ImGui::End();
}

} // namespace demi::editor
