#include "editor/EditorTerrainAuthoring.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

namespace demi::editor {

EditorTerrainAuthoring::EditorTerrainAuthoring(Generator generator)
    : generator_(std::move(generator)) {}
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
  surface_ = std::move(surface);
}

void EditorTerrainAuthoring::unbind() {
  if (!entityId_.empty() && hasDraftChanges())
    drafts_[{document_, entityId_}] = SavedDraft{authored_, draft_};
  cancel();
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

bool EditorTerrainAuthoring::needsResizeDecision() const {
  const bool stamps =
      (authored_.contains("regions") && !authored_.at("regions").empty()) ||
      (authored_.contains("edits") && !authored_.at("edits").empty());
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
  try {
    std::promise<Result> promise;
    future_ = promise.get_future();
    // Replacing a completed jthread joins only the already completed job.
    worker_ = std::jthread([this, recipe = pending_,
                            promise = std::move(promise)](
                               std::stop_token stop) mutable {
      Result result;
      try {
        result.surface = generator_(
            recipe, stop,
            [this](float value) { progress_ = std::clamp(value, 0.0F, 1.0F); },
            result.error);
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
  stroke_.reset();
  lastStamp_.reset();
}

std::optional<EditorTerrainCommit>
EditorTerrainAuthoring::poll(std::string &error) {
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
    return std::nullopt;
  }
  return EditorTerrainCommit{document_, entityId_, pendingBefore_, pending_,
                             std::move(result.surface)};
}

void EditorTerrainAuthoring::stamp(runtime::Vec3 hit) {
  auto &recipe = *stroke_;
  runtime::TerrainRecipe stampRecipe;
  if (brush.mode == EditorTerrainBrush::Biome) {
    stampRecipe.biomes.try_emplace(brush.biome);
    runtime::TerrainRegion region;
    region.biome = brush.biome;
    region.center = {hit.x, hit.z};
    region.radius = brush.radius;
    region.strength = brush.strength;
    region.falloff = brush.falloff;
    stampRecipe.regions.push_back(std::move(region));
    recipe["regions"].push_back(stampRecipe.toJson()["regions"].back());
  } else {
    runtime::TerrainEdit edit;
    edit.center = {hit.x, hit.z};
    edit.radius = brush.radius;
    edit.strength = brush.strength;
    edit.falloff = brush.falloff;
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
    case EditorTerrainBrush::Protect:
      recipe["edits"].push_back(
          surface_->protection({hit.x, hit.z}, brush.radius, 1.0F, 0.0F));
      lastStamp_ = hit;
      return;
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
    stroke_.reset();
    lastStamp_.reset();
    return true;
  }
  if (busy_ || !brushActive() || !surface_)
    return true;
  if (input.leftPressed && hit_) {
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
    draft_ = std::move(*stroke_);
    stroke_.reset();
    lastStamp_.reset();
    return generate(std::nullopt, error);
  }
  return true;
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
