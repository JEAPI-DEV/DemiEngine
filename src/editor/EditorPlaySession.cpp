#include "editor/EditorPlaySession.h"

#include "demi/runtime/app/EmbeddedRuntimeSession.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <utility>

namespace demi::editor {

bool EditorPlaySession::mouseCaptured() const {
  return embedded_ && embedded_->mouseCaptured();
}
bool EditorPlaySession::mouseVisible() const {
  return !embedded_ || embedded_->mouseVisible();
}
void EditorPlaySession::releaseMouseCapture() {
  if (embedded_)
    embedded_->releaseMouseCapture();
}

std::string_view editorPlayStateLabel(const EditorPlayState state) {
  switch (state) {
  case EditorPlayState::Stopped:
    return "Stopped";
  case EditorPlayState::Starting:
    return "Starting";
  case EditorPlayState::Running:
    return "Running";
  case EditorPlayState::Paused:
    return "Paused";
  case EditorPlayState::Failed:
    return "Failed";
  }
  return "Unknown";
}

EditorPlaySession::EditorPlaySession() = default;

EditorPlaySession::~EditorPlaySession() { stop(); }

bool EditorPlaySession::startEmbedded(const std::filesystem::path &project,
                                      std::string &error,
                                      const std::string &sceneId) {
  stop();
  state_ = EditorPlayState::Starting;
  failure_.clear();
  retainedLogs_.clear();
  gpuTimingAvailable_ = false;
  auto session = std::make_unique<runtime::EmbeddedRuntimeSession>();
  runtime::RuntimeProfiler::setEnabled(true);
  runtime::RuntimeProfiler::resetSession();
  if (!session->start(project, error, sceneId)) {
    runtime::RuntimeProfiler::setEnabled(false);
    reportFailure(error);
    return false;
  }
  embedded_ = std::move(session);
  state_ = EditorPlayState::Running;
  return true;
}

bool EditorPlaySession::togglePause(std::string &error) {
  if (!isRunning()) {
    error = "No play session is running.";
    return false;
  }
  const bool pause = state_ == EditorPlayState::Running;
  embedded_->setPaused(pause);
  state_ = pause ? EditorPlayState::Paused : EditorPlayState::Running;
  return true;
}

bool EditorPlaySession::step(runtime::InputState input,
                             const std::uint16_t width,
                             const std::uint16_t height, std::string &error) {
  if (!isPaused()) {
    error = "Step requires a paused embedded play session.";
    return false;
  }
  if (!embedded_->step(std::move(input), width, height, error)) {
    reportFailure(error);
    return false;
  }
  return true;
}

bool EditorPlaySession::update(runtime::InputState input,
                               const float deltaSeconds,
                               const std::uint16_t width,
                               const std::uint16_t height, std::string &error) {
  if (state_ != EditorPlayState::Running)
    return true;
  if (!embedded_->update(std::move(input), deltaSeconds, width, height,
                         error)) {
    reportFailure(error);
    return false;
  }
  if (embedded_->quitRequested())
    stop();
  return true;
}

void EditorPlaySession::stop() {
  if (embedded_ != nullptr)
    retainedLogs_ = embedded_->runtimeLogs();
  if (embedded_ != nullptr) {
    embedded_->stop();
    embedded_.reset();
    runtime::RuntimeProfiler::setEnabled(false);
  }
  state_ = EditorPlayState::Stopped;
}

const runtime::World *EditorPlaySession::runtimeWorld() const {
  return embedded_ == nullptr ? nullptr : embedded_->world();
}

std::uint64_t EditorPlaySession::fixedTickCount() const {
  return embedded_ == nullptr ? 0 : embedded_->fixedTickCount();
}

float EditorPlaySession::interpolationAlpha() const {
  return embedded_ == nullptr ? 1.0F : embedded_->interpolationAlpha();
}

EditorProfilerSnapshot EditorPlaySession::profilerSnapshot() const {
  return buildEditorProfilerSnapshot(
      isEmbedded(), isPaused(), runtime::RuntimeProfiler::sessionEntries(),
      runtime::RuntimeProfiler::frameCount(), gpuTimingAvailable_);
}

void EditorPlaySession::setGpuTiming(EditorGpuTimingSample sample) {
  if (!isEmbedded()) {
    gpuTimingAvailable_ = false;
    return;
  }
  if (!sample.available)
    return;
  gpuTimingAvailable_ = true;
  runtime::RuntimeProfiler::record("GPU.game_view", sample.totalMilliseconds);
  for (const EditorGpuPassTiming &pass : sample.passes)
    runtime::RuntimeProfiler::record("GPU." + pass.name, pass.milliseconds);
}

runtime::RuntimeDebugSnapshot EditorPlaySession::debugSnapshot() const {
  return embedded_ == nullptr ? runtime::RuntimeDebugSnapshot{}
                              : embedded_->debugSnapshot();
}

std::vector<runtime::RuntimeLogEntry> EditorPlaySession::runtimeLogs() const {
  return embedded_ != nullptr ? embedded_->runtimeLogs() : retainedLogs_;
}

runtime::LuaScriptHost::ConsoleResult
EditorPlaySession::executeLuaConsole(const std::string_view command) {
  if (!isEmbedded() || embedded_ == nullptr)
    return {.succeeded = false,
            .values = {},
            .error = "Start embedded Play before using the Lua Console."};
  return embedded_->executeLuaConsole(command);
}

void EditorPlaySession::setDebugOverlays(
    const runtime::DebugOverlayConfig overlays) {
  if (embedded_ != nullptr)
    embedded_->setDebugOverlays(overlays);
}

void EditorPlaySession::setDebugFocus(std::string entityId) {
  if (embedded_ != nullptr)
    embedded_->setDebugFocus(std::move(entityId));
}

void EditorPlaySession::reportFailure(std::string message) {
  if (embedded_ != nullptr) {
    retainedLogs_ = embedded_->runtimeLogs();
    embedded_->stop();
    embedded_.reset();
  }
  failure_ = std::move(message);
  state_ = EditorPlayState::Failed;
}

} // namespace demi::editor
