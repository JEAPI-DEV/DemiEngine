#include "editor/EditorProfilerModel.h"
#include "editor/EditorProfilerOverview.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

int main() {
  ImGui::SetAllocatorFunctions(
      [](std::size_t bytes, void *) { return std::malloc(bytes); },
      [](void *memory, void *) { std::free(memory); });
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1600, 1600};
  io.DeltaTime = 1.0F / 60;
  io.Fonts->AddFontDefault();
  unsigned char *pixels = nullptr;
  int atlasWidth = 0, atlasHeight = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);

  using demi::runtime::RuntimeProfiler;
  std::vector<RuntimeProfiler::Entry> values{
      {.name = "Process.cpu_percent", .gauge = 234.5, .hasGauge = true},
      {.name = "Process.resident_bytes", .gauge = 536870912, .hasGauge = true},
      {.name = "Process.peak_resident_bytes",
       .gauge = 1073741824,
       .hasGauge = true},
      {.name = "Process.sample_age_ms", .gauge = 240, .hasGauge = true},
      {.name = "Lua.heap_bytes", .gauge = 2097152, .hasGauge = true},
      {.name = "Lua.heap_peak_sampled_bytes",
       .gauge = 4194304,
       .hasGauge = true},
      {.name = "Lua.gc_running", .gauge = 1, .hasGauge = true},
      {.name = "Lua.gc_collect_requests", .gauge = 100, .hasGauge = true},
      {.name = "Lua.gc_step_requests", .gauge = 200, .hasGauge = true},
      {.name = "Lua.gc_step_cycles", .gauge = 10, .hasGauge = true},
      {.name = "Lua.gc_explicit",
       .totalMilliseconds = 5,
       .latestMilliseconds = 0.5,
       .calls = 10}};
  for (const float scale : {1.0F, 1.5F}) {
    io.FontGlobalScale = scale;
    for (const float width : {320.0F, 720.0F, 1100.0F}) {
      for (const bool populated : {false, true}) {
        const auto snapshot = demi::editor::buildEditorProfilerSnapshot(
            true, !populated,
            populated ? values : std::vector<RuntimeProfiler::Entry>{}, 12);
        for (int frame = 0; frame < 2; ++frame) {
          ImGui::NewFrame();
          ImGui::SetNextWindowPos({0, 0});
          ImGui::SetNextWindowSize({width, 1500});
          ImGui::Begin("Overview test");
          ImGui::LogToBuffer();
          demi::editor::drawEditorProfilerOverview(snapshot);
          const std::string labels =
              ImGui::GetCurrentContext()->LogBuffer.c_str();
          ImGui::LogFinish();
          assert(labels.find("CPU") != std::string::npos);
          assert(labels.find("PROCESS RAM") != std::string::npos);
          assert(labels.find("LUA HEAP") != std::string::npos);
          assert(labels.find("LUA GC") == std::string::npos);
          assert(labels.find("Collect requests") == std::string::npos);
          const auto *window = ImGui::GetCurrentWindow();
          assert(window->DC.CursorMaxPos.x <= window->WorkRect.Max.x + 1.0F);
          ImGui::End();
          ImGui::Render();
          assert(ImGui::GetDrawData()->TotalVtxCount > 0);
        }
      }
    }
  }
  ImGui::DestroyContext();
}
