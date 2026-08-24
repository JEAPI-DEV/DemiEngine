#include "editor/EditorSpecializedPanel.h"

#include "editor/EditorJsonInspector.h"

#include "editor/EditorWorkspace.h"

#include "demi/assets/DataDocument.h"
#include "demi/assets/RenderAsset.h"
#include "demi/runtime/animation/AnimationRuntime.h"
#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace demi::editor {
namespace {

void drawPrefabPreview(EditorSpecializedDocument &editor, std::string &notice) {
  const nlohmann::json &expanded = editor.expandedPrefab();
  if (expanded.empty()) {
    ImGui::TextDisabled("Prefab expansion is unavailable.");
    return;
  }
  ImGui::Text("Expanded entities: %zu",
              expanded.value("entities", nlohmann::json::array()).size());
  if (ImGui::BeginTable("prefab-preview", 2,
                        ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Expanded stable ID");
    ImGui::TableSetupColumn("Name");
    ImGui::TableHeadersRow();
    for (const nlohmann::json &entity :
         expanded.value("entities", nlohmann::json::array())) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(entity.value("id", "").c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(entity.value("name", "").c_str());
    }
    ImGui::EndTable();
  }
  ImGui::SeparatorText("Source / expanded diff");
  if (editor.prefabDiff().empty())
    ImGui::TextDisabled("No expansion differences.");
  for (const nlohmann::json &entry : editor.prefabDiff())
    ImGui::BulletText("%s %s", entry.value("op", "").c_str(),
                      entry.value("path", "").c_str());
  const auto instances = editor.document().json().find("instances");
  if (instances != editor.document().json().end() && instances->is_array()) {
    ImGui::SeparatorText("Nested prefab overrides");
    for (std::size_t index = 0; index < instances->size(); ++index) {
      const nlohmann::json &instance = (*instances)[index];
      if (!instance.is_object() || !instance.contains("overrides"))
        continue;
      ImGui::PushID(static_cast<int>(index));
      ImGui::Text("%s -> %s", instance.value("id", "").c_str(),
                  instance.value("prefab", "").c_str());
      ImGui::SameLine(430.0F);
      if (ImGui::SmallButton("Apply to source")) {
        std::string error;
        notice = editor.applyPrefabOverrides(index, error)
                     ? "Prefab overrides applied atomically"
                     : error;
        ImGui::PopID();
        return;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Revert")) {
        std::string error;
        notice = editor.revertPrefabOverrides(index, error)
                     ? "Prefab overrides reverted"
                     : error;
        ImGui::PopID();
        return;
      }
      ImGui::PopID();
    }
  }
}

void drawHudPreview(const runtime::ui::UiDocument &hud) {
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float scale = std::min(available.x / std::max(hud.canvasSize.x, 1.0F),
                               available.y / std::max(hud.canvasSize.y, 1.0F));
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 canvas{hud.canvasSize.x * scale, hud.canvasSize.y * scale};
  ImGui::InvisibleButton("hud-preview", canvas);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(origin, {origin.x + canvas.x, origin.y + canvas.y},
                      IM_COL32(18, 21, 27, 255));
  for (const runtime::ui::UiNode &node : hud.nodes) {
    if (!node.visible)
      continue;
    const ImVec2 min{origin.x + node.resolved.x * scale,
                     origin.y + node.resolved.y * scale};
    const ImVec2 max{min.x + node.resolved.width * scale,
                     min.y + node.resolved.height * scale};
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(
        {node.backgroundColor.r, node.backgroundColor.g, node.backgroundColor.b,
         std::max(node.backgroundColor.a, 0.08F)});
    draw->AddRectFilled(min, max, fill);
    draw->AddRect(min, max, IM_COL32(116, 94, 175, 210));
    draw->AddText({min.x + 3.0F, min.y + 2.0F}, IM_COL32(220, 222, 230, 255),
                  node.id.c_str());
  }
}

void drawMaterialPreview(const EditorJsonDocument &document) {
  Diagnostics diagnostics;
  const auto material = assets::parseMaterialAsset(
      document.json(), document.path(), &diagnostics);
  ImVec4 preview{0.68F, 0.58F, 0.88F, 1.0F};
  if (material)
    if (const auto color = material->colors.find("base_color");
        color != material->colors.end())
      preview = {color->second.r, color->second.g, color->second.b,
                 color->second.a};
  const ImVec2 center{ImGui::GetCursorScreenPos().x + 170.0F,
                      ImGui::GetCursorScreenPos().y + 170.0F};
  ImGui::InvisibleButton("material-preview", {340.0F, 340.0F});
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddCircleFilled(center, 118.0F, ImGui::ColorConvertFloat4ToU32(preview),
                        64);
  draw->AddCircle(center, 118.0F, IM_COL32(220, 220, 230, 180), 64, 2.0F);
  ImGui::Text("Shader: %s",
              material ? material->shader.c_str() : "invalid material");
  ImGui::TextDisabled(
      "Preview uses the runtime material parser and descriptor.");
}

void drawMaterialControls(EditorSpecializedDocument &editor,
                          std::string &notice) {
  auto &document = editor.document();
  const auto render =
      document.json().value("render_state", nlohmann::json::object());
  std::string blend = render.value("blend", "opaque");
  if (ImGui::BeginCombo("Blend", blend.c_str())) {
    for (const char *choice : {"opaque", "alpha", "additive"})
      if (ImGui::Selectable(choice, blend == choice)) {
        std::string error;
        notice = document.set("/render_state/blend", choice, error)
                     ? "Material blend modified"
                     : error;
      }
    ImGui::EndCombo();
  }
  std::string cull = render.value("cull", "back");
  if (ImGui::BeginCombo("Cull", cull.c_str())) {
    for (const char *choice : {"back", "front", "none"})
      if (ImGui::Selectable(choice, cull == choice)) {
        std::string error;
        notice = document.set("/render_state/cull", choice, error)
                     ? "Material culling modified"
                     : error;
      }
    ImGui::EndCombo();
  }
  auto parameters =
      document.json().value("parameters", nlohmann::json::object());
  const auto color = parameters.find("base_color");
  if (color != parameters.end() && color->is_array() && color->size() == 4) {
    std::array<float, 4> edited{
        (*color)[0].get<float>(), (*color)[1].get<float>(),
        (*color)[2].get<float>(), (*color)[3].get<float>()};
    if (ImGui::ColorEdit4("Base color", edited.data())) {
      std::string error;
      notice = document.set("/parameters/base_color", edited, error)
                   ? "Material color modified"
                   : error;
    }
  }
}

void drawAnimationPreview(EditorSpecializedDocument &editor) {
  const auto settings =
      editor.document().json().value("settings", nlohmann::json::object());
  const auto clips = settings.value("animations", nlohmann::json::object())
                         .value("clips", nlohmann::json::array());
  ImGui::Text("Imported clips: %zu", clips.size());
  for (const nlohmann::json &clip : clips)
    ImGui::BulletText("%s  [%s]", clip.value("name", "unnamed").c_str(),
                      clip.value("skeleton", "default").c_str());
  if (!clips.empty() && clips.front().is_object()) {
    const std::string name = clips.front().value("name", "preview");
    const float duration =
        std::max(clips.front().value("duration", 1.0F), 0.01F);
    runtime::AnimationStateMachineComponent machine;
    machine.states[name] = {.modelClipName = name,
                            .duration = duration,
                            .loop = clips.front().value("loop", true)};
    machine.state = name;
    machine.time = std::fmod(static_cast<float>(ImGui::GetTime()), duration);
    machine.normalizedTime = machine.time / duration;
    const runtime::AnimationPreview preview =
        runtime::animationPreview(machine);
    ImGui::Text("Runtime preview: %s  %.2fs", preview.state.c_str(),
                preview.time);
    ImGui::ProgressBar(preview.normalizedTime, {320.0F, 0.0F});
  }
}

void drawAnimationControls(EditorSpecializedDocument &editor,
                           std::string &notice) {
  auto &document = editor.document();
  auto settings = document.json().value("settings", nlohmann::json::object());
  auto animations = settings.value("animations", nlohmann::json::object());
  auto clips = animations.value("clips", nlohmann::json::array());
  if (ImGui::Button("Add clip")) {
    std::set<std::string> names;
    for (const auto &clip : clips)
      names.insert(clip.value("name", ""));
    std::string name = "Clip";
    for (int suffix = 2; names.contains(name); ++suffix)
      name = "Clip" + std::to_string(suffix);
    clips.push_back({{"name", name},
                     {"skeleton", "default"},
                     {"duration", 1.0},
                     {"loop", true}});
    std::string error;
    notice = document.set("/settings/animations/clips", clips, error)
                 ? "Animation clip added"
                 : error;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(clips.empty());
  if (ImGui::Button("Remove last clip")) {
    clips.erase(std::prev(clips.end()));
    std::string error;
    notice = document.set("/settings/animations/clips", clips, error)
                 ? "Animation clip removed"
                 : error;
  }
  ImGui::EndDisabled();
}

void drawAudioControls(EditorSpecializedDocument &editor, std::string &notice) {
  auto &document = editor.document();
  const auto settings =
      document.json().value("settings", nlohmann::json::object());
  bool streaming = settings.value("streaming", false);
  if (ImGui::Checkbox("Stream from disk", &streaming)) {
    std::string error;
    notice = document.set("/settings/streaming", streaming, error)
                 ? "Audio streaming modified"
                 : error;
  }
}

void drawDataPreview(const EditorJsonDocument &document) {
  const auto parsed =
      assets::parseDataDocument(document.json().dump(), document.path());
  if (!parsed.document) {
    drawEditorDocumentDiagnostics(parsed.diagnostics);
    return;
  }
  ImGui::Text("Elements: %zu", parsed.document->elementCount());
  ImGui::Text("Canonical bytes: %zu", parsed.document->byteSize());
  ImGui::TextDisabled("Arrays and objects preserve their authored JSON shape.");
}

} // namespace

bool EditorSpecializedPanel::open(const std::filesystem::path &source,
                                  const EditorAssetIndex &assets,
                                  std::string &error) {
  EditorSpecializedDocument document;
  if (!document.open(source, assets, error))
    return false;
  active_ = std::move(document);
  selectedPointer_.clear();
  editBuffer_.fill('\0');
  editBufferPointer_.clear();
  return true;
}

void EditorSpecializedPanel::draw(EditorWorkspace &workspace,
                                  std::string &notice) {
  if (!active_)
    return;
  ImGui::SetNextWindowSize({1120.0F, 760.0F}, ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                          ImGuiCond_Appearing, {0.5F, 0.5F});
  if (!ImGui::Begin("Specialized Document", nullptr,
                    ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return;
  }

  EditorJsonDocument &document = active_->document();
  ImGui::TextUnformatted(active_->title().data());
  ImGui::SameLine();
  ImGui::TextDisabled("%s", document.path().string().c_str());
  if (document.isDirty()) {
    ImGui::SameLine();
    ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "Modified");
  }
  ImGui::SameLine(790.0F);
  ImGui::BeginDisabled(!document.canUndo());
  if (ImGui::Button("Undo")) {
    std::string error;
    if (document.undo(error)) {
      active_->rebuildPreview(safeArea_, locale_.data(), hudDpiScale_);
      editBufferPointer_.clear();
      notice = "Undid document edit";
    } else
      notice = error;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!document.canRedo());
  if (ImGui::Button("Redo")) {
    std::string error;
    if (document.redo(error)) {
      active_->rebuildPreview(safeArea_, locale_.data(), hudDpiScale_);
      editBufferPointer_.clear();
      notice = "Redid document edit";
    } else
      notice = error;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!document.isDirty());
  if (ImGui::Button("Save")) {
    std::string error;
    if (document.save(error)) {
      if (active_->associatedManifest() &&
          !workspace.reimportAsset(*active_->associatedManifest(), error))
        notice = error;
      else {
        workspace.refreshAssetMetadata();
        notice = "Specialized document saved";
      }
    } else
      notice = error;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Close")) {
    if (document.isDirty())
      notice = "Save or undo specialized document changes before closing.";
    else {
      active_.reset();
      ImGui::End();
      return;
    }
  }
  ImGui::Separator();

  if (ImGui::BeginTabBar("specialized-tabs")) {
    const char *previewLabel =
        active_->kind() == EditorSpecializedKind::Hud ? "Design" : "Preview";
    if (ImGui::BeginTabItem(previewLabel)) {
      active_->rebuildPreview(safeArea_, locale_.data(), hudDpiScale_);
      if (active_->kind() == EditorSpecializedKind::Prefab)
        drawPrefabPreview(*active_, notice);
      else if (active_->kind() == EditorSpecializedKind::Hud) {
        bool changed = false;
        changed |= ImGui::InputFloat("Safe left", &safeArea_.left);
        ImGui::SameLine();
        changed |= ImGui::InputFloat("Safe top", &safeArea_.top);
        changed |= ImGui::InputFloat("Safe right", &safeArea_.right);
        ImGui::SameLine();
        changed |= ImGui::InputFloat("Safe bottom", &safeArea_.bottom);
        changed |= ImGui::InputText("Locale", locale_.data(), locale_.size(),
                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::InputTextWithHint("Sample text", "Preview-only sample data",
                                 hudSampleText_.data(), hudSampleText_.size());
        changed |=
            ImGui::InputFloat("DPI scale", &hudDpiScale_, 0.1F, 0.5F, "%.2f");
        if (changed)
          active_->rebuildPreview(safeArea_, locale_.data(), hudDpiScale_);
        active_->applyHudSampleText(hudSampleText_.data());
        if (active_->hudPreview())
          drawHudPreview(*active_->hudPreview());
      } else if (active_->kind() == EditorSpecializedKind::Material) {
        drawMaterialControls(*active_, notice);
        drawMaterialPreview(document);
      } else if (active_->kind() == EditorSpecializedKind::Animation) {
        drawAnimationControls(*active_, notice);
        drawAnimationPreview(*active_);
      } else if (active_->kind() == EditorSpecializedKind::Data)
        drawDataPreview(document);
      else if (active_->kind() == EditorSpecializedKind::Audio) {
        drawAudioControls(*active_, notice);
        ImGui::Text("Source: %s", document.json().value("source", "").c_str());
        ImGui::TextDisabled(
            "Audio playback remains owned by the Play runtime audio service.");
      }
      ImGui::EndTabItem();
    }
    const char *sourceLabel = active_->kind() == EditorSpecializedKind::Hud
                                  ? "Advanced JSON"
                                  : "Source";
    if (ImGui::BeginTabItem(sourceLabel)) {
      drawEditorJsonSource(document, selectedPointer_, editBuffer_,
                           editBufferPointer_, notice);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

} // namespace demi::editor
