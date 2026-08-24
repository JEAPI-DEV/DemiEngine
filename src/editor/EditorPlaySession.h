#pragma once

#include "demi/runtime/debug/RuntimeDebugSnapshot.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include "editor/EditorProfilerModel.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace demi::runtime {
class EmbeddedRuntimeSession;
struct World;
} // namespace demi::runtime

namespace demi::editor {

enum class EditorPlayState { Stopped, Starting, Running, Paused, Failed };
enum class EditorPlayMode { Embedded, External };

[[nodiscard]] std::string_view editorPlayStateLabel(EditorPlayState state);

// Owns editor-facing lifecycle policy. Embedded play is the default; the
// external process remains an explicit compatibility path.
class EditorPlaySession {
public:
  EditorPlaySession();
  EditorPlaySession(const EditorPlaySession &) = delete;
  EditorPlaySession &operator=(const EditorPlaySession &) = delete;
  ~EditorPlaySession();

  [[nodiscard]] bool startEmbedded(const std::filesystem::path &project,
                                   std::string &error);
  [[nodiscard]] bool startExternal(const std::filesystem::path &project,
                                   std::string &error);
  [[nodiscard]] bool togglePause(std::string &error);
  [[nodiscard]] bool step(runtime::InputState input, std::uint16_t width,
                          std::uint16_t height, std::string &error);
  [[nodiscard]] bool update(runtime::InputState input, float deltaSeconds,
                            std::uint16_t width, std::uint16_t height,
                            std::string &error);
  void reportFailure(std::string message);
  void stop();
  void poll();

  [[nodiscard]] EditorPlayState state() const { return state_; }
  [[nodiscard]] EditorPlayMode mode() const { return mode_; }
  [[nodiscard]] bool isRunning() const {
    return state_ == EditorPlayState::Running ||
           state_ == EditorPlayState::Paused;
  }
  [[nodiscard]] bool isPaused() const {
    return state_ == EditorPlayState::Paused;
  }
  [[nodiscard]] bool isEmbedded() const {
    return isRunning() && mode_ == EditorPlayMode::Embedded;
  }
  [[nodiscard]] const runtime::World *runtimeWorld() const;
  [[nodiscard]] std::string_view failure() const { return failure_; }
  [[nodiscard]] std::uint64_t fixedTickCount() const;
  [[nodiscard]] float interpolationAlpha() const;
  [[nodiscard]] EditorProfilerSnapshot profilerSnapshot() const;
  [[nodiscard]] runtime::RuntimeDebugSnapshot debugSnapshot() const;
  void setDebugOverlays(runtime::DebugOverlayConfig overlays);
  void setDebugFocus(std::string entityId);

private:
  std::unique_ptr<runtime::EmbeddedRuntimeSession> embedded_;
  int processId_ = 0;
  EditorPlayState state_ = EditorPlayState::Stopped;
  EditorPlayMode mode_ = EditorPlayMode::Embedded;
  std::string failure_;
};

} // namespace demi::editor
