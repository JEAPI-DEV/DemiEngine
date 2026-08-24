#include "editor/EditorConsolePanel.h"

#include "editor/EditorPanelStyle.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace demi::editor {
namespace {

ImVec4 diagnosticColor(const Severity severity) {
  if (severity == Severity::Error)
    return {0.95F, 0.34F, 0.38F, 1.0F};
  if (severity == Severity::Warning)
    return {0.95F, 0.72F, 0.30F, 1.0F};
  return {0.45F, 0.72F, 0.95F, 1.0F};
}

const EditorProfilerRow *scope(const EditorProfilerSnapshot &snapshot,
                               const std::string_view name) {
  const auto found = std::ranges::find_if(
      snapshot.rows, [&](const auto &row) { return row.entry.name == name; });
  return found == snapshot.rows.end() ? nullptr : &*found;
}

void metric(const char *label, const EditorProfilerRow *row,
            const char *fallback = "--") {
  ImGui::TextDisabled("%s", label);
  ImGui::SameLine();
  if (row == nullptr)
    ImGui::TextUnformatted(fallback);
  else
    ImGui::Text("%.2f ms avg  %.2f p95", row->averageMilliseconds,
                row->entry.p95Milliseconds);
}

} // namespace

std::optional<std::filesystem::path> EditorConsolePanel::takeOpenRequest() {
  return std::exchange(openRequest_, std::nullopt);
}

void EditorConsolePanel::draw(EditorWorkspace &workspace,
                              const EditorPlaySession &playSession,
                              const ImVec2 position, const ImVec2 size,
                              const EditorProjectOperationSnapshot &operation,
                              std::string &notice) {
  beginEditorPanel("Console", position, size);
  if (!ImGui::BeginTabBar("diagnostic-tabs")) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTabItem("Console")) {
    ImGui::SetNextItemWidth(std::max(120.0F, size.x - 235.0F));
    ImGui::InputTextWithHint("##diagnostic-filter", "Search diagnostics",
                             diagnosticFilter_.data(),
                             diagnosticFilter_.size());
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo_);
    ImGui::SameLine();
    ImGui::Checkbox("Warn", &showWarnings_);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &showErrors_);

    Diagnostics buildDiagnostics;
    if (operation.result)
      buildDiagnostics = operation.result->diagnostics;
    const auto records = filterEditorDiagnostics(
        collectEditorDiagnostics(workspace.diagnostics(), buildDiagnostics),
        diagnosticFilter_.data(), showInfo_, showWarnings_, showErrors_);
    if (records.empty())
      ImGui::TextColored({0.35F, 0.85F, 0.55F, 1.0F},
                         "No matching diagnostics.");
    if (ImGui::BeginChild("diagnostic-list", {0.0F, 0.0F}, false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
      for (std::size_t index = 0; index < records.size(); ++index) {
        const Diagnostic &diagnostic = records[index].diagnostic;
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextColored(diagnosticColor(diagnostic.severity), "%s",
                           diagnostic.code.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(diagnostic.message.c_str());
        if (!diagnostic.path.empty()) {
          ImGui::SameLine();
          std::string location = diagnostic.path;
          if (diagnostic.line > 0)
            location += ':' + std::to_string(diagnostic.line);
          if (ImGui::SmallButton(location.c_str())) {
            ImGui::SetClipboardText(location.c_str());
            if (std::filesystem::is_regular_file(diagnostic.path))
              openRequest_ = diagnostic.path;
            notice = "Diagnostic location copied: " + location;
          }
        }
        if (!records[index].entityId.empty()) {
          ImGui::SameLine();
          if (ImGui::SmallButton(
                  ("Select " + records[index].entityId).c_str())) {
            workspace.selectEntity(records[index].entityId);
            notice = records[index].component.empty()
                         ? "Selected diagnostic entity"
                         : "Selected " + records[index].entityId + " · " +
                               records[index].component +
                               (records[index].field.empty()
                                    ? std::string{}
                                    : "." + records[index].field);
          }
        }
        if (!diagnostic.suggestion.empty() && ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", diagnostic.suggestion.c_str());
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("Profiler")) {
    const EditorProfilerSnapshot snapshot = playSession.profilerSnapshot();
    if (!snapshot.attached) {
      ImGui::TextDisabled(
          "Start Play in the Game view to capture runtime data.");
      ImGui::EndTabItem();
    } else {
      ImGui::Text("%zu frames%s", snapshot.frameCount,
                  snapshot.paused ? " · paused" : "");
      ImGui::SameLine();
      ImGui::TextDisabled("GPU timing: unavailable on this backend");
      metric("Update", scope(snapshot, "Frame.update"));
      ImGui::SameLine(245.0F);
      metric("Render submit", scope(snapshot, "Render.submit"));

      ImGui::SetNextItemWidth(std::max(120.0F, size.x - 190.0F));
      ImGui::InputTextWithHint("##profiler-filter", "Search scopes",
                               profilerFilter_.data(), profilerFilter_.size());
      ImGui::SameLine();
      const char *categoryLabel =
          allProfilerCategories_
              ? "All categories"
              : editorProfilerCategoryLabel(profilerCategory_).data();
      if (ImGui::BeginCombo("##profiler-category", categoryLabel)) {
        if (ImGui::Selectable("All categories", allProfilerCategories_))
          allProfilerCategories_ = true;
        for (const EditorProfilerCategory category :
             {EditorProfilerCategory::Frame, EditorProfilerCategory::Rendering,
              EditorProfilerCategory::Scripting,
              EditorProfilerCategory::Physics,
              EditorProfilerCategory::Animation,
              EditorProfilerCategory::Resources,
              EditorProfilerCategory::Network, EditorProfilerCategory::Input,
              EditorProfilerCategory::Other}) {
          if (ImGui::Selectable(editorProfilerCategoryLabel(category).data(),
                                !allProfilerCategories_ &&
                                    profilerCategory_ == category)) {
            allProfilerCategories_ = false;
            profilerCategory_ = category;
          }
        }
        ImGui::EndCombo();
      }

      const auto rows =
          filterEditorProfilerRows(snapshot, profilerFilter_.data(),
                                   profilerCategory_, allProfilerCategories_);
      if (ImGui::BeginTable(
              "profiler-table", 6,
              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
              {0.0F, 0.0F})) {
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch,
                                2.2F);
        ImGui::TableSetupColumn("Latest");
        ImGui::TableSetupColumn("Average");
        ImGui::TableSetupColumn("P95");
        ImGui::TableSetupColumn("Max");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        for (const EditorProfilerRow &row : rows) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(row.entry.name.c_str());
          ImGui::TableNextColumn();
          ImGui::Text("%.3f", row.entry.latestMilliseconds);
          ImGui::TableNextColumn();
          ImGui::Text("%.3f", row.averageMilliseconds);
          ImGui::TableNextColumn();
          ImGui::Text("%.3f", row.entry.p95Milliseconds);
          ImGui::TableNextColumn();
          ImGui::Text("%.3f", row.entry.maxMilliseconds);
          ImGui::TableNextColumn();
          if (row.entry.hasGauge)
            ImGui::Text("%.0f", row.entry.gauge);
          else if (row.entry.bytes > 0)
            ImGui::Text("%zu B", row.entry.bytes);
          else
            ImGui::TextDisabled("%d calls", row.entry.calls);
        }
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }
  }
  ImGui::EndTabBar();
  ImGui::End();
}

} // namespace demi::editor
