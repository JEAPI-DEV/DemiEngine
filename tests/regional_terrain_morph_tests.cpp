#include "demi/runtime/render/bgfx3d/TerrainGpuFieldData3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/terrain/RegionalTerrainMorph.h"
#include "demi/runtime/terrain/RegionalTerrainPresentation.h"
#include "demi/runtime/terrain/RegionalTerrainSystem.h"
#include "demi/runtime/terrain/TerrainMeshBuilder.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
#include <thread>

using namespace demi::runtime;
namespace {
TerrainRegionConfig config() {
  TerrainRegionConfig result;
  result.recipe.size = {128, 128};
  result.recipe.chunkCells = 8;
  result.recipe.landforms.at("default").heightVariation = 50;
  result.recipe.landforms.at("default").featureSize = 30;
  result.baseSampleSpacing = 2;
  result.normalSampleSpacing = 1;
  return result;
}
using Fields =
    std::map<TerrainRegionPatchKey, std::shared_ptr<const HeightField>>;
Fields sampleCover(const RegionalTerrainSampler &sampler,
                   const std::vector<TerrainRegionPatchKey> &cover) {
  Fields result;
  for (const auto key : regionCoverVariants(cover)) {
    result[key.patch] = std::make_shared<HeightField>(
        sampler.samplePatch(key.patch, key.stitch).field);
  }
  return result;
}
void endpointAndSeamAgreement() {
  auto source = config();
  RegionalTerrainSampler sampler(source);
  const std::vector<TerrainRegionPatchKey> coarse{
      {1, 0, 0}, {1, 1, 0}, {1, 0, 1}, {1, 1, 1}};
  const std::vector<TerrainRegionPatchKey> fine{{2, 0, 0}, {2, 1, 0}, {2, 0, 1},
                                                {2, 1, 1}, {1, 1, 0}, {1, 0, 1},
                                                {1, 1, 1}};
  const auto oldFields = sampleCover(sampler, coarse);
  const auto newFields = sampleCover(sampler, fine);
  RegionalTerrainCoverSurface from(source, oldFields);
  RegionalTerrainCoverSurface to(source, newFields);
  for (const auto &[key, grid] : newFields) {
    const auto forward = makeRegionalMorphField(source, key, *grid, from, to);
    const auto reverse = makeRegionalMorphField(source, key, *grid, to, from);
    const auto box = regionPatchRectangle(source, key);
    for (int z = 0; z <= grid->cellsZ; ++z) {
      for (int x = 0; x <= grid->cellsX; ++x) {
        const auto i = grid->index(x, z);
        const double wx = box.x + double(x) * box.width / grid->cellsX;
        const double wz = box.z + double(z) * box.depth / grid->cellsZ;
        assert(std::abs(forward->morphFromHeights->at(i) -
                        from.height(wx, wz)) < 0.0001F);
        assert(std::abs(forward->heights.at(i) - to.height(wx, wz)) < 0.0001F);
        assert(forward->heights.at(i) == reverse->morphFromHeights->at(i));
        assert(forward->morphFromHeights->at(i) == reverse->heights.at(i));
      }
    }
  }
  // Neighboring fine children share the same old and new edge heights. A
  // common blend factor therefore closes their seam throughout the transition.
  const auto left = makeRegionalMorphField(source, {2, 0, 0},
                                           *newFields.at({2, 0, 0}), from, to);
  const auto right = makeRegionalMorphField(source, {2, 1, 0},
                                            *newFields.at({2, 1, 0}), from, to);
  for (int z = 0; z <= left->cellsZ; ++z) {
    for (float amount : {0.F, .25F, .5F, .75F, 1.F}) {
      const auto a = left->index(left->cellsX, z);
      const auto b = right->index(0, z);
      assert(std::abs(std::lerp(left->morphFromHeights->at(a),
                                left->heights.at(a), amount) -
                      std::lerp(right->morphFromHeights->at(b),
                                right->heights.at(b), amount)) < .0001F);
    }
  }
  auto saddle = std::make_shared<HeightField>();
  saddle->cellsX = saddle->cellsZ = 1;
  saddle->heights = {0, 0, 0, 8};
  RegionalTerrainCoverSurface triangle(source, {{{0, 0, 0}, saddle}});
  assert(triangle.height(64, 64) == 0); // Bilinear interpolation would be 2.
  assert(triangle.height(96, 96) == 4);
}
void gpuAndNativeFallbackAgree() {
  const auto source = config();
  auto original = std::make_shared<HeightField>(
      RegionalTerrainSampler(source).samplePatch({0, 0, 0}).field);
  auto changed = std::make_shared<HeightField>(*original);
  auto start = std::make_shared<TerrainSamples<float>>(original->heights);
  for (std::size_t i = 0; i < start->size(); ++i) {
    start->set(i, start->at(i) + 10);
  }
  changed->morphFromHeights = start;
  const auto gpu = render::prepareTerrainGpuFieldData3D(*changed, {}, 4096);
  assert(gpu.compatible && gpu.heightFormat == render::TextureFormat::RGBA32F);
  assert(gpu.heights.size() == original->heights.size() * 4);
  World world;
  Entity owner;
  owner.id = "patch";
  owner.setComponent(Transform3DComponent{});
  Terrain3DComponent terrain;
  terrain.generated = original;
  owner.setComponent(terrain);
  world.entities.push_back(owner);
  const auto triangles =
      terrain_detail::buildChunkTriangles(*original, original->chunks.front());
  const auto &surface = triangles.begin()->second;
  std::string error;
  auto mesh = buildTerrainMeshEntity(owner, "surface", surface.vertices,
                                     surface.normals, surface.uvs,
                                     original->biomeColors.front(), {}, error);
  assert(mesh);
  mesh->setComponent(TerrainRenderLod{
      prepareTerrainRenderLod(original, original->chunks.front()),
      original->biomeIds.front(), "patch",
      mesh->component<MeshRendererComponent>()->revision});
  world.entities.push_back(std::move(*mesh));
  const auto collider = findEntity(world, "surface")
                            ->component<ModelCollider3DComponent>()
                            ->inlineGeometry;
  RegionalTerrainMorph morph(world, {{"patch", changed, {"surface"}}});
  const auto verify = [&](float amount) {
    const auto *entity = findEntity(world, "surface");
    assert(entity->component<ModelCollider3DComponent>()->inlineGeometry ==
           collider);
    assert(
        findEntity(world, "patch")->component<TerrainVisualMorph>()->amount ==
        amount);
    for (const auto vertex :
         entity->component<MeshRendererComponent>()->vertices) {
      const int x =
          int(std::lround(vertex.x / original->size.x * original->cellsX));
      const int z =
          int(std::lround(vertex.z / original->size.y * original->cellsZ));
      const auto i = original->index(x, z);
      const float expected =
          std::lerp(gpu.heights[i * 4 + 1], gpu.heights[i * 4], amount);
      assert(std::abs(vertex.y - expected) < .0001F);
      assert(original->heights.at(i) == changed->heights.at(i));
    }
  };
  verify(0);
  assert(!morph.advance(world, .125F));
  verify(.5F);
  assert(morph.advance(world, .125F));
  verify(1);
}
void movingBootstrapBecomesReady() {
  auto source = std::make_shared<TerrainRegionConfig>(config());
  World world;
  Entity owner;
  owner.id = "moving-bootstrap";
  owner.setComponent(Transform3DComponent{});
  Terrain3DComponent terrain;
  terrain.region = source;
  owner.setComponent(terrain);
  world.entities.push_back(std::move(owner));
  RegionalTerrainSystem streaming({.physics = false, .vegetation = false});
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  unsigned frame = 0;
  while (world.entities.size() == 1) {
    std::string error;
    const float x = float((frame++ * 7) % 128);
    assert(streaming.update(world, {x, 30, 64}, error, nullptr, .016F));
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(!regionalTerrainPreparing(world));
}

void presentationWaitsForReplacementResources() {
  auto source = std::make_shared<TerrainRegionConfig>(config());
  World world;
  Entity owner;
  owner.id = "render-gated-region";
  owner.setComponent(Transform3DComponent{});
  Terrain3DComponent terrain;
  terrain.region = source;
  owner.setComponent(terrain);
  world.entities.push_back(std::move(owner));
  RegionalTerrainSystem streaming({.physics = false, .vegetation = false});
  auto lease = std::make_shared<int>(0);
  const auto update = [&](Vec3 camera) {
    std::string error;
    assert(streaming.update(world, camera, error, nullptr, .05F));
    auto state = findEntity(world, "render-gated-region")
                     ->component<RegionalTerrainPresentation>()->render;
    state->renderer = lease;
    return state;
  };
  const Vec3 nearCamera{64, 20, 64};
  auto state = update(nearCamera);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (state->pending.empty()) {
    state = update(nearCamera);
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  for (int frame = 0; frame < 10; ++frame) {
    update(nearCamera);
    assert(world.entities.size() == 1);
  }
  state->prepared = true;
  update(nearCamera);
  assert(!regionalTerrainPreparing(world));
  const auto previousCount = world.entities.size();
  const Vec3 farCamera{10000, 10000, 10000};
  while (state->pending.empty()) {
    update(farCamera);
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(!state->prepared);
  for (int frame = 0; frame < 10; ++frame) {
    update(farCamera);
    assert(world.entities.size() == previousCount);
    for (const auto &entity : world.entities) {
      assert(!entity.hasComponent<TerrainVisualMorph>());
    }
  }
  state->prepared = true;
  update(farCamera);
  assert(std::ranges::any_of(world.entities, [](const auto &entity) {
    const auto *morph = entity.template component<TerrainVisualMorph>();
    return morph && morph->amount == 0;
  }));
  // A hidden/destroyed renderer cannot leave a headless world blocked.
  lease.reset();
  state->prepared = false;
  assert(!state->waiting());
}

void streamingMorphsBothDirections() {
  auto source = std::make_shared<TerrainRegionConfig>(config());
  World world;
  Entity owner;
  owner.id = "region";
  owner.setComponent(Transform3DComponent{});
  Terrain3DComponent terrain;
  terrain.region = source;
  owner.setComponent(terrain);
  world.entities.push_back(std::move(owner));
  RegionalTerrainSystem streaming({.physics = false, .vegetation = false});
  const auto settle = [&](Vec3 camera, bool expectMorph, bool expectSplit = false) {
    bool seen = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
      std::string error;
      if (!streaming.update(world, camera, error, nullptr, .05F)) {
        throw std::runtime_error(error);
      }
      if (expectSplit) {
        const auto preparation = findEntity(world, "region")
            ->component<RegionalTerrainPresentation>()->render;
        std::set<const HeightField *> requested;
        for (const auto &patch : preparation->active) {
          assert(requested.insert(patch.field.get()).second);
        }
      }
      for (const auto &entity : world.entities) {
        if (const auto *morph = entity.component<TerrainVisualMorph>();
            morph && morph->amount < 1) {
          seen = true;
          assert(!regionalTerrainPreparing(world));
        }
      }
      assert(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (streaming.hasPendingWork());
    assert(seen == expectMorph);
  };
  settle({64, 20, 64}, false);
  settle({10000, 10000, 10000}, true);
  settle({64, 20, 64}, true, true);
}
} // namespace
int main() {
  endpointAndSeamAgreement();
  gpuAndNativeFallbackAgree();
  movingBootstrapBecomesReady();
  streamingMorphsBothDirections();
  presentationWaitsForReplacementResources();
  std::cout << "Regional terrain morph tests passed\n";
}
