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
  Protect,
  Exclusion
};
enum class EditorTerrainResize { Keep, Clear };

struct EditorTerrainBrushSettings {
  EditorTerrainBrush mode = EditorTerrainBrush::Select;
  float radius = 8.0F;
  float strength = 1.0F;
  float falloff = 0.5F;
  std::string biome;
  std::string layer;
  float exclusionValue = 1.0F;
};

struct EditorTerrainCommit {
  std::string document;
  std::string entityId;
  nlohmann::json before;
  nlohmann::json recipe;
  EditorTerrainSurfacePtr surface;
  std::shared_ptr<const runtime::TerrainPatch> patch;
  bool final = true;
  bool restore = false;
  // History spans the stroke; publication starts at the displayed last batch.
  std::shared_ptr<const runtime::TerrainPatch> publicationPatch;
};

struct EditorTerrainMaskSample {
  runtime::Vec3 position;
  float weight = 0;
};

// Owns transient drafts, pointer strokes, and worker lifecycle. Authored writes
// and preview installation are exclusively the workspace's responsibility.
class EditorTerrainAuthoring {
public:
  using Generator = std::function<EditorTerrainSurfacePtr(
      const nlohmann::json &, std::stop_token,
      const std::function<void(float)> &, std::string &)>;
  using Updater = std::function<std::optional<EditorTerrainUpdate>(
      const nlohmann::json &, const nlohmann::json &, EditorTerrainSurfacePtr,
      std::stop_token, const std::function<void(float)> &, std::string &)>;
  explicit EditorTerrainAuthoring(Generator generator = generateEditorTerrain,
                                  Updater updater = updateEditorTerrain);
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
  bool busy() const { return busy_ || released_; }
  bool pendingEdits() const {
    return busy_ || stroke_.has_value() || released_ || rollback_.has_value();
  }
  bool stroking() const { return stroke_.has_value(); }
  bool brushActive() const {
    return !entityId_.empty() && brush.mode != EditorTerrainBrush::Select;
  }
  bool needsResizeDecision() const;
  bool generate(std::optional<EditorTerrainResize> decision,
                std::string &error);
  void cancel();
  std::optional<EditorTerrainCommit> poll(std::string &error);
  std::optional<EditorTerrainCommit> takeRollback();
  float progress() const { return progress_.load(); }
  bool update(const EditorViewportToolInput &input,
              std::optional<runtime::Vec3> localHit, std::string &error);
  std::vector<runtime::Vec3> brushRing() const;
  EditorTerrainSurfacePtr surface() const { return surface_; }
  EditorTerrainBrushSettings brush;
  bool previewExclusions = false;
  // The applied recipe determines brush eligibility, never unapplied drafts.
  std::vector<EditorTerrainMaskSample>
  exclusionPreview(std::size_t sampleBudget = 4096) const;
  std::string brushLayerKind() const;
  bool brushLayerEnabled() const;

private:
  struct Result {
    EditorTerrainSurfacePtr surface;
    std::shared_ptr<const runtime::TerrainPatch> patch;
    std::string error;
  };
  struct SavedDraft {
    nlohmann::json baseline;
    nlohmann::json recipe;
  };
  std::map<std::pair<std::string, std::string>, SavedDraft> drafts_;
  std::string notice_;
  Generator generator_;
  Updater updater_;
  std::jthread worker_;
  std::future<Result> future_;
  std::atomic<float> progress_{0.0F};
  bool busy_ = false;
  bool cancelled_ = false;
  bool incremental_ = false;
  bool released_ = false;
  std::string document_, entityId_;
  nlohmann::json authored_, draft_, pending_, pendingBefore_;
  EditorTerrainSurfacePtr surface_;
  EditorTerrainSurfacePtr strokeSurface_;
  nlohmann::json evaluated_;
  std::shared_ptr<const runtime::TerrainPatch> strokePatch_;
  std::optional<EditorTerrainCommit> rollback_;
  std::optional<nlohmann::json> stroke_;
  std::optional<runtime::Vec3> hit_, lastStamp_;
  float flattenHeight_ = 0.0F;
  void stamp(runtime::Vec3 hit);
  void validateBrushBiome();
  void validateBrushLayer();
  bool startBatch(std::string &error);
};

} // namespace demi::editor
