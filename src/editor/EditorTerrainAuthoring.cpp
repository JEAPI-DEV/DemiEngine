#include "editor/EditorTerrainAuthoring.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <utility>

namespace demi::editor {

EditorTerrainAuthoring::EditorTerrainAuthoring(Generator generator,
                                               Updater updater)
    : generator_(std::move(generator)), updater_(std::move(updater)) {}
EditorTerrainAuthoring::~EditorTerrainAuthoring() {
  cancel();
  if (worker_.joinable())
    worker_.join();
}

void EditorTerrainAuthoring::bind(std::string document, std::string entityId,
                                  const nlohmann::json &recipe,
                                  EditorTerrainSurfacePtr surface) {
  if (document_ == document && entityId_ == entityId && authored_ == recipe) {
    validateBrushBiome();
    validateBrushLayer();
    if (!stroke_ && !released_ && !rollback_)
      surface_ = std::move(surface);
    return;
  }
  const bool sameSelection = document_ == document && entityId_ == entityId;
  const auto previousBrush = brush;
  unbind();
  if (sameSelection)
    brush = previousBrush;
  document_ = std::move(document);
  entityId_ = std::move(entityId);
  authored_ = recipe;
  draft_ = recipe;
  const auto saved = drafts_.find({document_, entityId_});
  if (saved != drafts_.end()) {
    if (saved->second.baseline == authored_)
      draft_ = saved->second.recipe;
    // A successful local commit adopts the draft itself as the new baseline.
    else if (!sameSelection || saved->second.recipe != authored_)
      notice_ = "Terrain draft discarded for " + entityId_ +
                ": the applied recipe changed since the draft was created.";
    drafts_.erase(saved);
  }
  validateBrushBiome();
  validateBrushLayer();
  surface_ = std::move(surface);
}

void EditorTerrainAuthoring::unbind() {
  cancel();
  if (!entityId_.empty() && hasDraftChanges())
    drafts_[{document_, entityId_}] = SavedDraft{authored_, draft_};
  stroke_.reset();
  hit_.reset();
  lastStamp_.reset();
  document_.clear();
  entityId_.clear();
  surface_.reset();
  brush.mode = EditorTerrainBrush::Select;
}

void EditorTerrainAuthoring::discardDraft() {
  drafts_.erase({document_, entityId_});
  draft_ = authored_;
  validateBrushBiome();
  validateBrushLayer();
}

std::string EditorTerrainAuthoring::takeNotice() {
  std::string notice = std::move(notice_);
  notice_.clear();
  return notice;
}

void EditorTerrainAuthoring::validateBrushBiome() {
  const auto biomes =
      authored_.value("biomes", defaultEditorTerrainRecipe().at("biomes"));
  if (biomes.contains(brush.biome))
    return;
  brush.biome = authored_.value("default_biome", std::string("default"));
  if (!biomes.contains(brush.biome))
    brush.biome = biomes.empty() ? std::string{} : biomes.begin().key();
}

std::string EditorTerrainAuthoring::brushLayerKind() const {
  switch (brush.mode) {
  case EditorTerrainBrush::Biome:
    return "biome";
  case EditorTerrainBrush::Protect:
    return "protection";
  case EditorTerrainBrush::Exclusion:
    return "exclusion";
  default:
    return "sculpt";
  }
}

bool EditorTerrainAuthoring::brushLayerEnabled() const {
  const auto layers =
      authored_.value("layers", defaultEditorTerrainRecipe().at("layers"));
  return std::ranges::any_of(layers, [&](const auto &layer) {
    return layer.value("id", std::string{}) == brush.layer &&
           layer.value("kind", std::string{}) == brushLayerKind() &&
           layer.value("enabled", true);
  });
}

void EditorTerrainAuthoring::validateBrushLayer() {
  const auto layers =
      authored_.value("layers", defaultEditorTerrainRecipe().at("layers"));
  const auto eligible = [&](const auto &layer) {
    return layer.value("kind", std::string{}) == brushLayerKind();
  };
  if (std::ranges::any_of(layers, [&](const auto &layer) {
        return eligible(layer) &&
               layer.value("id", std::string{}) == brush.layer;
      }))
    return;
  const auto found = std::ranges::find_if(layers, [&](const auto &layer) {
    return eligible(layer) && layer.value("enabled", true);
  });
  brush.layer = found == layers.end()
                    ? std::string{}
                    : found->at("id").template get<std::string>();
}

bool EditorTerrainAuthoring::needsResizeDecision() const {
  const bool stamps =
      (authored_.contains("regions") && !authored_.at("regions").empty()) ||
      (authored_.contains("edits") && !authored_.at("edits").empty()) ||
      (authored_.contains("exclusions") && !authored_.at("exclusions").empty());
  return stamps && (draft_.value("size", nlohmann::json{}) !=
                        authored_.value("size", nlohmann::json{}) ||
                    draft_.value("resolution", nlohmann::json{}) !=
                        authored_.value("resolution", nlohmann::json{}));
}

bool EditorTerrainAuthoring::generate(
    std::optional<EditorTerrainResize> decision, std::string &error) {
  if (busy_ || entityId_.empty() || stroke_) {
    error = "Finish the current terrain operation first.";
    return false;
  }
  if (needsResizeDecision() && !decision) {
    error = "Choose how to preserve terrain edits after resizing.";
    return false;
  }
  if (needsResizeDecision() && decision == EditorTerrainResize::Keep) {
    if (draft_.contains("edits") &&
        std::ranges::any_of(draft_.at("edits"), [](const auto &edit) {
          return edit.value("type", std::string{}) == "protect";
        })) {
      error = "Protection snapshots require the original grid. Clear edits or "
              "keep the original size and resolution.";
      return false;
    }
  }
  pending_ = draft_;
  if (decision == EditorTerrainResize::Clear) {
    pending_["regions"] = nlohmann::json::array();
    pending_["edits"] = nlohmann::json::array();
    pending_["exclusions"] = nlohmann::json::array();
  }
  try {
    runtime::TerrainRecipe::parse(pending_).validate();
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
  pendingBefore_ = authored_;
  progress_ = 0.0F;
  cancelled_ = false;
  incremental_ = false;
  try {
    std::promise<Result> promise;
    future_ = promise.get_future();
    // Replacing a completed jthread joins only the already completed job.
    worker_ = std::jthread(
        [this, recipe = pending_, before = authored_, previous = surface_,
         promise = std::move(promise)](std::stop_token stop) mutable {
          Result result;
          try {
            const auto progress = [this](float value) {
              progress_ = std::clamp(value, 0.0F, 1.0F);
            };
            if (previous && previous->heightField()) {
              if (auto update = updater_(before, recipe, previous, stop,
                                         progress, result.error)) {
                result.surface = std::move(update->surface);
                result.patch = std::move(update->patch);
              }
            } else {
              result.surface = generator_(recipe, stop, progress, result.error);
            }
          } catch (const std::exception &exception) {
            result.error = exception.what();
          } catch (...) {
            result.error = "Terrain generation failed.";
          }
          promise.set_value(std::move(result));
        });
    busy_ = true;
    return true;
  } catch (const std::exception &exception) {
    future_ = {};
    busy_ = false;
    cancelled_ = true;
    error =
        std::string("Could not start terrain generation: ") + exception.what();
    return false;
  }
}

void EditorTerrainAuthoring::cancel() {
  cancelled_ = true;
  worker_.request_stop();
  if (strokeSurface_) {
    if (strokePatch_)
      rollback_ = EditorTerrainCommit{
          document_,      entityId_,    authored_, authored_,
          strokeSurface_, strokePatch_, false,     true};
    surface_ = strokeSurface_;
    draft_ = authored_;
  }
  stroke_.reset();
  strokeSurface_.reset();
  strokePatch_.reset();
  released_ = false;
  lastStamp_.reset();
}

std::optional<EditorTerrainCommit> EditorTerrainAuthoring::takeRollback() {
  return std::exchange(rollback_, std::nullopt);
}

bool EditorTerrainAuthoring::startBatch(std::string &error) {
  if (busy_ || !stroke_ || *stroke_ == evaluated_)
    return true;
  pending_ = *stroke_;
  pendingBefore_ = evaluated_;
  progress_ = 0;
  cancelled_ = false;
  incremental_ = true;
  try {
    std::promise<Result> promise;
    future_ = promise.get_future();
    worker_ = std::jthread([this, before = pendingBefore_, after = pending_,
                            previous = surface_, promise = std::move(promise)](
                               std::stop_token stop) mutable {
      Result result;
      try {
        auto update = updater_(
            before, after, previous, stop,
            [this](float value) { progress_ = std::clamp(value, 0.0F, 1.0F); },
            result.error);
        if (update) {
          result.surface = std::move(update->surface);
          result.patch = std::move(update->patch);
        }
      } catch (const std::exception &exception) {
        result.error = exception.what();
      } catch (...) {
        result.error = "Terrain update failed.";
      }
      promise.set_value(std::move(result));
    });
    busy_ = true;
    return true;
  } catch (const std::exception &exception) {
    error = std::string("Could not start terrain update: ") + exception.what();
    cancel();
    return false;
  }
}

std::optional<EditorTerrainCommit>
EditorTerrainAuthoring::poll(std::string &error) {
  if (rollback_)
    return takeRollback();
  if (released_ && !busy_ && stroke_ && *stroke_ == evaluated_) {
    EditorTerrainCommit commit{document_,  entityId_, authored_,
                               evaluated_, surface_,  strokePatch_};
    stroke_.reset();
    strokeSurface_.reset();
    strokePatch_.reset();
    released_ = false;
    return commit;
  }
  if (!busy_ ||
      future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return std::nullopt;
  Result result = future_.get();
  busy_ = false;
  if (cancelled_)
    return std::nullopt;
  if (!result.surface) {
    error = result.error.empty() ? "Terrain generation returned no surface."
                                 : result.error;
    if (incremental_)
      cancel();
    return std::nullopt;
  }
  if (!incremental_)
    return EditorTerrainCommit{document_,
                               entityId_,
                               pendingBefore_,
                               pending_,
                               std::move(result.surface),
                               std::move(result.patch)};
  surface_ = std::move(result.surface);
  evaluated_ = pending_;
  try {
    strokePatch_ = mergeEditorTerrainPatches(strokePatch_, result.patch);
  } catch (const std::exception &exception) {
    error = exception.what();
    cancel();
    return std::nullopt;
  }
  const bool final = released_ && *stroke_ == evaluated_;
  EditorTerrainCommit commit{
      document_,  entityId_, authored_,
      evaluated_, surface_,  final ? strokePatch_ : result.patch,
      final,      false,     result.patch};
  if (final) {
    stroke_.reset();
    strokeSurface_.reset();
    strokePatch_.reset();
    released_ = false;
  } else if (!startBatch(error)) {
    return std::nullopt;
  }
  return commit;
}

void EditorTerrainAuthoring::stamp(runtime::Vec3 hit) {
  auto &recipe = *stroke_;
  runtime::TerrainRecipe stampRecipe;
  stampRecipe.layers = runtime::TerrainRecipe::parse(authored_).layers;
  if (brush.mode == EditorTerrainBrush::Biome) {
    stampRecipe.biomes.try_emplace(brush.biome);
    runtime::TerrainRegion region;
    region.biome = brush.biome;
    region.center = {hit.x, hit.z};
    region.radius = brush.radius;
    region.strength = brush.strength;
    region.falloff = brush.falloff;
    region.layer = brush.layer;
    stampRecipe.regions.push_back(std::move(region));
    recipe["regions"].push_back(stampRecipe.toJson()["regions"].back());
  } else if (brush.mode == EditorTerrainBrush::Exclusion) {
    runtime::TerrainExclusion exclusion;
    exclusion.center = {hit.x, hit.z};
    exclusion.radius = brush.radius;
    exclusion.strength = brush.strength;
    exclusion.falloff = brush.falloff;
    exclusion.value = brush.exclusionValue;
    exclusion.layer = brush.layer;
    stampRecipe.exclusions.push_back(std::move(exclusion));
    recipe["exclusions"].push_back(stampRecipe.toJson()["exclusions"].back());
  } else {
    runtime::TerrainEdit edit;
    edit.center = {hit.x, hit.z};
    edit.radius = brush.radius;
    edit.strength = brush.strength;
    edit.falloff = brush.falloff;
    edit.layer = brush.layer;
    switch (brush.mode) {
    case EditorTerrainBrush::Raise:
      edit.kind = runtime::TerrainEditKind::Raise;
      break;
    case EditorTerrainBrush::Lower:
      edit.kind = runtime::TerrainEditKind::Lower;
      break;
    case EditorTerrainBrush::Flatten:
      edit.kind = runtime::TerrainEditKind::Flatten;
      edit.targetHeight = flattenHeight_;
      break;
    case EditorTerrainBrush::Smooth:
      edit.kind = runtime::TerrainEditKind::Smooth;
      break;
    case EditorTerrainBrush::Protect: {
      auto protection =
          surface_->protection({hit.x, hit.z}, brush.radius, 1.0F, 0.0F);
      protection["layer"] = brush.layer;
      recipe["edits"].push_back(std::move(protection));
      lastStamp_ = hit;
      return;
    }
    default:
      return;
    }
    stampRecipe.edits.push_back(std::move(edit));
    recipe["edits"].push_back(stampRecipe.toJson()["edits"].back());
  }
  lastStamp_ = hit;
}

bool EditorTerrainAuthoring::update(const EditorViewportToolInput &input,
                                    std::optional<runtime::Vec3> localHit,
                                    std::string &error) {
  hit_ = input.hovered ? localHit : std::nullopt;
  if (input.cancelPressed || !input.focused || input.navigationModifier) {
    if (stroke_)
      cancel();
    return true;
  }
  if ((busy_ && (!incremental_ || cancelled_)) || released_ || !brushActive() ||
      !surface_)
    return true;
  if (input.leftPressed && hit_) {
    if (brush.mode == EditorTerrainBrush::Exclusion &&
        (!std::isfinite(brush.exclusionValue) || brush.exclusionValue < 0 ||
         brush.exclusionValue > 1)) {
      error = "Exclusion brush value must be in [0,1].";
      return false;
    }
    validateBrushLayer();
    if (!brushLayerEnabled()) {
      error = "Choose an enabled layer for this brush before painting.";
      return false;
    }
    if (!std::isfinite(brush.radius) || brush.radius <= 0 ||
        !std::isfinite(brush.strength) || brush.strength < 0 ||
        brush.strength > 1 || !std::isfinite(brush.falloff) ||
        brush.falloff < 0) {
      error = "Brush radius must be positive, strength in [0,1], and falloff "
              "nonnegative.";
      return false;
    }
    if (draft_ != authored_) {
      error = "Generate or discard the draft settings before painting.";
      return false;
    }
    if (brush.mode == EditorTerrainBrush::Biome &&
        !authored_.value("biomes", defaultEditorTerrainRecipe().at("biomes"))
             .contains(brush.biome)) {
      error = "Choose an existing biome before painting.";
      return false;
    }
    stroke_ = authored_;
    evaluated_ = authored_;
    strokeSurface_ = surface_;
    strokePatch_.reset();
    flattenHeight_ = hit_->y;
    lastStamp_.reset();
  }
  if (!stroke_)
    return true;
  if (input.leftDown && hit_) {
    const float spacing = std::max(brush.radius * 0.2F, 0.01F);
    if (!lastStamp_)
      stamp(*hit_);
    else {
      const runtime::Vec3 start = *lastStamp_;
      const float dx = hit_->x - start.x, dz = hit_->z - start.z;
      const float distance = std::hypot(dx, dz);
      for (float step = spacing; step <= distance; step += spacing) {
        const float t = step / distance;
        stamp({start.x + dx * t, hit_->y, start.z + dz * t});
      }
    }
  }
  if (input.leftReleased || !input.leftDown) {
    draft_ = *stroke_;
    released_ = true;
    lastStamp_.reset();
    if (*stroke_ == authored_) {
      cancel();
      return true;
    }
  }
  return startBatch(error);
}

std::vector<EditorTerrainMaskSample>
EditorTerrainAuthoring::exclusionPreview(std::size_t sampleBudget) const {
  std::vector<EditorTerrainMaskSample> points;
  if (!previewExclusions || !surface_)
    return points;
  const auto field = surface_->heightField();
  if (!field || field->exclusions.empty())
    return points;
  // The overlay is sampled to its screen budget; authored mask samples remain
  // exact and are queried through the immutable native result.
  const auto stride = std::max<std::int64_t>(
      1, static_cast<std::int64_t>(std::ceil(
             std::sqrt(double(field->exclusions.size()) /
                       double(std::max<std::size_t>(sampleBudget, 1))))));
  for (std::int64_t z = 0; z <= field->cellsZ; z += stride) {
    for (std::int64_t x = 0; x <= field->cellsX; x += stride) {
      const auto index = field->index(static_cast<int>(x), static_cast<int>(z));
      const float weight = field->exclusions[index];
      if (weight <= 0)
        continue;
      const auto position =
          field->position(static_cast<int>(x), static_cast<int>(z));
      points.push_back(
          {{position.x, field->heights[index] + .04F, position.y}, weight});
    }
  }
  return points;
}

std::vector<runtime::Vec3> EditorTerrainAuthoring::brushRing() const {
  std::vector<runtime::Vec3> ring;
  if (!hit_ || !surface_ || !brushActive())
    return ring;
  for (int i = 0; i <= 64; ++i) {
    const float angle = static_cast<float>(i) * 6.283185307F / 64.0F;
    const runtime::Vec2 point{hit_->x + std::cos(angle) * brush.radius,
                              hit_->z + std::sin(angle) * brush.radius};
    const auto height = surface_->height(point);
    // Gaps at terrain edges are kept as NaNs for the presentation adapter.
    ring.push_back({point.x, height ? *height + 0.03F : NAN, point.y});
  }
  return ring;
}
} // namespace demi::editor
