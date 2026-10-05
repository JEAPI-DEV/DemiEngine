#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace demi::runtime;
namespace {
bool near(float left, float right) { return std::abs(left - right) < .0001F; }
template <class Operation> void expectInvalid(Operation operation) {
  bool failed = false;
  try {
    operation();
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);
}
TerrainRecipe flatRecipe() {
  TerrainRecipe recipe;
  recipe.size = {8, 8};
  recipe.cellsX = 8;
  recipe.cellsZ = 8;
  recipe.chunkCells = 3;
  recipe.landforms.at("default").heightVariation = 0;
  return recipe;
}
TerrainEdit hardBrush(TerrainEditKind kind, Vec2 center, float radius) {
  TerrainEdit edit;
  edit.kind = kind;
  edit.center = center;
  edit.radius = radius;
  edit.falloff = 0;
  return edit;
}
void deterministicSampling() {
  auto recipe = flatRecipe();
  recipe.landforms.at("default").heightVariation = 6;
  auto first = TerrainGenerator::generate(recipe);
  auto second =
      TerrainGenerator::generate(TerrainRecipe::parse(recipe.toJson()));
  assert(first && second && first->heights == second->heights);
  assert(first->baseHeights == first->heights);
  recipe.seed += 17;
  auto changed = TerrainGenerator::generate(recipe);
  assert(changed && changed->heights != first->heights);
  recipe.seed -= 17;
  recipe.chunkCells = 5;
  auto rechunked = TerrainGenerator::generate(recipe);
  assert(rechunked && rechunked->heights == first->heights);
  for (std::size_t index = 0; index < first->normals.size(); ++index) {
    const auto original = first->normals[index];
    const auto other = rechunked->normals[index];
    assert(original.x == other.x && original.y == other.y &&
           original.z == other.z);
    assert(near(std::hypot(original.x, original.y, original.z), 1));
  }
  for (const auto &chunk : first->chunks) {
    assert(chunk.cellsX > 0 && chunk.cellsZ > 0);
    assert(chunk.firstCellX + chunk.cellsX <= first->cellsX);
    assert(chunk.firstCellZ + chunk.cellsZ <= first->cellsZ);
  }
  recipe = flatRecipe();
  // A painted region reshapes the ground by pointing at a landform, which is
  // the whole point of separating shape from appearance: the "hill" biome keeps
  // its own look while sharing the default landform, and a second landform
  // supplies the raised ground.
  recipe.landforms.at("default").heightVariation = 0;
  recipe.landforms.at("default").baseHeight = 0;
  recipe.landforms.emplace("raised", TerrainLandform{.baseHeight = 10, .heightVariation = 0});
  recipe.biomes.emplace("hill", TerrainBiome{.landform = "raised"});
  recipe.regions.push_back({"hill", {4, 4}, 4, 1, 1});
  auto blended = TerrainGenerator::generate(recipe);
  assert(blended && near(blended->height(4, 4), 10));
  assert(near(blended->height(8, 4), 0));
  // The blend falls off with the brush, and two biomes sharing the default
  // landform must not be counted twice.
  assert(blended->height(2, 4) < blended->height(4, 4));
  assert(blended->height(6, 4) < blended->height(4, 4));
  assert(blended->biomeIds[blended->biomeIndices[blended->index(4, 4)]] ==
         "hill");
}
void snapshotSmoothing() {
  auto recipe = flatRecipe();
  auto raise = hardBrush(TerrainEditKind::Raise, {4, 4}, .5F);
  raise.amount = 9;
  recipe.edits.push_back(raise);
  auto raised = TerrainGenerator::generate(recipe);
  assert(raised && near(raised->height(4, 4), 9));
  assert(near(raised->baseHeights[raised->index(4, 4)], 0));
  auto smooth = hardBrush(TerrainEditKind::Smooth, {4, 4}, 10);
  recipe.edits.push_back(smooth);
  auto smoothed = TerrainGenerator::generate(recipe);
  assert(smoothed);
  for (int z = 3; z <= 5; ++z)
    for (int x = 3; x <= 5; ++x)
      assert(near(smoothed->height(x, z), 1));
  assert(near(smoothed->height(2, 4), 0));
  assert(near(smoothed->height(3, 4), smoothed->height(5, 4)));
  recipe = flatRecipe();
  raise.center = {0, 0};
  recipe.edits = {raise, smooth};
  auto edge = TerrainGenerator::generate(recipe);
  assert(edge && near(edge->height(0, 0), 9.F / 4));
  assert(near(edge->height(1, 0), 9.F / 6));
}
void persistentProtection() {
  auto recipe = flatRecipe();
  auto flatten = hardBrush(TerrainEditKind::Flatten, {4, 4}, 2);
  flatten.targetHeight = 7;
  recipe.edits = {flatten};
  auto flattened = TerrainGenerator::generate(recipe);
  assert(flattened && near(flattened->height(4, 4), 7));
  const auto protection = createProtectionEdit(*flattened, {4, 4}, 1);
  recipe.edits.push_back(protection);
  auto lower = hardBrush(TerrainEditKind::Lower, {4, 4}, 20);
  lower.amount = 3;
  recipe.edits.push_back(lower);
  recipe.seed = -12;
  recipe.landforms.at("default").baseHeight = 20;
  auto preserved =
      TerrainGenerator::generate(TerrainRecipe::parse(recipe.toJson()));
  assert(preserved && near(preserved->height(4, 4), 7));
  assert(near(preserved->height(0, 0), 17));
  assert(near(preserved->height(5, 4), 7));
  assert(near(preserved->height(6, 4), 4));
  assert(near(preserved->baseHeights[preserved->index(4, 4)], 20));
  recipe.edits.push_back(hardBrush(TerrainEditKind::Smooth, {4, 4}, 20));
  auto protectedSmooth = TerrainGenerator::generate(recipe);
  assert(protectedSmooth && near(protectedSmooth->height(4, 4), 7));
  auto incompatible = recipe;
  incompatible.cellsX = 16;
  expectInvalid([&] { incompatible.validate(); });
  incompatible = recipe;
  incompatible.edits[1].samples.push_back(protection.samples.front());
  expectInvalid([&] { incompatible.validate(); });
  expectInvalid([&] { (void)createProtectionEdit(*flattened, {0, 0}, -1); });
}
void validationAndComponent() {
  assert(TerrainRecipe::parse(TerrainRecipe::defaults()).toJson() ==
         TerrainRecipe::defaults());
  Entity entity;
  const nlohmann::json authored{{"format_version", 1}, {"resolution", {8, 8}}};
  Terrain3DComponent::parse({{"recipe", authored}}, entity);
  auto &component = *entity.component<Terrain3DComponent>();
  assert(component.recipe == authored);
  component.generated = std::make_shared<const HeightField>();
  Terrain3DComponent replacement;
  replacement.recipe = {{"format_version", 1}};
  const auto binding = std::find_if(Terrain3DComponent::runtimeFields.begin(),
      Terrain3DComponent::runtimeFields.end(), [](const auto &field) {
        return field.name == "recipe";
      });
  assert(binding != Terrain3DComponent::runtimeFields.end());
  binding->copy(component, replacement);
  assert(!component.generated);
  nlohmann::json serialized;
  assert(binding->read(component, serialized));
  assert(serialized == replacement.recipe);
  const auto good = flatRecipe().toJson();
  for (const auto &resolution :
       {nlohmann::json{0, 8}, nlohmann::json{-1, 8}, nlohmann::json{1.5, 8},
        nlohmann::json{8}, nlohmann::json{18446744073709551615ULL, 8}}) {
    auto bad = good;
    bad["resolution"] = resolution;
    expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  }
  auto bad = good;
  bad["format_version"] = 2;
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  bad = good;
  bad.erase("format_version");
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  bad = good;
  bad["biomes"] = nlohmann::json::object();
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  bad = good;
  bad["biomes"]["default"]["octaves"] = 1000000;
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  bad = good;
  bad["biomes"]["default"]["feature_szie"] = 2;
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  bad = good;
  bad["chunk_cell"] = 8;
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  auto edited = flatRecipe();
  edited.edits.push_back(hardBrush(TerrainEditKind::Smooth, {4, 4}, 2));
  bad = edited.toJson();
  bad["edits"][0]["amount"] = 2;
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  const auto source = TerrainGenerator::generate(flatRecipe());
  assert(source);
  edited.edits = {createProtectionEdit(*source, {4, 4}, 2)};
  bad = edited.toJson();
  bad["edits"][0]["snapshot"]["samples"][0]["heigth"] = 2;
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  bad = edited.toJson();
  bad["edits"][0]["snapshot"]["resoluton"] = {8, 8};
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  bad = good;
  bad["regions"] = {{{"biome", "missing"}, {"center", {0, 0}}, {"radius", 1}}};
  expectInvalid([&] { (void)TerrainRecipe::parse(bad); });
  auto typed = flatRecipe();
  typed.size.x = std::numeric_limits<float>::infinity();
  expectInvalid([&] { typed.validate(); });
  typed = flatRecipe();
  typed.cellsX = std::numeric_limits<int>::max();
  typed.cellsZ = std::numeric_limits<int>::max();
  expectInvalid([&] { typed.validate(); });
  typed = flatRecipe();
  typed.landforms.at("default").baseHeight = std::numeric_limits<float>::max();
  auto overflow = hardBrush(TerrainEditKind::Raise, {4, 4}, 20);
  overflow.amount = std::numeric_limits<float>::max();
  typed.edits.push_back(overflow);
  expectInvalid([&] { (void)TerrainGenerator::generate(typed); });
}
void cancellationAndProgress() {
  std::stop_source cancellation;
  cancellation.request_stop();
  assert(!TerrainGenerator::generate(flatRecipe(), cancellation.get_token()));
  cancellation = std::stop_source{};
  float lastProgress = -1;
  auto cancelled = TerrainGenerator::generate(
      flatRecipe(), cancellation.get_token(), [&](float progress) {
        assert(progress >= lastProgress && progress <= 1);
        lastProgress = progress;
        if (progress > .2F)
          cancellation.request_stop();
      });
  assert(!cancelled);
  lastProgress = -1;
  auto completed =
      TerrainGenerator::generate(flatRecipe(), {}, [&](float progress) {
        assert(progress >= lastProgress && progress <= 1);
        lastProgress = progress;
      });
  assert(completed && lastProgress == 1);
  auto edited = flatRecipe();
  edited.edits.push_back(hardBrush(TerrainEditKind::Raise, {4, 4}, 4));
  edited.edits.push_back(hardBrush(TerrainEditKind::Smooth, {4, 4}, 4));
  for (const float threshold : {.3F, .6F, .95F}) {
    cancellation = std::stop_source{};
    auto interrupted = TerrainGenerator::generate(
        edited, cancellation.get_token(), [&](float progress) {
          if (progress > threshold)
            cancellation.request_stop();
        });
    assert(!interrupted);
  }
}
} // namespace
int main() {
  deterministicSampling();
  snapshotSmoothing();
  persistentProtection();
  validationAndComponent();
  cancellationAndProgress();
}
