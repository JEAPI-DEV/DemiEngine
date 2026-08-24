#pragma once

#include "demi/runtime/debug/RuntimeDebugSnapshot.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace demi::runtime {

struct World;

// Owns one in-process game lifetime without owning a platform window or a
// renderer. Hosts provide focused input and render the exposed runtime world.
class EmbeddedRuntimeSession {
public:
  EmbeddedRuntimeSession();
  ~EmbeddedRuntimeSession();

  EmbeddedRuntimeSession(const EmbeddedRuntimeSession &) = delete;
  EmbeddedRuntimeSession &operator=(const EmbeddedRuntimeSession &) = delete;

  [[nodiscard]] bool start(const std::filesystem::path &projectPath,
                           std::string &error);
  void stop();
  void setPaused(bool paused);
  [[nodiscard]] bool update(InputState input, float deltaSeconds,
                            std::uint16_t viewportWidth,
                            std::uint16_t viewportHeight, std::string &error);
  [[nodiscard]] bool step(InputState input, std::uint16_t viewportWidth,
                          std::uint16_t viewportHeight, std::string &error);

  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] bool isPaused() const;
  [[nodiscard]] bool quitRequested() const;
  [[nodiscard]] const World *world() const;
  [[nodiscard]] World *world();
  [[nodiscard]] float fixedTimestep() const;
  [[nodiscard]] float interpolationAlpha() const;
  [[nodiscard]] std::uint64_t fixedTickCount() const;
  [[nodiscard]] RuntimeDebugSnapshot debugSnapshot() const;
  void setDebugOverlays(DebugOverlayConfig overlays);
  void setDebugFocus(std::string entityId);
  [[nodiscard]] static std::size_t liveSessionCount();

private:
  class State;
  [[nodiscard]] static bool advance(State &state, InputState input,
                                    float deltaSeconds,
                                    std::uint16_t viewportWidth,
                                    std::uint16_t viewportHeight,
                                    bool oneFixedTick, std::string &error);
  std::unique_ptr<State> state_;
};

} // namespace demi::runtime
