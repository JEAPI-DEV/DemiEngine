#include "editor/EditorPlaySession.h"

#include "demi/runtime/app/EmbeddedRuntimeSession.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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
    assert(demi::runtime::EmbeddedRuntimeSession::liveSessionCount() ==
           baseline + 1);
    assert(session.togglePause(error));
    assert(session.state() == demi::editor::EditorPlayState::Paused);
    const std::uint64_t beforeStep = session.fixedTickCount();
    assert(session.update({}, 1.0F, 960, 540, error));
    assert(session.fixedTickCount() == beforeStep);
    assert(session.step({}, 960, 540, error));
    assert(session.fixedTickCount() == beforeStep + 1);
    session.stop();
    assert(session.state() == demi::editor::EditorPlayState::Stopped);
    assert(session.runtimeWorld() == nullptr);
    assert(demi::runtime::EmbeddedRuntimeSession::liveSessionCount() ==
           baseline);
  }

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
