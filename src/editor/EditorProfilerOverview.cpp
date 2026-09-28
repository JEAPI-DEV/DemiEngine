#include "editor/EditorProfilerOverview.h"

#include "editor/EditorProfilerModel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace demi::editor {
namespace {

void explanation(const char *description) {
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", description);
}

void item(const char *label, const std::string &value,
          const char *description) {
  ImGui::TextDisabled("%s", label);
  explanation(description);
  ImGui::SameLine();
  if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(value.c_str()).x)
    ImGui::NewLine();
  ImGui::TextWrapped("%s", value.c_str());
  explanation(description);
}

std::string memory(const EditorProfilerSnapshot &snapshot, const char *name) {
  const auto bytes = editorProfilerGauge(snapshot, name);
  return bytes ? editorProfilerMiB(*bytes) : "N/A";
}

void cardTitle(const char *title, const char *description) {
  ImGui::TextColored({0.67F, 0.76F, 0.94F, 1.0F}, "%s", title);
  explanation(description);
  ImGui::Separator();
}

void cpuCard(const EditorProfilerSnapshot &snapshot) {
  cardTitle("CPU", "Whole editor process, including the embedded Game. "
                   "100% means one logical CPU core; usage can exceed 100%.");
  const auto cpu = editorProfilerGauge(snapshot, "Process.cpu_percent");
  const bool hasProcessSample =
      editorProfilerGauge(snapshot, "Process.resident_bytes").has_value();
  if (cpu) {
    ImGui::Text("%.1f%%", *cpu);
  } else {
    ImGui::TextDisabled("%s", hasProcessSample ? "Warming up" : "N/A");
  }
  explanation("CPU usage is for the whole Editor + Game process. "
              "The first CPU interval may still be warming up.");
  const auto age = editorProfilerGauge(snapshot, "Process.sample_age_ms");
  if (age) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.0f ms", *age);
    item("Sample age", buffer,
         "Elapsed time since the process resource sample was captured.");
  } else {
    item("Sample age", "N/A", "No process sample age is available.");
  }
}

void ramCard(const EditorProfilerSnapshot &snapshot) {
  cardTitle("PROCESS RAM", "Resident memory belongs to the entire Editor + "
                           "Game process, including native allocations.");
  item("Resident", memory(snapshot, "Process.resident_bytes"),
       "Current resident memory of the Editor + Game process.");
  item("Peak", memory(snapshot, "Process.peak_resident_bytes"),
       "Peak resident memory over the process lifetime, not just this Play "
       "session.");
}

void luaHeapCard(const EditorProfilerSnapshot &snapshot) {
  cardTitle("LUA HEAP", "Gameplay Lua VM only. Worker VMs are separate and "
                        "are included in process RAM, not this gauge.");
  item("Current", memory(snapshot, "Lua.heap_bytes"),
       "Memory currently reported by the Lua VM allocator.");
  item("Sampled peak", memory(snapshot, "Lua.heap_peak_sampled_bytes"),
       "Highest sampled Lua heap value during this VM lifetime. "
       "Short peaks between samples may be missed.");
}

} // namespace

void drawEditorProfilerOverview(const EditorProfilerSnapshot &snapshot) {
  const bool expanded =
      ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen);
  explanation("Process gauges include Editor + Game. Lua values belong to "
              "the attached VM. Paused Play retains the last samples.");
  if (!expanded)
    return;

  const float minimumCardWidth = ImGui::GetFontSize() * 17.0F;
  const float width = ImGui::GetContentRegionAvail().x;
  const int columns =
      std::clamp(static_cast<int>(width / minimumCardWidth), 1, 3);
  if (ImGui::BeginTable("profiler-resources", columns,
                        ImGuiTableFlags_SizingStretchSame |
                            ImGuiTableFlags_BordersInnerV)) {
    const auto drawCard = [&](auto draw) {
      ImGui::TableNextColumn();
      draw(snapshot);
    };
    drawCard(cpuCard);
    drawCard(ramCard);
    drawCard(luaHeapCard);
    ImGui::EndTable();
  }
  ImGui::Spacing();
}

} // namespace demi::editor
