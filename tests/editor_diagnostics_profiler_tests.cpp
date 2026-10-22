#include "editor/EditorDiagnosticsModel.h"
#include "editor/EditorGpuTiming.h"
#include "editor/EditorProfilerModel.h"
#include "editor/EditorProfilerScale.h"
#include "editor/EditorProfilerGameView.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>

int main() {
  using namespace demi;
  using namespace demi::editor;

  // Auto axes retain real spikes, reject transient shrink and stay independent.
  EditorProfilerScale cpuScale(16.67);
  EditorProfilerScale gpuScale(16.67);
  cpuScale.observe(35.0, 0.0);
  assert(cpuScale.span() == 40.0);
  assert(gpuScale.span() == 16.67);
  cpuScale.observe(10.0, 0.1);
  cpuScale.observe(10.0, 2.0);
  assert(cpuScale.span() == 40.0);
  cpuScale.observe(10.0, 2.1);
  assert(cpuScale.span() == 12.5);
  cpuScale.observe(12.0, 2.2);
  assert(cpuScale.span() == 12.5);
  cpuScale.setManualSpan(8.0);
  cpuScale.observe(500.0, 10.0);
  assert(cpuScale.span() == 8.0 && !cpuScale.automatic());
  cpuScale.setAutomatic(true);
  cpuScale.observe(500.0, 10.1);
  assert(cpuScale.span() == 600.0);
  cpuScale.observe(10.0, 10.2);
  cpuScale.observe(400.0, 11.0);
  cpuScale.observe(10.0, 12.0);
  cpuScale.observe(10.0, 13.0);
  assert(cpuScale.span() == 600.0);
  cpuScale.observe(10.0, 14.0);
  assert(cpuScale.span() == 12.5);

  cpuScale.observe(500.0, 15.0);
  cpuScale.resetSession(16.67);
  assert(cpuScale.span() == 16.67 && cpuScale.automatic());
  cpuScale.setManualSpan(80.0);
  cpuScale.resetSession(16.67);
  assert(cpuScale.span() == 80.0 && !cpuScale.automatic());
  gpuScale.observe(500.0, 15.0);
  gpuScale.observe(10.0, 15.5);
  gpuScale.resetSession(16.67);
  gpuScale.observe(1.0, 16.0);
  assert(gpuScale.span() == 16.67);
  gpuScale.observe(1.0, 17.9);
  assert(gpuScale.span() == 16.67);
  gpuScale.observe(1.0, 18.0);
  assert(gpuScale.span() == 1.25);

  runtime::RuntimeProfiler::FrameCapture gameFrame;
  gameFrame.durationMilliseconds = 150.0;
  gameFrame.entries = {
      {.name = "Frame.update", .totalMilliseconds = 4.0},
      {.name = "Render.submit", .totalMilliseconds = 5.0},
      {.name = "Lua.update", .totalMilliseconds = 1.0},
      {.name = "Editor.present", .totalMilliseconds = 100.0}};
  // Completion order places child events before their parents.
  gameFrame.scopes = {
      {.name = "Renderer3D.editor_view", .startMilliseconds = 1.0,
       .durationMilliseconds = 2.0, .depth = 1},
      {.name = "Editor.viewport3d", .startMilliseconds = 0.0,
       .durationMilliseconds = 10.0, .depth = 0},
      {.name = "Lua.update", .startMilliseconds = 21.0,
       .durationMilliseconds = 1.0, .depth = 2},
      {.name = "Frame.update", .startMilliseconds = 20.0,
       .durationMilliseconds = 4.0, .depth = 1},
      {.name = "Render.submit", .startMilliseconds = 30.0,
       .durationMilliseconds = 5.0, .depth = 1},
      {.name = "Backend.frame_advance_wait", .startMilliseconds = 40.0,
       .durationMilliseconds = 100.0, .depth = 1},
      {.name = "Editor.present", .startMilliseconds = 39.0,
       .durationMilliseconds = 110.0, .depth = 0}};
  assert(editorGameCpuMilliseconds(gameFrame) == 9.0);
  const auto gameScopes = editorGameCpuScopes(gameFrame);
  assert(gameScopes.size() == 3);
  assert(gameScopes[0].name == "Lua.update");
  assert(gameScopes[0].startMilliseconds == 1.0 && gameScopes[0].depth == 1);
  assert(gameScopes[1].startMilliseconds == 0.0 && gameScopes[1].depth == 0);
  assert(gameScopes[2].startMilliseconds == 10.0 && gameScopes[2].depth == 0);
  assert(gameFrame.scopes[2].startMilliseconds == 21.0);
  assert(gameFrame.durationMilliseconds == 150.0);
  assert(editorGameCpuMilliseconds({}) == 0.0);
  assert(editorGameCpuScopes({}).empty());

  Diagnostics project{
      {.severity = Severity::Error,
       .code = "SCENE_INVALID_COMPONENT_FIELD",
       .message = "Entity player, component Movement.speed is invalid",
       .path = "scenes/main.scene.json"}};
  Diagnostics build{{.severity = Severity::Warning,
                     .code = "PACKAGE_SIZE",
                     .message = "Large package",
                     .path = "demi.project.json"}};
  const auto records = collectEditorDiagnostics(project, build);
  assert(records.size() == 2);
  assert(filterEditorDiagnostics(records, "player", true, true, true).size() ==
         1);
  assert(
      filterEditorDiagnostics(records, "package", false, true, false).size() ==
      1);
  assert(
      filterEditorDiagnostics(records, {}, false, false, true).front().field ==
      "speed");

  std::vector<runtime::RuntimeProfiler::Entry> entries{
      {.name = "Frame.update",
       .totalMilliseconds = 24.0,
       .latestMilliseconds = 3.0,
       .maxMilliseconds = 5.0,
       .p95Milliseconds = 4.0,
       .calls = 8},
      {.name = "Lua.on_update",
       .totalMilliseconds = 8.0,
       .latestMilliseconds = 1.0,
       .maxMilliseconds = 2.0,
       .p95Milliseconds = 1.5,
       .calls = 8},
      {.name = "Assets.resident_bytes", .gauge = 4096.0, .hasGauge = true}};
  const EditorProfilerSnapshot snapshot =
      buildEditorProfilerSnapshot(true, false, std::move(entries), 8);
  assert(snapshot.attached && snapshot.frameCount == 8);
  assert(editorProfilerCategory("Physics2D.step") ==
         EditorProfilerCategory::Physics);
  assert(filterEditorProfilerRows(snapshot, "lua",
                                  EditorProfilerCategory::Scripting, false)
             .size() == 1);
  assert(filterEditorProfilerRows(snapshot, {}, EditorProfilerCategory::Frame,
                                  true)
             .size() == 3);
  assert(!editorProfilerGauge(snapshot, "Frame.update"));
  assert(editorProfilerGauge(snapshot, "Assets.resident_bytes") == 4096.0);
  assert(editorProfilerMiB(1048576.0) == "1.0 MiB");
  assert(editorProfilerMiB(1572864.0) == "1.5 MiB");
  assert(editorProfilerMiB(-1.0) == "N/A");

  const auto resources = buildEditorProfilerSnapshot(
      true, true,
      {{.name = "Process.cpu_percent", .gauge = 175.25, .hasGauge = true},
       {.name = "Process.resident_bytes", .gauge = 1572864.0, .hasGauge = true},
       {.name = "Lua.gc_running", .gauge = 0.0, .hasGauge = true},
       {.name = "Lua.gc_collect_requests", .gauge = 0.0, .hasGauge = true},
       {.name = "Lua.heap_bytes",
        .gauge = std::numeric_limits<double>::quiet_NaN(),
        .hasGauge = true},
       {.name = "Lua.gc_explicit",
        .totalMilliseconds = 2.0,
        .latestMilliseconds = 0.5,
        .calls = 1}},
      12);
  assert(resources.paused);
  assert(editorProfilerCategory("Process.resident_bytes") ==
         EditorProfilerCategory::Resources);
  assert(editorProfilerGauge(resources, "Process.cpu_percent") == 175.25);
  assert(editorProfilerGauge(resources, "Lua.gc_running") == 0.0);
  assert(editorProfilerGauge(resources, "Lua.gc_collect_requests") == 0.0);
  assert(!editorProfilerGauge(resources, "Lua.heap_bytes"));
  assert(!editorProfilerGauge(resources, "Lua.gc_explicit"));
  const auto *explicitGc =
      editorProfilerTimedScope(resources, "Lua.gc_explicit");
  assert(explicitGc && explicitGc->entry.totalMilliseconds == 2.0);
  assert(explicitGc->entry.latestMilliseconds == 0.5);
  assert(!editorProfilerTimedScope(resources, "Lua.gc_running"));
  assert(!editorProfilerGauge(resources, "Lua.gc_step_cycles"));
  assert(filterEditorProfilerRows(resources, "gc_explicit",
                                  EditorProfilerCategory::Scripting, false)
             .size() == 1);

  runtime::render::GraphicsFrameTimings timings;
  timings.gpuFrame = 42;
  timings.gpuMilliseconds = 5.0;
  const auto appendView = [&](std::uint16_t id, const char *name,
                              std::uint32_t frame, std::int64_t begin,
                              std::int64_t end) {
    const auto view = runtime::render::makeGraphicsGpuViewTiming(
        id, name, frame, timings.gpuFrame, begin, end, 1'000'000, 1000);
    if (view) {
      timings.gpuViews.push_back(*view);
    }
  };
  appendView(1, "scene", 42, 1000, 6000);
  appendView(EditorGameFirstView, "game", 42, 2000, 4500);
  appendView(EditorGameLastView, "post", 42, 5000, 6000);
  appendView(EditorGameFirstView + 1, "stale", 41, 1000, 6000);
  appendView(EditorGameFirstView + 2, "future", 43, 1000, 6000);
  const EditorGpuTimingSample gpu = buildEditorGpuTimingSample(timings);
  assert(gpu.available && gpu.passes.size() == 2 &&
         gpu.totalMilliseconds == 3.5 && gpu.gpuFrame == 42);
  assert(gpu.passes[0].startMilliseconds == 0.0);
  assert(gpu.passes[1].startMilliseconds == 3.0);
  assert(!buildEditorGpuTimingSample({}, EditorGameFirstView,
                                     EditorGameLastView).available);
  assert(!buildEditorGpuTimingSample(timings, EditorGameLastView,
                                     EditorGameFirstView).available);
  // Stale view storage cannot become a fresh editor sample when the shared
  // graphics owner reports no new enclosing GPU frame interval.
  timings.gpuMilliseconds.reset();
  assert(!buildEditorGpuTimingSample(timings).available);
  assert(editorProfilerCategory("GPU.game_view") ==
         EditorProfilerCategory::Rendering);
}
