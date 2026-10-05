#pragma once

#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainPreset.h"
#include "editor/EditorTerrainRuntime.h"
#include "editor/EditorViewportTool.h"
#include <atomic>
#include <cstddef>
#include <future>
#include <map>
#include <thread>
#include <utility>

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

// Overlay read from the generated field. Moisture is deliberately absent: it is
// a separate context pass and is not committed, so showing a zero would lie.
enum class EditorTerrainMaskPreview { None, Biome, Elevation, Slope };

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

// A committed-field sample. Color and biome come from the field itself, so the
// presentation adapter never invents a palette the terrain does not use.
struct EditorTerrainRuleMaskSample {
  runtime::Vec3 position;
  float weight = 0;
  std::size_t biome = 0;
  runtime::Color color;
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
  using InputResolver = std::function<runtime::TerrainGenerationInputs(
      const runtime::TerrainRecipe &)>;
  using ResolvedUpdater = std::function<std::optional<EditorTerrainUpdate>(
      const nlohmann::json &, const nlohmann::json &, EditorTerrainSurfacePtr,
      const runtime::TerrainGenerationInputs &, std::stop_token,
      const std::function<void(float)> &, std::string &)>;
  explicit EditorTerrainAuthoring(Generator generator = generateEditorTerrain,
                                  Updater updater = updateEditorTerrain);
  // Called on the editor thread before dispatch. The worker captures only the
  // returned palette snapshot and fingerprint, never the asset registry.
  void setInputResolver(InputResolver resolver) {
    inputResolver_ = std::move(resolver);
  }
  // Receives the already-resolved after-inputs for global regeneration. Until
  // installed, a changed palette is refused before starting the worker.
  void setResolvedUpdater(ResolvedUpdater updater) {
    resolvedUpdater_ = std::move(updater);
  }
  ~EditorTerrainAuthoring();
  EditorTerrainAuthoring(const EditorTerrainAuthoring &) = delete;
  EditorTerrainAuthoring &operator=(const EditorTerrainAuthoring &) = delete;

  void bind(std::string document, std::string entityId,
            const nlohmann::json &recipe, EditorTerrainSurfacePtr surface,
            std::string draftIdentity = {});
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
  void setBrushTargetSelected(bool selected) {
    brushTargetSelected_ = selected;
  }
  bool brushActive() const {
    return brushTargetSelected_ && !entityId_.empty() &&
           brush.mode != EditorTerrainBrush::Select;
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
  EditorTerrainMaskPreview maskPreview = EditorTerrainMaskPreview::None;
  // The applied recipe determines brush eligibility, never unapplied drafts.
  std::vector<EditorTerrainMaskSample>
  exclusionPreview(std::size_t sampleBudget = 4096) const;
  // Stride-samples the committed field. Draft rule edits cannot move the
  // overlay until Generate commits them; authored rule values stay exact and
  // are queried through the immutable native result.
  std::vector<EditorTerrainRuleMaskSample>
  ruleMaskPreview(EditorTerrainMaskPreview mode,
                  std::size_t sampleBudget = 4096) const;
  std::string brushLayerKind() const;
  bool brushLayerEnabled() const;

  // Landscape presets offered by the Terrain Graph settings. Populated by the
  // workspace from the project's asset registry when the project or its asset
  // metadata changes, because presets load through the registry and this
  // object deliberately holds no asset access. Transient: never authored, never
  // part of undo history or the runtime world.
  void setPresets(std::vector<runtime::TerrainPreset> presets) {
    presets_ = std::move(presets);
  }
  [[nodiscard]] const std::vector<runtime::TerrainPreset> &presets() const {
    return presets_;
  }
  [[nodiscard]] const runtime::TerrainPreset *preset(std::string_view id) const;

  // Merges a preset's generation keys into the draft while keeping the
  // author's regions, edits and exclusions, and stamps preset_id /
  // preset_version. Returns false and fills error with the loader/merge
  // message verbatim when the merged recipe is rejected, leaving the draft
  // untouched.
  [[nodiscard]] bool applyPreset(const runtime::TerrainPreset &preset,
                                 std::string &error);
  // Removes both provenance keys. Provenance is all-or-nothing, so a recipe
  // without it is exactly the recipe the engine already accepts.
  [[nodiscard]] bool clearPreset(std::string &error);
  // True when applying the preset would move the grid that authored protection
  // snapshots were captured on. Such an apply must go through the existing
  // resize decision instead of silently invalidating those snapshots.
  [[nodiscard]] bool
  presetChangesGrid(const runtime::TerrainPreset &preset) const;

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
  InputResolver inputResolver_;
  ResolvedUpdater resolvedUpdater_;
  std::jthread worker_;
  std::future<Result> future_;
  std::atomic<float> progress_{0.0F};
  bool busy_ = false;
  bool cancelled_ = false;
  bool incremental_ = false;
  bool released_ = false;
  bool brushTargetSelected_ = true;
  std::string document_, entityId_;
  // Shared asset drafts follow their source identity, not the placement used
  // for viewport picking. Inline recipes default to the entity identity.
  std::string draftIdentity_;
  nlohmann::json authored_, draft_, pending_, pendingBefore_;
  EditorTerrainSurfacePtr surface_;
  EditorTerrainSurfacePtr strokeSurface_;
  std::vector<runtime::TerrainPreset> presets_;
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
  std::optional<runtime::TerrainGenerationInputs>
  prepareInputs(const nlohmann::json &recipe, EditorTerrainSurfacePtr previous,
                std::string &error) const;
};

} // namespace demi::editor
