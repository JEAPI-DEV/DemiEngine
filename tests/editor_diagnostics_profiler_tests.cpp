#include "editor/EditorDiagnosticsModel.h"
#include "editor/EditorGpuTiming.h"
#include "editor/EditorProfilerModel.h"

#include <cassert>

int main() {
  using namespace demi;
  using namespace demi::editor;

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

  const EditorGpuTimingSample gpu = buildEditorGpuTimingSample(
      1'000'000, {{.viewId = 1, .name = "scene", .begin = 0, .end = 5000},
                  {.viewId = 8, .name = "game", .begin = 1000, .end = 3500},
                  {.viewId = 9, .name = "post", .begin = 4000, .end = 5000}});
  assert(gpu.available && gpu.passes.size() == 2 &&
         gpu.totalMilliseconds == 3.5);
  assert(!buildEditorGpuTimingSample(0, {}).available);
  assert(editorProfilerCategory("GPU.game_view") ==
         EditorProfilerCategory::Rendering);
}
