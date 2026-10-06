#pragma once

#include "demi/runtime/terrain/TerrainRecipe.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

// Per-sample values every rule is evaluated against. Deriving these needs the
// finished base surface, which is why biome rules run as a field pass after
// generation rather than inside the per-point base sample.
struct TerrainRuleContext {
  float height = 0;
  // Degrees, 0 flat to 90 vertical.
  float slope = 0;
  // 0 dry to 1 saturated, from the biome-placement sub-seed field.
  float moisture = 0;
  // World units to the nearest sample at or below sea level.
  float waterDistance = 0;
  TerrainSubstrate substrate = TerrainSubstrate::Soil;
};

class TerrainRuleContextBuilder {
public:
  explicit TerrainRuleContextBuilder(const TerrainRecipe &recipe);
  ~TerrainRuleContextBuilder();
  TerrainRuleContextBuilder(TerrainRuleContextBuilder &&) noexcept;
  TerrainRuleContextBuilder &operator=(TerrainRuleContextBuilder &&) noexcept;
  // Fills every sample's derived values. The base heights must already be
  // complete, because slope and water distance read the whole field.
  void build(const TerrainRecipe &recipe,
             const std::vector<float> &baseHeights, int cellsX, int cellsZ,
             Vec2 size);
  const std::vector<TerrainRuleContext> &contexts() const;
  // Sea level in world units. Samples at or below it are water.
  static float seaLevel(const TerrainRecipe &recipe);

private:
  struct MoistureField;
  std::vector<TerrainRuleContext> contexts_;
  std::unique_ptr<MoistureField> moisture_;
};

// Result of assigning one sample, retained so the editor and CLI can explain a
// choice rather than making the author guess.
struct TerrainRuleDecision {
  std::size_t biome = 0;
  std::string ruleId;
  // True when a painted region or the default biome overrode the rules.
  bool overridden = false;
};

// Assigns biomes for the whole field. Returns a decision per sample in the same
// index order as the heightfield. The recipe's painted regions are applied on
// top, so a stroke always beats a rule that also matches.
std::vector<TerrainRuleDecision> assignTerrainBiomes(
    const TerrainRecipe &recipe, const std::vector<TerrainRuleContext> &contexts,
    int cellsX, int cellsZ, Vec2 size);

} // namespace demi::runtime
