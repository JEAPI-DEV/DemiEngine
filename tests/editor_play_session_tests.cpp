#include "editor/EditorPlaySession.h"

#include "demi/runtime/app/EmbeddedRuntimeSession.h"
#include "demi/runtime/input/replay/InputReplay.h"
#include "demi/runtime/scene/model/World.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <utility>

namespace {

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
  const std::filesystem::path source = DEMI_SOURCE_DIR;
  const std::filesystem::path project =
      source / "examples/minimal_3d/demi.project.json";
  const std::filesystem::path scene =
      source / "examples/minimal_3d/scenes/main.scene.json";
  const std::string authoredBefore = readFile(scene);
  const std::size_t baseline =
      demi::runtime::EmbeddedRuntimeSession::liveSessionCount();

  for (int iteration = 0; iteration < 3; ++iteration) {
    demi::editor::EditorPlaySession session;
    std::string error;
    assert(session.startEmbedded(project, error));
    assert(session.state() == demi::editor::EditorPlayState::Running);
    assert(session.runtimeWorld() != nullptr);
    const auto console = session.executeLuaConsole("1 + 2");
    assert(console.succeeded && console.values.size() == 1 &&
           console.values.front() == "3");
    const auto printed = session.executeLuaConsole("print('editor-log-probe')");
    assert(printed.succeeded);
    const auto logs = session.runtimeLogs();
    assert(std::ranges::any_of(logs, [](const auto &entry) {
      return entry.channel == "lua" && entry.message == "editor-log-probe";
    }));
    assert(demi::runtime::EmbeddedRuntimeSession::liveSessionCount() ==
           baseline + 1);
    assert(session.togglePause(error));
    assert(session.state() == demi::editor::EditorPlayState::Paused);
    const std::uint64_t beforeStep = session.fixedTickCount();
    assert(session.update({}, 1.0F, 960, 540, error));
    assert(session.fixedTickCount() == beforeStep);
    demi::runtime::InputState stepInput;
    stepInput.keysDown.insert("W");
    stepInput.mouseButtonsDown.insert("left");
    assert(session.step(std::move(stepInput), 960, 540, error));
    assert(session.fixedTickCount() == beforeStep + 1);
    const auto profile = session.profilerSnapshot();
    assert(profile.attached && profile.frameCount >= 1 &&
           !profile.rows.empty());
    const auto debug = session.debugSnapshot();
    assert(debug.entities > 0 && debug.input.keysDown.size() == 1 &&
           debug.input.keysDown.front() == "W" &&
           debug.input.mouseButtonsDown.size() == 1);
    demi::runtime::DebugOverlayConfig overlays;
    overlays.colliders = true;
    overlays.uiBounds = true;
    session.setDebugOverlays(overlays);
    const std::string focus = session.runtimeWorld()->entities.front().id;
    session.setDebugFocus(focus);
    assert(session.debugSnapshot().overlays.colliders &&
           session.debugSnapshot().overlays.uiBounds &&
           session.debugSnapshot().focusedEntityId == focus);
    session.stop();
    assert(!session.runtimeLogs().empty());
    assert(session.state() == demi::editor::EditorPlayState::Stopped);
    assert(session.runtimeWorld() == nullptr);
    assert(demi::runtime::EmbeddedRuntimeSession::liveSessionCount() ==
           baseline);
  }

  demi::editor::EditorPlaySession isometric;
  std::string isometricError;
  assert(isometric.startEmbedded(
      source / "examples/isometric_base_builder/demi.project.json",
      isometricError));
  const auto replay = demi::runtime::input::loadInputReplay(
      source / "examples/isometric_base_builder/replays/"
               "build_and_defend.replay.json",
      isometricError);
  assert(replay);
  for (std::size_t frame = 0; frame < 3000; ++frame) {
    demi::runtime::InputState input;
    replay->applyOrNeutral(frame, input);
    assert(isometric.update(std::move(input), replay->fixedTimestep, 960, 540,
                            isometricError));
  }
  const auto isometricLogs = isometric.runtimeLogs();
  const auto hasMessage = [&](const std::string_view prefix) {
    return std::ranges::any_of(isometricLogs, [&](const auto &entry) {
      return entry.channel == "lua" && entry.message.starts_with(prefix);
    });
  };
  assert(hasMessage("Tower placed: Arrow tower"));
  assert(hasMessage("Wave started: 1"));
  assert(hasMessage("Wave completed: 1"));
  isometric.stop();

  demi::editor::EditorPlaySession failed;
  std::string error;
  assert(!failed.startEmbedded(source / "missing.project.json", error));
  assert(failed.state() == demi::editor::EditorPlayState::Failed);
  assert(!failed.failure().empty());
  failed.stop();
  assert(failed.state() == demi::editor::EditorPlayState::Stopped);
  assert(readFile(scene) == authoredBefore);
  return 0;
}
