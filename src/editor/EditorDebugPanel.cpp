#include "editor/EditorDebugPanel.h"

#include "editor/EditorPlaySession.h"

#include <imgui.h>

#include <algorithm>

namespace demi::editor {
namespace {

const runtime::RuntimeProfiler::Entry *
gauge(const EditorProfilerSnapshot &profile, const std::string_view name) {
  const auto found = std::ranges::find_if(
      profile.rows, [&](const auto &row) { return row.entry.name == name; });
  return found == profile.rows.end() ? nullptr : &found->entry;
}

void value(const char *label, const std::size_t number) {
  ImGui::TextDisabled("%-22s", label);
  ImGui::SameLine();
  ImGui::Text("%zu", number);
}

void gaugeValue(const EditorProfilerSnapshot &profile, const char *label,
                const char *name) {
  ImGui::TextDisabled("%-22s", label);
  ImGui::SameLine();
  const auto *entry = gauge(profile, name);
  if (entry == nullptr || !entry->hasGauge)
    ImGui::TextUnformatted("--");
  else
    ImGui::Text("%.0f", entry->gauge);
}

const char *networkMode(const runtime::NetworkMode mode) {
  switch (mode) {
  case runtime::NetworkMode::Offline:
    return "Offline";
  case runtime::NetworkMode::Host:
    return "Host";
  case runtime::NetworkMode::Client:
    return "Client";
  }
  return "Unknown";
}

} // namespace

void EditorDebugPanel::draw(EditorPlaySession &playSession) {
  if (!playSession.isEmbedded()) {
    ImGui::TextDisabled("Start embedded Play to inspect runtime debug state.");
    return;
  }
  const auto sectionButton = [&](const char *label, const Section section) {
    if (section != Section::Input)
      ImGui::SameLine();
    if (ImGui::Selectable(label, section_ == section, 0,
                          {ImGui::CalcTextSize(label).x + 16.0F, 24.0F}))
      section_ = section;
  };
  sectionButton("Input", Section::Input);
  sectionButton("Renderer", Section::Renderer);
  sectionButton("Physics", Section::Physics);
  sectionButton("Navigation", Section::Navigation);
  sectionButton("Assets", Section::Assets);
  sectionButton("Network", Section::Network);
  ImGui::Separator();

  const runtime::RuntimeDebugSnapshot snapshot = playSession.debugSnapshot();
  const EditorProfilerSnapshot profile = playSession.profilerSnapshot();
  if (section_ == Section::Input) {
    ImGui::Text("Mouse %.1f, %.1f  delta %.1f, %.1f  wheel %.1f, %.1f",
                snapshot.input.mousePosition.x, snapshot.input.mousePosition.y,
                snapshot.input.mouseDelta.x, snapshot.input.mouseDelta.y,
                snapshot.input.mouseScroll.x, snapshot.input.mouseScroll.y);
    ImGui::Text("Keys: %s", snapshot.input.keysDown.empty()
                                ? "none"
                                : snapshot.input.keysDown.front().c_str());
    for (std::size_t index = 1; index < snapshot.input.keysDown.size();
         ++index) {
      ImGui::SameLine();
      ImGui::TextUnformatted(snapshot.input.keysDown[index].c_str());
    }
    value("Mouse buttons", snapshot.input.mouseButtonsDown.size());
    value("Gamepads", snapshot.input.gamepads);
    value("Touches", snapshot.input.touches);
  } else if (section_ == Section::Renderer) {
    gaugeValue(profile, "2D draw calls", "Renderer2D.draw_calls");
    gaugeValue(profile, "2D triangles", "Renderer2D.triangles");
    gaugeValue(profile, "2D quads", "Renderer2D.quads");
    gaugeValue(profile, "3D batches", "Renderer3D.batches");
    gaugeValue(profile, "3D triangles", "Renderer3D.triangles");
    gaugeValue(profile, "Visible meshes", "Renderer3D.visible_meshes");
    gaugeValue(profile, "Culled meshes", "Renderer3D.culled_meshes");
    ImGui::TextDisabled("GPU timestamp queries are unavailable.");
  } else if (section_ == Section::Physics) {
    value("2D rigidbodies", snapshot.physics.rigidbodies2D);
    value("2D colliders", snapshot.physics.colliders2D);
    value("2D contacts", snapshot.physics.contacts2D);
    value("3D rigidbodies", snapshot.physics.rigidbodies3D);
    value("3D colliders", snapshot.physics.colliders3D);
    value("3D contacts", snapshot.physics.contacts3D);
  } else if (section_ == Section::Navigation) {
    if (!snapshot.navigation.available)
      ImGui::TextDisabled("No runtime NavigationGrid2D is configured.");
    else
      ImGui::Text("Grid %d x %d  cell %.2f  blockers %zu  weighted %zu",
                  snapshot.navigation.width, snapshot.navigation.height,
                  snapshot.navigation.cellSize, snapshot.navigation.blockers,
                  snapshot.navigation.weightedCells);
  } else if (section_ == Section::Assets) {
    value("Resident assets", snapshot.assets.assets.size());
    value("Resident bytes", snapshot.assets.residentBytes);
    value("Pending bytes", snapshot.assets.pendingBytes);
    if (ImGui::BeginTable("resident-assets", 3,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_ScrollY,
                          {0.0F, 0.0F})) {
      ImGui::TableSetupColumn("Asset");
      ImGui::TableSetupColumn("Backend");
      ImGui::TableSetupColumn("Bytes");
      ImGui::TableHeadersRow();
      for (const demi::assets::AssetMemoryEntry &asset :
           snapshot.assets.assets) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(asset.assetId.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(asset.backend.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%zu", asset.residentBytes);
      }
      ImGui::EndTable();
    }
  } else if (section_ == Section::Network) {
    ImGui::Text("Mode %s  %s  latency %u ms",
                networkMode(snapshot.network.mode),
                snapshot.network.connected ? "connected" : "disconnected",
                snapshot.network.latencyMilliseconds);
    ImGui::Text("Backend %s  security %s",
                snapshot.network.available ? "available" : "unavailable",
                snapshot.network.secure ? "secure" : "plain");
    if (!snapshot.network.securityError.empty())
      ImGui::TextColored({0.95F, 0.34F, 0.38F, 1.0F}, "%s",
                         snapshot.network.securityError.c_str());
  }

  ImGui::SeparatorText("Runtime overlays");
  runtime::DebugOverlayConfig overlays = snapshot.overlays;
  if (snapshot.focusedEntityId.empty() && overlays.drawOrder)
    ImGui::TextColored(
        {0.95F, 0.72F, 0.30F, 1.0F},
        "Select a runtime entity in the hierarchy to inspect it.");
  else if (!snapshot.focusedEntityId.empty() && overlays.drawOrder)
    ImGui::Text("Focused entity: %s", snapshot.focusedEntityId.c_str());
  bool changed = false;
  if (overlays.entityIds) {
    overlays.entityIds = false;
    changed = true;
  }
  changed |= ImGui::Checkbox("Colliders", &overlays.colliders);
  ImGui::SameLine();
  changed |= ImGui::Checkbox("Contacts", &overlays.contacts);
  ImGui::SameLine();
  changed |= ImGui::Checkbox("Selected draw index", &overlays.drawOrder);
  ImGui::SameLine();
  changed |= ImGui::Checkbox("UI bounds", &overlays.uiBounds);
  if (changed)
    playSession.setDebugOverlays(overlays);
}

} // namespace demi::editor
