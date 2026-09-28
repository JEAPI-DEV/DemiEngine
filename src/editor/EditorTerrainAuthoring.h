#pragma once

#include "editor/EditorTerrainRuntime.h"
#include "editor/EditorViewportTool.h"
#include <atomic>
#include <future>
#include <map>
#include <thread>

namespace demi::editor {

enum class EditorTerrainBrush {
  Select,
  Raise,
  Lower,
  Flatten,
  Smooth,
  Biome,
  Protect
};
enum class EditorTerrainResize { Keep, Clear };

struct EditorTerrainBrushSettings {
  EditorTerrainBrush mode = EditorTerrainBrush::Select;
  float radius = 8.0F;
  float strength = 1.0F;
  float falloff = 0.5F;
  std::string biome;
};

struct EditorTerrainCommit {
  std::string document;
  std::string entityId;
  nlohmann::json before;
  nlohmann::json recipe;
  EditorTerrainSurfacePtr surface;
};

// Owns transient drafts, pointer strokes, and worker lifecycle. Authored writes
// and preview installation are exclusively the workspace's responsibility.
class EditorTerrainAuthoring {
public:
  using Generator = std::function<EditorTerrainSurfacePtr(
      const nlohmann::json &, std::stop_token,
      const std::function<void(float)> &, std::string &)>;
  explicit EditorTerrainAuthoring(Generator generator = generateEditorTerrain);
  ~EditorTerrainAuthoring();
  EditorTerrainAuthoring(const EditorTerrainAuthoring &) = delete;
  EditorTerrainAuthoring &operator=(const EditorTerrainAuthoring &) = delete;

  void bind(std::string document, std::string entityId,
            const nlohmann::json &recipe, EditorTerrainSurfacePtr surface);
  void unbind();
  nlohmann::json &draft() { return draft_; }
  const nlohmann::json &draft() const { return draft_; }
  bool hasDraftChanges() const { return draft_ != authored_; }
  void discardDraft();
  std::string takeNotice();
  const std::string &entityId() const { return entityId_; }
  bool busy() const { return busy_; }
  bool stroking() const { return stroke_.has_value(); }
  bool brushActive() const {
    return !entityId_.empty() && brush.mode != EditorTerrainBrush::Select;
  }
  bool needsResizeDecision() const;
  bool generate(std::optional<EditorTerrainResize> decision,
                std::string &error);
  void cancel();
  std::optional<EditorTerrainCommit> poll(std::string &error);
  float progress() const { return progress_.load(); }
  bool update(const EditorViewportToolInput &input,
              std::optional<runtime::Vec3> localHit, std::string &error);
  std::vector<runtime::Vec3> brushRing() const;
  EditorTerrainSurfacePtr surface() const { return surface_; }
  EditorTerrainBrushSettings brush;

private:
  struct Result {
    EditorTerrainSurfacePtr surface;
    std::string error;
  };
  struct SavedDraft {
    nlohmann::json baseline;
    nlohmann::json recipe;
  };
  std::map<std::pair<std::string, std::string>, SavedDraft> drafts_;
  std::string notice_;
  Generator generator_;
  std::jthread worker_;
  std::future<Result> future_;
  std::atomic<float> progress_{0.0F};
  bool busy_ = false;
  bool cancelled_ = false;
  std::string document_, entityId_;
  nlohmann::json authored_, draft_, pending_, pendingBefore_;
  EditorTerrainSurfacePtr surface_;
  std::optional<nlohmann::json> stroke_;
  std::optional<runtime::Vec3> hit_, lastStamp_;
  float flattenHeight_ = 0.0F;
  void stamp(runtime::Vec3 hit);
  void validateBrushBiome();
};

} // namespace demi::editor
