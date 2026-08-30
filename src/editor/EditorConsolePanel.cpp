#include "editor/EditorConsolePanel.h"

#include "editor/EditorPanelStyle.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
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

ImVec4 runtimeLogColor(const runtime::RuntimeLogSeverity severity) {
  if (severity == runtime::RuntimeLogSeverity::Error)
    return diagnosticColor(Severity::Error);
  if (severity == runtime::RuntimeLogSeverity::Warning)
    return diagnosticColor(Severity::Warning);
  return diagnosticColor(Severity::Info);
}

bool containsCaseInsensitive(const std::string_view value,
                             const std::string_view query) {
  if (query.empty())
    return true;
  std::string haystack(value);
  std::string needle(query);
  const auto lower = [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  };
  std::ranges::transform(haystack, haystack.begin(), lower);
  std::ranges::transform(needle, needle.begin(), lower);
  return haystack.find(needle) != std::string::npos;
}

bool visible(const runtime::RuntimeLogEntry &entry,
             const std::string_view query, const bool info, const bool warning,
             const bool error) {
  const bool severity =
      (entry.severity == runtime::RuntimeLogSeverity::Info && info) ||
      (entry.severity == runtime::RuntimeLogSeverity::Warning && warning) ||
      (entry.severity == runtime::RuntimeLogSeverity::Error && error);
  return severity && (containsCaseInsensitive(entry.channel, query) ||
                      containsCaseInsensitive(entry.message, query) ||
                      containsCaseInsensitive(entry.source, query) ||
                      containsCaseInsensitive(entry.entityId, query) ||
                      containsCaseInsensitive(entry.component, query) ||
                      containsCaseInsensitive(entry.field, query));
}

struct LuaHistoryContext {
  std::vector<std::string> *history = nullptr;
  int *cursor = nullptr;
};

int luaHistoryCallback(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory)
    return 0;
  auto *context = static_cast<LuaHistoryContext *>(data->UserData);
  if (context == nullptr || context->history == nullptr ||
      context->cursor == nullptr || context->history->empty())
    return 0;
  if (data->EventKey == ImGuiKey_UpArrow) {
    *context->cursor = *context->cursor < 0
                           ? static_cast<int>(context->history->size()) - 1
                           : std::max(*context->cursor - 1, 0);
  } else if (data->EventKey == ImGuiKey_DownArrow) {
    if (*context->cursor < 0)
      return 0;
    ++*context->cursor;
    if (*context->cursor >= static_cast<int>(context->history->size()))
      *context->cursor = -1;
  }
  const std::string value = *context->cursor < 0
                                ? std::string{}
                                : (*context->history)[*context->cursor];
  data->DeleteChars(0, data->BufTextLen);
  data->InsertChars(0, value.c_str());
  return 0;
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
                              EditorPlaySession &playSession,
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
      const auto runtimeLogs = playSession.runtimeLogs();
      for (const runtime::RuntimeLogEntry &entry : runtimeLogs) {
        if (!visible(entry, diagnosticFilter_.data(), showInfo_, showWarnings_,
                     showErrors_))
          continue;
        ImGui::PushID(static_cast<int>(entry.sequence));
        ImGui::TextColored(runtimeLogColor(entry.severity), "[%s]",
                           entry.channel.c_str());
        ImGui::SameLine();
        ImGui::TextWrapped("%s", entry.message.c_str());
        if (!entry.source.empty()) {
          ImGui::SameLine();
          const std::string location =
              entry.line > 0 ? entry.source + ':' + std::to_string(entry.line)
                             : entry.source;
          if (ImGui::SmallButton(location.c_str())) {
            ImGui::SetClipboardText(location.c_str());
            if (std::filesystem::is_regular_file(entry.source))
              openRequest_ = entry.source;
            notice = "Runtime log source copied: " + location;
          }
        }
        if (!entry.entityId.empty()) {
          ImGui::SameLine();
          if (ImGui::SmallButton(("Select " + entry.entityId).c_str()))
            workspace.selectEntity(entry.entityId);
        }
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("Lua Console")) {
    const bool available = playSession.isEmbedded();
    if (!available)
      ImGui::TextDisabled("Start embedded Play to execute Lua commands.");
    if (ImGui::BeginChild("lua-console-output", {0.0F, -34.0F}, false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
      for (const runtime::RuntimeLogEntry &entry : playSession.runtimeLogs()) {
        if (!entry.channel.starts_with("lua"))
          continue;
        ImGui::TextColored(runtimeLogColor(entry.severity), "[%s]",
                           entry.channel.c_str());
        ImGui::SameLine();
        ImGui::TextWrapped("%s", entry.message.c_str());
      }
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(!available);
    ImGui::SetNextItemWidth(-55.0F);
    LuaHistoryContext history{.history = &luaHistory_,
                              .cursor = &luaHistoryCursor_};
    const bool submitted =
        ImGui::InputTextWithHint("##lua-command", "Lua expression or statement",
                                 luaCommand_.data(), luaCommand_.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_CallbackHistory,
                                 luaHistoryCallback, &history);
    ImGui::SameLine();
    const bool run = ImGui::Button("Run");
    if ((submitted || run) && luaCommand_[0] != '\0') {
      const std::string command = luaCommand_.data();
      const auto result = playSession.executeLuaConsole(command);
      if (luaHistory_.empty() || luaHistory_.back() != command)
        luaHistory_.push_back(command);
      constexpr std::size_t MaximumHistory = 100;
      if (luaHistory_.size() > MaximumHistory)
        luaHistory_.erase(luaHistory_.begin());
      luaHistoryCursor_ = -1;
      luaCommand_.fill('\0');
      notice = result.succeeded ? "Lua command executed" : result.error;
    }
    ImGui::EndDisabled();
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
      if (snapshot.gpuTimingAvailable)
        metric("GPU Game", scope(snapshot, "GPU.game_view"));
      else
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
  if (ImGui::BeginTabItem("Debug")) {
    debugPanel_.draw(playSession);
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
  ImGui::End();
}

} // namespace demi::editor
