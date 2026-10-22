#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/TerrainCameraDemand3D.h"
#include "demi/runtime/render/bgfx3d/TerrainGpuFieldCache3D.h"
#include "demi/runtime/render/bgfx3d/TerrainMaterialArrayCache3D.h"
#include "demi/runtime/render/bgfx3d/TerrainPatchBatch3D.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

class CapturingCommands final : public RenderCommands {
public:
  struct Submission {
    std::uint16_t view = 0;
    ProgramHandle program;
    BufferSlice vertices;
    BufferSlice indices;
    DrawState state;
    std::array<TextureHandle, 4> textures{};
    std::vector<std::array<float, 20>> instances;
  };
  std::uint32_t recordsPerDraw = 100;
  std::uint32_t remainingRecords = 100;
  std::vector<Submission> submissions;

  void beginFrame(std::uint32_t available) {
    remainingRecords = available;
    submissions.clear();
  }

  std::uint32_t availableInstances(std::uint16_t stride,
                                   std::uint32_t requested) const override {
    assert(stride == 80);
    return std::min({requested, recordsPerDraw, remainingRecords});
  }
  bool configureView2D(const View2DConfig &, std::string &) override {
    return true;
  }
  bool configureView3D(const View3DConfig &, std::string &) override {
    return true;
  }
  bool submit(const TransientDraw &, std::string &) override { return false; }
  bool submit(const BufferedDraw &, std::string &) override { return false; }
  bool submit(const InstancedBufferedDraw &draw, std::string &) override {
    assert(draw.instances.stride == 80);
    const auto count = draw.instances.data.size() / draw.instances.stride;
    assert(count <= remainingRecords && count <= recordsPerDraw);
    Submission submission{.view = draw.viewId,
                          .program = draw.program,
                          .vertices = draw.vertices,
                          .indices = draw.indices,
                          .state = draw.state};
    assert(draw.textures.size() == submission.textures.size() ||
           draw.textures.size() ==
               submission.textures.size() + terrainMaterialArrayCount3D);
    for (std::size_t index = 0; index < submission.textures.size(); ++index) {
      submission.textures[index] = draw.textures[index].texture;
    }
    submission.instances.resize(count);
    std::memcpy(submission.instances.data(), draw.instances.data.data(),
                draw.instances.data.size());
    submissions.push_back(std::move(submission));
    remainingRecords -= static_cast<std::uint32_t>(count);
    return true;
  }
};

constexpr std::size_t GpuBudget = 8U * 1024U * 1024U;

std::shared_ptr<const HeightField> flatField() {
  TerrainRecipe recipe;
  recipe.size = {16, 16};
  recipe.cellsX = 16;
  recipe.cellsZ = 16;
  recipe.chunkCells = 8;
  recipe.landforms.at("default").baseHeight = 0;
  recipe.landforms.at("default").heightVariation = 0;
  const auto generated = TerrainGenerator::generate(recipe);
  assert(generated);
  return std::make_shared<const HeightField>(*generated);
}

World worldWithField(const std::shared_ptr<const HeightField> &field,
                     const TerrainRecipe &recipe) {
  World world;
  Entity owner;
  owner.id = "terrain";
  owner.setComponent(Transform3DComponent{});
  Terrain3DComponent terrain;
  terrain.asset = "asset://test/terrain";
  owner.setComponent(std::move(terrain));
  world.entities.push_back(std::move(owner));
  const TerrainFieldResolver resolveField = [&](std::string_view,
                                                nlohmann::json &sourceRecipe) {
    sourceRecipe = recipe.toJson();
    return field;
  };
  std::string error;
  if (!materializeTerrains(world, error, nullptr, {}, resolveField)) {
    throw std::runtime_error("Terrain patch fixture publication failed: " +
                             error);
  }
  return world;
}

std::shared_ptr<const TerrainGpuField3D>
readyField(GpuResources &resources,
           const std::shared_ptr<const HeightField> &field) {
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  MaterialLibrary materials(resources);
  cache.beginPresentation();
  assert(!cache.request(field, materials).field);
  cache.endPresentation();
  jobs.submit([] {}).wait();
  cache.pollCompleted();
  cache.beginPresentation();
  const auto result = cache.request(field, materials);
  assert(result.field);
  cache.endPresentation();
  return result.field;
}

TerrainPatchInstance3D instance(float translationX, float originX = 0) {
  TerrainPatchInstance3D result;
  result.transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, translationX,
                      0, 0, 1};
  result.gridOrigin = {originX, 0, 0, 0};
  return result;
}

std::array<DrawUniformValue, 8> lighting() {
  static constexpr std::array<float, 4> zero{};
  static constexpr std::array<float, 4> white{1, 1, 1, 1};
  std::array<DrawUniformValue, 8> result;
  for (auto &uniform : result) {
    uniform.values = zero;
  }
  result.front().values = white;
  return result;
}

ProgramHandle terrainProgram(GpuResources &resources, std::string &error) {
  const auto program =
      resources.createBuiltinProgram(BuiltinProgram::Terrain3D, error);
  assert(program);
  return program;
}

void sharedTopologyCombinesIndependentOwnerTransforms(GpuResources &resources) {
  TerrainPatchBatch3D batch(resources);
  const auto field = readyField(resources, flatField());
  const TerrainPatchTopologyKey3D topology{8, 8};
  assert(topology.level == TerrainLod::Full);
  const auto left = instance(-100);
  auto right = instance(100);
  right.transform[0] = 2;
  right.transform[10] = 3;
  std::string error;
  batch.beginPresentation();
  batch.beginView();
  assert(batch.addPatch(field, topology, left, {}, GpuBudget, error));
  assert(batch.addPatch(field, topology, right, {}, GpuBudget, error));
  CapturingCommands commands;
  const auto fallback = batch.flush(
      commands, 7, terrainProgram(resources, error), lighting(), false, error);
  assert(fallback.empty());
  assert(commands.submissions.size() == 1);
  const auto &submission = commands.submissions.front();
  assert(submission.view == 7 && submission.instances.size() == 2);
  assert(submission.indices.count == 8 * 8 * 6);
  assert(std::equal(left.transform.begin(), left.transform.end(),
                    submission.instances[0].begin()));
  assert(std::equal(right.transform.begin(), right.transform.end(),
                    submission.instances[1].begin()));
  assert(batch.statistics().draws == 1);
  assert(batch.statistics().instances == 2);
  assert(batch.statistics().triangles == 8 * 8 * 2 * 2);
  assert(batch.topologyBytes() > 0);
}

void oppositeSurfaceBlendingFlagsShareOneBatch(GpuResources &resources) {
  TerrainPatchBatch3D batch(resources);
  const auto field = readyField(resources, flatField());
  auto blended = instance(0);
  auto discrete = instance(20);
  blended.gridOrigin[2] = 1;
  discrete.gridOrigin[2] = 0;
  std::string error;
  batch.beginPresentation();
  batch.beginView();
  assert(batch.addPatch(field, {8, 8}, blended, {}, GpuBudget, error));
  assert(batch.addPatch(field, {8, 8}, discrete, {}, GpuBudget, error));
  CapturingCommands commands;
  assert(batch
             .flush(commands, 0, terrainProgram(resources, error), lighting(),
                    false, error)
             .empty());
  assert(commands.submissions.size() == 1);
  assert(commands.submissions[0].instances.size() == 2);
  assert(commands.submissions[0].instances[0][18] == 1);
  assert(commands.submissions[0].instances[1][18] == 0);
}

void mappedProgramsPreserveMixedCullCoverage(GpuResources &resources) {
  const auto ready = readyField(resources, flatField());
  assert(!ready->requiresFragmentCulling);
  const auto arrays = std::make_shared<TerrainMaterialArrays3D>();
  TerrainPatchBatch3D batch(resources);
  CapturingCommands commands;
  std::string error;
  const auto ordinaryProgram = terrainProgram(resources, error);
  const ProgramHandle mappedProgram{.index = 100};
  const ProgramHandle opaqueProgram{.index = 101};
  for (const bool requiresCulling : {false, true}) {
    const auto field = std::make_shared<TerrainGpuField3D>(*ready);
    field->mapped = true;
    field->requiresFragmentCulling = requiresCulling;
    for (const bool opaqueAvailable : {true, false}) {
      batch.beginPresentation();
      batch.beginView();
      commands.beginFrame(100);
      assert(batch.addPatch(field, {8, 8}, instance(0), {}, GpuBudget, error,
                            arrays));
      assert(batch
                 .flush(commands, 0, ordinaryProgram, lighting(), false, error,
                        mappedProgram,
                        opaqueAvailable ? opaqueProgram : ProgramHandle{})
                 .empty());
      assert(commands.submissions.size() == 1);
      const auto &draw = commands.submissions.front();
      const auto expected =
          !requiresCulling && opaqueAvailable ? opaqueProgram : mappedProgram;
      assert(draw.program == expected);
      assert(draw.state.cull == field->state.cull);
      assert(draw.state.depthTest == DepthTest::Less);
      assert(draw.state.writeDepth);
      assert(draw.instances.size() == 1);
      assert(draw.indices.count == 8 * 8 * 6);
    }
  }
}

void publishedSurfaceBlendingToggleKeepsGpuResources(GpuResources &resources) {
  TerrainRecipe recipe;
  recipe.size = {8, 8};
  recipe.cellsX = recipe.cellsZ = recipe.chunkCells = 8;
  recipe.landforms.at("default").heightVariation = 0;
  const auto generated = TerrainGenerator::generate(recipe);
  assert(generated);
  const auto field = std::make_shared<const HeightField>(*generated);
  auto world = worldWithField(field, recipe);
  auto *owner = world.entities.front().component<Terrain3DComponent>();
  assert(owner && owner->surfaceBlending);
  JobSystem jobs(1);
  MaterialLibrary materials(resources);
  TerrainGpuFieldCache3D fields(resources, jobs);
  fields.beginPresentation();
  assert(!fields.request(field, materials).field);
  fields.endPresentation();
  jobs.submit([] {}).wait();
  fields.pollCompleted();
  fields.beginPresentation();
  const auto ready = fields.request(field, materials).field;
  assert(ready);
  fields.endPresentation();
  TerrainPatchBatch3D batch(resources);
  TerrainDrawSelection3D selection;
  TerrainCameraDemand3D demand;
  CapturingCommands commands;
  BgfxCameraFrame3D main;
  main.cameraId = "main";
  main.camera.perspective = false;
  main.camera.orthographicSize = 100;
  main.position = {4, 100, 4};
  main.forward = {0, -1, 0};
  main.up = {0, 0, -1};
  main.viewportWidth = main.viewportHeight = 512;
  auto shadow = main;
  shadow.cameraId = "shadow";
  shadow.viewId = 1;
  std::string error;
  const auto mainProgram = terrainProgram(resources, error);
  const auto shadowProgram = resources.createBuiltinProgram(
      BuiltinProgram::Terrain3DDirectional, error);
  assert(shadowProgram);
  BufferHandle vertices;
  BufferHandle indices;
  std::size_t topologyBytes = 0;
  for (const bool blend : {true, false, true}) {
    owner->surfaceBlending = blend;
    fields.beginPresentation();
    selection.beginPresentation();
    demand.beginPresentation();
    batch.beginPresentation();
    for (const auto &view : {main, shadow}) {
      commands.beginFrame(100);
      batch.beginView();
      batch.prepareExtraction(world, view, main, selection, fields, materials,
                              demand, GpuBudget, true);
      assert(batch.suppressesOwner("terrain"));
      const auto program = view.viewId == 0 ? mainProgram : shadowProgram;
      assert(
          batch.flush(commands, view.viewId, program, lighting(), false, error)
              .empty());
      assert(commands.submissions.size() == 1);
      const auto &draw = commands.submissions.front();
      assert(draw.instances.size() == 1);
      assert(draw.instances.front()[18] == (blend ? 1.0F : 0.0F));
      assert(
          (draw.textures == std::array{ready->height, ready->normal,
                                       ready->classification, ready->palette}));
      if (!vertices) {
        vertices = draw.vertices.handle;
        indices = draw.indices.handle;
        topologyBytes = batch.topologyBytes();
      }
      assert(draw.vertices.handle == vertices &&
             draw.indices.handle == indices);
      assert(batch.topologyBytes() == topologyBytes);
      assert(fields.request(field, materials).field == ready);
      assert(ready->source == field);
    }
    fields.endPresentation();
    demand.endPresentation();
  }
}

void opaquePatchOrderUsesViewDepthWithStableTies(GpuResources &resources) {
  const auto field = flatField();
  TerrainRecipe recipe;
  recipe.size = {16, 16};
  recipe.cellsX = recipe.cellsZ = 16;
  recipe.chunkCells = 8;
  auto world = worldWithField(field, recipe);
  JobSystem jobs(1);
  MaterialLibrary materials(resources);
  TerrainGpuFieldCache3D fields(resources, jobs);
  fields.beginPresentation();
  assert(!fields.request(field, materials).field);
  fields.endPresentation();
  jobs.submit([] {}).wait();
  fields.pollCompleted();
  fields.beginPresentation();
  const auto ready = fields.request(field, materials).field;
  assert(ready);

  BgfxCameraFrame3D lodCamera;
  lodCamera.cameraId = "lod";
  lodCamera.camera.perspective = false;
  lodCamera.camera.orthographicSize = 100;
  lodCamera.position = {8, 100, 8};
  lodCamera.forward = {0, -1, 0};
  lodCamera.up = {0, 0, -1};
  lodCamera.viewportWidth = lodCamera.viewportHeight = 512;
  auto negativeZ = lodCamera;
  negativeZ.cameraId = "negative-z";
  negativeZ.position = {8, 30, 40};
  negativeZ.forward = {0, -0.5F, -1};
  negativeZ.up = {0, 1, 0};
  auto positiveZ = negativeZ;
  positiveZ.cameraId = "positive-z";
  positiveZ.position.z = -40;
  positiveZ.forward.z = 1;

  TerrainPatchBatch3D batch(resources);
  TerrainDrawSelection3D selection;
  TerrainCameraDemand3D demand;
  CapturingCommands commands;
  selection.beginPresentation();
  demand.beginPresentation();
  batch.beginPresentation();
  std::string error;
  const auto program = terrainProgram(resources, error);
  const std::array<std::array<std::array<float, 2>, 4>, 3> expectedOrigins{
      {{{{0, 8}, {8, 8}, {0, 0}, {8, 0}}},
       {{{0, 0}, {8, 0}, {0, 8}, {8, 8}}},
       {{{0, 0}, {8, 0}, {0, 8}, {8, 8}}}}};
  BufferSlice originalVertices;
  BufferSlice originalIndices;
  std::size_t originalTriangles = 0;
  std::size_t viewIndex = 0;
  for (const auto &view : {negativeZ, positiveZ, lodCamera}) {
    batch.beginView();
    commands.beginFrame(100);
    batch.prepareExtraction(world, view, lodCamera, selection, fields,
                            materials, demand, GpuBudget, true);
    assert(batch.suppressesOwner("terrain"));
    assert(batch.flush(commands, 0, program, lighting(), false, error).empty());
    assert(commands.submissions.size() == 1);
    const auto &draw = commands.submissions.front();
    assert(draw.instances.size() == 4);
    for (std::size_t index = 0; index < draw.instances.size(); ++index) {
      assert(draw.instances[index][16] == expectedOrigins[viewIndex][index][0]);
      assert(draw.instances[index][17] == expectedOrigins[viewIndex][index][1]);
      assert(draw.instances[index][18] == 1);
      const auto identity = instance(0);
      assert(std::equal(identity.transform.begin(), identity.transform.end(),
                        draw.instances[index].begin()));
    }
    assert(draw.program == program);
    assert(
        (draw.textures == std::array{ready->height, ready->normal,
                                     ready->classification, ready->palette}));
    assert(draw.state.blend == ready->state.blend);
    assert(draw.state.depthTest == ready->state.depthTest);
    assert(draw.state.cull == ready->state.cull);
    assert(draw.state.topology == ready->state.topology);
    assert(draw.state.writeColor == ready->state.writeColor);
    assert(draw.state.writeAlpha == ready->state.writeAlpha);
    assert(draw.state.writeDepth == ready->state.writeDepth);
    assert(draw.state.multisampling == ready->state.multisampling);
    if (viewIndex == 0) {
      originalVertices = draw.vertices;
      originalIndices = draw.indices;
      originalTriangles = batch.statistics().triangles;
    }
    assert(draw.vertices.handle == originalVertices.handle);
    assert(draw.vertices.count == originalVertices.count);
    assert(draw.indices.handle == originalIndices.handle);
    assert(draw.indices.count == originalIndices.count);
    assert(batch.statistics().triangles == originalTriangles);
    assert(batch.statistics().instances == 4);
    assert(batch.statistics().fallbackPatches == 0);
    ++viewIndex;
  }
  fields.endPresentation();
  demand.endPresentation();
}

void topologyIsSharedAcrossDifferentFields(GpuResources &resources) {
  TerrainPatchBatch3D batch(resources);
  const auto first = readyField(resources, flatField());
  const auto second = readyField(resources, flatField());
  const TerrainPatchTopologyKey3D topology{8, 8, TerrainLod::Half};
  std::string error;
  batch.beginPresentation();
  batch.beginView();
  assert(batch.addPatch(first, topology, instance(0), {}, GpuBudget, error));
  const auto bytes = batch.topologyBytes();
  assert(batch.addPatch(second, topology, instance(100), {}, GpuBudget, error));
  assert(batch.topologyBytes() == bytes);
  CapturingCommands commands;
  assert(batch
             .flush(commands, 0, terrainProgram(resources, error), lighting(),
                    false, error)
             .empty());
  assert(commands.submissions.size() == 2);
  assert(commands.submissions[0].vertices.handle ==
         commands.submissions[1].vertices.handle);
  assert(commands.submissions[0].indices.handle ==
         commands.submissions[1].indices.handle);
  const auto triangles = terrainLodSurfaceTriangleCount(8, 8, TerrainLod::Half);
  assert(commands.submissions[0].indices.count == triangles * 3);
  assert(batch.statistics().triangles == triangles * 2);
}

void partialCapacityReturnsOnlyUnsubmittedChunks(GpuResources &resources) {
  TerrainPatchBatch3D batch(resources);
  const auto field = readyField(resources, flatField());
  std::array<Entity, 3> entities;
  entities[0].id = "first";
  entities[1].id = "second";
  entities[2].id = "remaining";
  std::string error;
  batch.beginPresentation();
  batch.beginView();
  for (std::size_t index = 0; index < entities.size(); ++index) {
    assert(batch.addPatch(field, {8, 8, TerrainLod::Full},
                          instance(static_cast<float>(index)),
                          {{.entity = &entities[index]}}, GpuBudget, error));
  }
  CapturingCommands commands;
  commands.recordsPerDraw = 1;
  commands.beginFrame(2);
  const auto fallback = batch.flush(
      commands, 0, terrainProgram(resources, error), lighting(), false, error);
  assert(commands.submissions.size() == 2);
  assert(commands.submissions[0].instances[0][12] == 0);
  assert(commands.submissions[1].instances[0][12] == 1);
  assert(fallback.size() == 1 && fallback.front().entity == &entities[2]);
  assert(batch.statistics().draws == 2);
  assert(batch.statistics().instances == 2);
  assert(batch.statistics().fallbackPatches == 1);
  assert(batch.statistics().triangles == 8 * 8 * 2 * 2);
}

void exhaustedCapacityPreservesEveryFallbackGroup(GpuResources &resources) {
  TerrainPatchBatch3D batch(resources);
  const auto field = readyField(resources, flatField());
  std::array<Entity, 2> groups;
  groups[0].id = "ground";
  groups[1].id = "cliff";
  std::string error;
  batch.beginPresentation();
  batch.beginView();
  assert(batch.addPatch(field, {8, 8}, instance(0),
                        {{.entity = &groups[0]}, {.entity = &groups[1]}},
                        GpuBudget, error));
  CapturingCommands commands;
  commands.beginFrame(0);
  const auto fallback = batch.flush(
      commands, 0, terrainProgram(resources, error), lighting(), false, error);
  assert(commands.submissions.empty());
  assert(fallback.size() == 2);
  assert(fallback[0].entity == &groups[0] && fallback[1].entity == &groups[1]);
  assert(batch.statistics().fallbackPatches == 1);
}

void preparingFieldKeepsFullNativeCoverage(GpuResources &resources) {
  const auto field = flatField();
  TerrainRecipe recipe;
  recipe.size = {16, 16};
  recipe.cellsX = 16;
  recipe.cellsZ = 16;
  recipe.chunkCells = 8;
  auto world = worldWithField(field, recipe);
  JobSystem jobs(1);
  MaterialLibrary materials(resources);
  TerrainGpuFieldCache3D fields(resources, jobs);
  TerrainPatchBatch3D batch(resources);
  TerrainDrawSelection3D selection;
  TerrainCameraDemand3D demand;
  fields.beginPresentation();
  batch.beginPresentation();
  batch.beginView();
  selection.beginPresentation();
  demand.beginPresentation();
  BgfxCameraFrame3D camera;
  camera.cameraId = "game";
  camera.camera.perspective = false;
  camera.camera.orthographicSize = 100;
  camera.position = {8, 100, 8};
  camera.forward = {0, -1, 0};
  camera.up = {0, 0, -1};
  camera.viewportWidth = 512;
  camera.viewportHeight = 512;
  batch.prepareExtraction(world, camera, camera, selection, fields, materials,
                          demand, GpuBudget, true);
  assert(!batch.suppressesOwner("terrain"));
  assert(batch.forcesFullOwner("terrain"));
  CapturingCommands commands;
  std::string error;
  assert(batch
             .flush(commands, 0, terrainProgram(resources, error), lighting(),
                    false, error)
             .empty());
  assert(commands.submissions.empty());
  fields.endPresentation();
  demand.endPresentation();
}

void changedBudgetAndInvalidFieldCannotBypassAdmission(
    GpuResources &resources) {
  TerrainPatchBatch3D batch(resources);
  const auto field = readyField(resources, flatField());
  std::string error;
  batch.beginPresentation();
  batch.beginView();
  assert(!batch.addPatch({}, {8, 8}, instance(0), {}, GpuBudget, error));
  assert(batch.topologyBytes() == 0);
  assert(batch.addPatch(field, {8, 8}, instance(0), {}, GpuBudget, error));
  assert(batch.topologyBytes() > 0);
  batch.beginPresentation();
  batch.beginView();
  assert(!batch.addPatch(field, {8, 8}, instance(0), {}, 0, error));
  CapturingCommands commands;
  assert(batch
             .flush(commands, 0, terrainProgram(resources, error), lighting(),
                    false, error)
             .empty());
  assert(commands.submissions.empty());
}

void missingBiomeGroupKeepsTheRemainingNativeFallback(GpuResources &resources) {
  TerrainRecipe recipe;
  recipe.size = {8, 8};
  recipe.cellsX = 8;
  recipe.cellsZ = 8;
  recipe.chunkCells = 8;
  recipe.landforms.at("default").baseHeight = 0;
  recipe.landforms.at("default").heightVariation = 0;
  auto generated = TerrainGenerator::generate(recipe);
  assert(generated);
  generated->biomeIds = {"default", "other"};
  generated->biomeColors = {{1, 1, 1, 1}, {0.5F, 0.5F, 0.5F, 1}};
  generated->biomeMaterials.clear();
  generated->biomeTextureScales.assign(2, 1.0F);
  for (int z = 0; z <= generated->cellsZ; ++z) {
    for (int x = 4; x <= generated->cellsX; ++x) {
      generated->biomeIndices.set(generated->index(x, z), 1);
    }
  }
  const auto field = std::make_shared<const HeightField>(std::move(*generated));
  auto world = worldWithField(field, recipe);
  std::string error;
  assert(world.entities.size() == 3);
  world.entities.eraseIf( [](const Entity &entity) {
    const auto *surface = entity.component<TerrainRenderLod>();
    return surface && surface->biome == "other";
  });
  rebuildTerrainOwnership(world);
  assert(world.entities.size() == 2);

  JobSystem jobs(1);
  MaterialLibrary materials(resources);
  TerrainGpuFieldCache3D fields(resources, jobs);
  fields.beginPresentation();
  assert(!fields.request(field, materials).field);
  fields.endPresentation();
  jobs.submit([] {}).wait();
  fields.pollCompleted();
  fields.beginPresentation();
  assert(fields.request(field, materials).field);
  TerrainPatchBatch3D batch(resources);
  TerrainDrawSelection3D selection;
  TerrainCameraDemand3D demand;
  batch.beginPresentation();
  batch.beginView();
  selection.beginPresentation();
  demand.beginPresentation();
  BgfxCameraFrame3D camera;
  camera.cameraId = "game";
  camera.camera.perspective = false;
  camera.camera.orthographicSize = 100;
  camera.position = {4, 100, 4};
  camera.forward = {0, -1, 0};
  camera.up = {0, 0, -1};
  camera.viewportWidth = 512;
  camera.viewportHeight = 512;
  batch.prepareExtraction(world, camera, camera, selection, fields, materials,
                          demand, GpuBudget, true);
  assert(!batch.suppressesOwner("terrain"));
  CapturingCommands commands;
  assert(batch
             .flush(commands, 0, terrainProgram(resources, error), lighting(),
                    false, error)
             .empty());
  assert(commands.submissions.empty());
  fields.endPresentation();
  demand.endPresentation();
}

void shadowSelectionUsesTheOriginatingCamera(GpuResources &resources) {
  TerrainRecipe recipe;
  recipe.size = {8, 8};
  recipe.cellsX = 8;
  recipe.cellsZ = 8;
  recipe.chunkCells = 8;
  recipe.landforms.at("default").baseHeight = 0;
  recipe.landforms.at("default").heightVariation = 0;
  auto generated = TerrainGenerator::generate(recipe);
  assert(generated);
  generated->heights.set(generated->index(3, 3), 20.0F);
  const auto field = std::make_shared<const HeightField>(std::move(*generated));
  auto world = worldWithField(field, recipe);
  std::string error;

  JobSystem jobs(1);
  MaterialLibrary materials(resources);
  TerrainGpuFieldCache3D fields(resources, jobs);
  fields.beginPresentation();
  assert(!fields.request(field, materials).field);
  fields.endPresentation();
  jobs.submit([] {}).wait();
  fields.pollCompleted();
  fields.beginPresentation();
  assert(fields.request(field, materials).field);
  TerrainPatchBatch3D batch(resources);
  TerrainDrawSelection3D selection;
  TerrainCameraDemand3D demand;
  batch.beginPresentation();
  batch.beginView();
  selection.beginPresentation();
  demand.beginPresentation();
  BgfxCameraFrame3D origin;
  origin.cameraId = "game";
  origin.position = {4, 12, 24};
  origin.forward = {0, -0.4F, -1};
  origin.viewportWidth = 512;
  origin.viewportHeight = 512;
  BgfxCameraFrame3D shadow = origin;
  shadow.cameraId = "shadow";
  shadow.camera.perspective = false;
  shadow.camera.orthographicSize = 10000;
  shadow.position = {4, 100, 4};
  shadow.forward = {0, -1, 0};
  shadow.up = {0, 0, -1};
  shadow.viewId = 1;
  batch.prepareExtraction(world, shadow, origin, selection, fields, materials,
                          demand, GpuBudget, true);
  assert(batch.suppressesOwner("terrain"));
  assert(!batch.forcesFullOwner("terrain"));
  CapturingCommands commands;
  const auto program = resources.createBuiltinProgram(
      BuiltinProgram::Terrain3DDirectional, error);
  assert(program);
  assert(batch.flush(commands, shadow.viewId, program, lighting(), false, error)
             .empty());
  assert(!commands.submissions.empty());
  assert(batch.statistics().selectedLevels[0] == 1);
  assert(commands.submissions.front().indices.count == 8 * 8 * 6);
  fields.endPresentation();
  demand.endPresentation();
}

void insufficientTopologyBudgetDoesNotPublishResources(
    GpuResources &resources) {
  TerrainPatchBatch3D batch(resources);
  const auto field = readyField(resources, flatField());
  std::string error;
  batch.beginPresentation();
  batch.beginView();
  assert(!batch.addPatch(field, {8, 8}, instance(0), {}, 0, error));
  assert(!error.empty());
  assert(batch.topologyBytes() == 0);
  CapturingCommands commands;
  assert(batch
             .flush(commands, 0, terrainProgram(resources, error), lighting(),
                    false, error)
             .empty());
  assert(commands.submissions.empty());
}

} // namespace

int main() {
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      {.api = GraphicsApi::Noop, .width = 64, .height = 64, .vsync = false},
      error));
  {
    auto resources = createBgfxGpuResources();
    sharedTopologyCombinesIndependentOwnerTransforms(*resources);
    oppositeSurfaceBlendingFlagsShareOneBatch(*resources);
    mappedProgramsPreserveMixedCullCoverage(*resources);
    publishedSurfaceBlendingToggleKeepsGpuResources(*resources);
    opaquePatchOrderUsesViewDepthWithStableTies(*resources);
    topologyIsSharedAcrossDifferentFields(*resources);
    partialCapacityReturnsOnlyUnsubmittedChunks(*resources);
    exhaustedCapacityPreservesEveryFallbackGroup(*resources);
    insufficientTopologyBudgetDoesNotPublishResources(*resources);
    shadowSelectionUsesTheOriginatingCamera(*resources);
    changedBudgetAndInvalidFieldCannotBypassAdmission(*resources);
    preparingFieldKeepsFullNativeCoverage(*resources);
    missingBiomeGroupKeepsTheRemainingNativeFallback(*resources);
  }
  graphics.shutdown();
  std::cout << "Terrain patch batch checks passed\n";
}
