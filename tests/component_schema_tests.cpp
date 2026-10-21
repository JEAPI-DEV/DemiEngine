#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainGenerator.h"

#include <limits>
#include <vector>

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {

void testTerrainTextureQuality() {
  using namespace demi::runtime;
  const auto *descriptor =
      scene_loading::findComponentDescriptor("Environment3D");
  if (!descriptor) {
    throw std::runtime_error("Environment descriptor is missing");
  }
  constexpr std::size_t MiB = 1024U * 1024U;
  Entity entity;
  const nlohmann::json authored{{"terrain_texture_max_size", 2048},
                                {"terrain_texture_cache_mb", 32},
                                {"terrain_detail_distance", 350}};
  descriptor->parse(authored, entity);
  auto *environment = entity.component<Environment3DComponent>();
  if (environment->terrainTextureMaxSize != 2048 ||
      environment->terrainTextureCacheBytes != 32 * MiB ||
      environment->terrainDetailDistance != 350 ||
      descriptor->serialize(entity) != authored) {
    throw std::runtime_error("Terrain texture quality did not round trip");
  }
  environment->shadowDistance = 42;
  auto disabled = authored;
  disabled["terrain_texture_cache_mb"] = 0;
  if (!descriptor->patch(entity, "terrain_texture_cache_mb", disabled) ||
      environment->terrainTextureCacheBytes != 0 ||
      environment->terrainTextureMaxSize != 2048 ||
      environment->terrainDetailDistance != 350 ||
      environment->shadowDistance != 42 ||
      descriptor->inspect(entity).at("terrain_texture_cache_mb") != 0) {
    throw std::runtime_error(
        "Terrain texture budget patch lost unrelated state");
  }
  Entity omitted;
  descriptor->parse(nlohmann::json::object(), omitted);
  const auto *defaults = omitted.component<Environment3DComponent>();
  if (defaults->terrainTextureMaxSize != 1024 ||
      defaults->terrainTextureCacheBytes != 128 * MiB ||
      defaults->terrainDetailDistance != 200 ||
      !descriptor->serialize(omitted).empty()) {
    throw std::runtime_error(
        "Terrain texture quality canonical defaults changed");
  }
  const auto schema = scene_loading::componentSchema(*descriptor);
  const auto &properties = schema.at("properties");
  if (properties.at("terrain_texture_max_size").at("default") != 1024 ||
      properties.at("terrain_texture_max_size").at("enum").size() != 8 ||
      properties.at("terrain_texture_cache_mb").at("default") != 128 ||
      properties.at("terrain_detail_distance").at("default") != 200) {
    throw std::runtime_error(
        "Terrain texture quality schema differs from native defaults");
  }
  for (const auto &invalid : std::vector<nlohmann::json>{
           {{"terrain_texture_max_size", 0}},
           {{"terrain_texture_max_size", 65}},
           {{"terrain_texture_max_size", 16384}},
           {{"terrain_texture_max_size", "1024"}},
           {{"terrain_texture_cache_mb", -1}},
           {{"terrain_texture_cache_mb", 0.5}},
           {{"terrain_texture_cache_mb",
             std::uint64_t(std::numeric_limits<std::size_t>::max() / MiB) + 1}},
           {{"terrain_detail_distance", 0}},
           {{"terrain_detail_distance", -1}},
           {{"terrain_detail_distance", 1e100}},
           {{"terrain_detail_distance", 1e-100}},
           {{"terrain_detail_distance",
             std::numeric_limits<double>::infinity()}},
           {{"terrain_detail_distance", "200"}}}) {
    bool rejected = false;
    try {
      Entity invalidEntity;
      descriptor->parse(invalid, invalidEntity);
    } catch (const std::exception &) {
      rejected = true;
    }
    if (!rejected) {
      throw std::runtime_error(
          "Invalid terrain texture quality was accepted: " + invalid.dump());
    }
  }
}

void testTerrainPreparationBudgets() {
  using namespace demi::runtime;
  const auto *descriptor =
      scene_loading::findComponentDescriptor("Environment3D");
  if (!descriptor) {
    throw std::runtime_error("Environment descriptor is missing");
  }
  Entity entity;
  const nlohmann::json authored{{"terrain_lod_cache_mb", 12},
                                {"terrain_lod_prepare_jobs", 3},
                                {"terrain_gpu_cache_mb", 24}};
  descriptor->parse(authored, entity);
  auto *environment = entity.component<Environment3DComponent>();
  constexpr std::size_t MiB = 1024U * 1024U;
  if (!environment || environment->terrainLodCacheBytes != 12 * MiB ||
      environment->terrainLodPrepareJobs != 3 ||
      environment->terrainGpuCacheBytes != 24 * MiB ||
      descriptor->serialize(entity) != authored) {
    throw std::runtime_error("Terrain preparation budget round trip failed");
  }
  environment->shadowDistance = 42.0F;
  const nlohmann::json patched{{"terrain_lod_cache_mb", 0},
                               {"terrain_lod_prepare_jobs", 3},
                               {"terrain_gpu_cache_mb", 24}};
  if (!descriptor->patch(entity, "terrain_lod_cache_mb", patched) ||
      environment->terrainLodCacheBytes != 0 ||
      environment->terrainLodPrepareJobs != 3 ||
      environment->terrainGpuCacheBytes != 24 * MiB ||
      environment->shadowDistance != 42.0F ||
      descriptor->inspect(entity).at("terrain_lod_cache_mb") != 0) {
    throw std::runtime_error(
        "Terrain preparation budget patch lost native state");
  }
  const nlohmann::json gpuDisabled{{"terrain_lod_cache_mb", 0},
                                   {"terrain_lod_prepare_jobs", 3},
                                   {"terrain_gpu_cache_mb", 0}};
  if (!descriptor->patch(entity, "terrain_gpu_cache_mb", gpuDisabled) ||
      environment->terrainGpuCacheBytes != 0 ||
      environment->terrainLodCacheBytes != 0 ||
      environment->terrainLodPrepareJobs != 3 ||
      environment->shadowDistance != 42.0F ||
      descriptor->inspect(entity).at("terrain_gpu_cache_mb") != 0 ||
      descriptor->serialize(entity) != gpuDisabled) {
    throw std::runtime_error(
        "Terrain GPU budget mutation changed unrelated native state");
  }
  Entity omitted;
  descriptor->parse(nlohmann::json::object(), omitted);
  if (omitted.component<Environment3DComponent>()->terrainGpuCacheBytes !=
          128 * MiB ||
      descriptor->defaults().at("terrain_gpu_cache_mb") != 128 ||
      !descriptor->serialize(omitted).empty()) {
    throw std::runtime_error(
        "Terrain GPU budget canonical default was materialized or changed");
  }
  const auto schema = scene_loading::componentSchema(*descriptor);
  const auto &gpuSchema = schema.at("properties").at("terrain_gpu_cache_mb");
  const auto maximumMiB = std::numeric_limits<std::size_t>::max() / MiB;
  if (gpuSchema.at("minimum") != 0 || gpuSchema.at("maximum") != maximumMiB ||
      gpuSchema.at("default") != 128) {
    throw std::runtime_error(
        "Terrain GPU budget schema differs from its native contract");
  }
  Entity maximum;
  const nlohmann::json maximumAuthored{{"terrain_gpu_cache_mb", maximumMiB}};
  descriptor->parse(maximumAuthored, maximum);
  if (maximum.component<Environment3DComponent>()->terrainGpuCacheBytes !=
          maximumMiB * MiB ||
      descriptor->serialize(maximum) != maximumAuthored) {
    throw std::runtime_error(
        "Maximum addressable terrain GPU budget did not round trip");
  }
  bool overflowRejected = false;
  try {
    Entity invalid;
    descriptor->parse({{"terrain_gpu_cache_mb", std::uint64_t(maximumMiB) + 1}},
                      invalid);
  } catch (const std::exception &) {
    overflowRejected = true;
  }
  if (!overflowRejected) {
    throw std::runtime_error("Overflowing terrain GPU budget was accepted");
  }
  for (const char *field : {"terrain_lod_cache_mb", "terrain_lod_prepare_jobs",
                            "terrain_gpu_cache_mb"}) {
    for (const nlohmann::json &invalid :
         {nlohmann::json(-1), nlohmann::json(0.5), nlohmann::json("invalid")}) {
      bool rejected = false;
      try {
        Entity invalidEntity;
        Environment3DComponent::parse({{field, invalid}}, invalidEntity);
      } catch (const std::exception &) {
        rejected = true;
      }
      if (!rejected) {
        throw std::runtime_error(
            "Invalid terrain preparation budget was accepted");
      }
    }
  }
}

void testTerrainSurfaceBlendingContract() {
  using namespace demi::runtime;
  using namespace demi::runtime::scene_loading;
  const auto require = [](bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  };
  const auto *descriptor = findComponentDescriptor("Terrain3D");
  require(descriptor != nullptr, "Terrain3D descriptor is missing");
  require(descriptor->defaults().at("surface_blending") == true,
          "Terrain3D blending default differs from native values");
  const auto schema = componentSchema(*descriptor);
  const auto &field = schema.at("properties").at("surface_blending");
  require(field.at("type") == "boolean" && field.at("default") == true,
          "Terrain3D blending schema lost its boolean/default contract");
  Entity entity;
  const nlohmann::json authored{{"asset", "asset://terrain/test"}};
  descriptor->parse(authored, entity);
  auto *terrain = entity.component<Terrain3DComponent>();
  require(terrain && terrain->surfaceBlending,
          "Terrain3D omitted blending did not default to enabled");
  require(descriptor->serialize(entity) == authored,
          "Terrain3D materialized omitted blending into authored source");
  const auto generated = std::make_shared<const HeightField>();
  terrain->generated = generated;
  terrain->recipe = {{"retained_input", true}};
  const auto recipe = terrain->recipe;
  const nlohmann::json changed{{"asset", "asset://terrain/test"},
                               {"surface_blending", false}};
  require(descriptor->patch(entity, "surface_blending", changed),
          "Terrain3D blending native field patch failed");
  require(!terrain->surfaceBlending && terrain->generated == generated &&
              terrain->recipe == recipe && terrain->lodErrorPixels == 2,
          "Terrain3D blending edit invalidated generated geometry or unrelated "
          "state");
  require(descriptor->serialize(entity) == changed,
          "Terrain3D explicit blending did not round trip");
  terrain->surfaceBlending = true;
  require(descriptor->inspect(entity).at("surface_blending") == true,
          "Terrain3D inspector missed native blending mutation");
  require(descriptor->serialize(entity) == changed,
          "Native blending mutation rewrote authored source");
  for (const auto &invalid :
       {nlohmann::json(0), nlohmann::json("false"), nlohmann::json(nullptr)}) {
    std::string error;
    require(!Terrain3DComponent::validateAuthored(
                {{"surface_blending", invalid}}, error),
            "Terrain3D accepted a nonboolean blending value");
    require(error.find("surface_blending") != std::string::npos,
            "Terrain3D blending validation did not name the rejected field");
  }
}

void testTerrainScatterExclusionsContract() {
  using namespace demi::runtime;
  using namespace demi::runtime::scene_loading;
  const auto require = [](bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  };
  const auto *descriptor = findComponentDescriptor("Terrain3D");
  require(descriptor != nullptr, "Terrain3D descriptor is missing");
  require(descriptor->defaults().at("scatter_exclusions") ==
              nlohmann::json::array(),
          "Terrain3D scatter exclusions lost their native empty default");
  const auto schema = componentSchema(*descriptor);
  const auto &field = schema.at("properties").at("scatter_exclusions");
  require(field.at("type") == "array" &&
              field.at("items").at("type") == "string",
          "Terrain3D scatter exclusions lost reflected string-array schema");
  Entity entity;
  const nlohmann::json authored{{"asset", "asset://terrain/test"}};
  descriptor->parse(authored, entity);
  auto *terrain = entity.component<Terrain3DComponent>();
  require(terrain && terrain->scatterExclusions.empty(),
          "Omitted scatter exclusions did not default to an empty list");
  require(descriptor->serialize(entity) == authored,
          "Omitted scatter exclusions were materialized into source");
  const auto generated = std::make_shared<const HeightField>();
  terrain->generated = generated;
  terrain->recipe = {{"retained_input", true}};
  const nlohmann::json changed{
      {"asset", "asset://terrain/test"},
      {"scatter_exclusions", {"land|scatter|tree|4|7"}}};
  require(descriptor->patch(entity, "scatter_exclusions", changed),
          "Scatter exclusions native field patch failed");
  require(terrain->scatterExclusions ==
              std::vector<std::string>{"land|scatter|tree|4|7"},
          "Scatter exclusion patch did not update native storage");
  require(terrain->generated == generated &&
              terrain->recipe.at("retained_input") == true,
          "Scatter exclusion patch rebuilt geometry or changed unrelated live "
          "state");
  require(descriptor->serialize(entity) == changed,
          "Scatter exclusions did not round trip");
  for (const auto &invalid :
       {nlohmann::json(1), nlohmann::json::array({1}),
        nlohmann::json::array({""}), nlohmann::json::array({"same", "same"})}) {
    std::string error;
    require(!Terrain3DComponent::validateAuthored(
                {{"scatter_exclusions", invalid}}, error),
            "Terrain3D accepted invalid scatter exclusions");
    require(error.find("scatter_exclusions") != std::string::npos,
            "Scatter exclusion validation omitted the field name");
  }
}

void testTerrainLodQualityContract() {
  using namespace demi::runtime;
  using namespace demi::runtime::scene_loading;
  const auto require = [](bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  };
  const auto *descriptor = findComponentDescriptor("Terrain3D");
  require(descriptor != nullptr, "Terrain3D descriptor is missing");
  require(descriptor->defaults().at("lod_error_pixels") == 2.0F,
          "Terrain3D quality default differs from native values");
  const auto schema = componentSchema(*descriptor);
  const auto &qualitySchema = schema.at("properties").at("lod_error_pixels");
  require(qualitySchema.at("minimum") == 0.0,
          "Terrain3D quality schema lost its nonnegative constraint");
  require(qualitySchema.at("default") == 2.0,
          "Terrain3D quality schema lost its default");

  Entity entity;
  const nlohmann::json authored{{"asset", "asset://terrain/test"},
                                {"lod_error_pixels", 3.5}};
  descriptor->parse(authored, entity);
  auto *terrain = entity.component<Terrain3DComponent>();
  require(terrain != nullptr && terrain->lodErrorPixels == 3.5F,
          "Terrain3D quality parsing failed");
  require(descriptor->serialize(entity) == authored,
          "Terrain3D quality authored round trip failed");
  auto retained = std::make_shared<const HeightField>();
  terrain->generated = retained;
  terrain->recipe = {{"retained_input", true}};
  const auto nativeRecipe = terrain->recipe;
  const nlohmann::json patched{{"asset", "asset://terrain/test"},
                               {"lod_error_pixels", 0.0}};
  require(descriptor->patch(entity, "lod_error_pixels", patched),
          "Terrain3D quality field binding failed");
  require(terrain->lodErrorPixels == 0.0F && terrain->generated == retained &&
              terrain->recipe == nativeRecipe &&
              terrain->asset == "asset://terrain/test",
          "Terrain3D quality mutation invalidated unrelated terrain state");
  require(descriptor->serialize(entity) == patched,
          "Terrain3D field patch did not publish its authored values");
  terrain->lodErrorPixels = 1.25F;
  require(descriptor->inspect(entity).at("lod_error_pixels") == 1.25F,
          "Terrain3D inspection missed current native quality");
  require(descriptor->serialize(entity) == patched,
          "Terrain3D native quality mutation rewrote patched authored source");

  for (const auto &invalid :
       {nlohmann::json(-1.0), nlohmann::json("invalid"),
        nlohmann::json(std::numeric_limits<double>::infinity()),
        nlohmann::json(std::numeric_limits<double>::quiet_NaN())}) {
    std::string error;
    require(!Terrain3DComponent::validateAuthored(
                {{"lod_error_pixels", invalid}}, error) &&
                error.find("lod_error_pixels") != std::string::npos,
            "Terrain3D accepted invalid render quality");
  }
  Entity omitted;
  descriptor->parse(nlohmann::json::object(), omitted);
  require(omitted.component<Terrain3DComponent>()->lodErrorPixels == 2.0F &&
              descriptor->serialize(omitted).empty(),
          "Terrain3D omission materialized authored defaults");
}

} // namespace

int main() {
  try {
    testTerrainPreparationBudgets();
    testTerrainTextureQuality();
    testTerrainLodQualityContract();
    testTerrainScatterExclusionsContract();
    testTerrainSurfaceBlendingContract();
    using namespace demi::runtime;
    using namespace demi::runtime::scene_loading;
    Entity entity;
    const auto *transform = findComponentDescriptor("Transform3D");
    const auto *body = findComponentDescriptor("Rigidbody3D");
    transform->parse({{"position", {1, 2, 3}}}, entity);
    body->parse(nlohmann::json::object(), entity);
    const auto authored = entity.serializedComponents;
    entity.updateComponent<Transform3DComponent>(
        [](Transform3DComponent &value) { value.position = {7, 8, 9}; });
    entity.component<Rigidbody3DComponent>()->velocity = {4, 5, 6};
    if (transform->inspect(entity).at("position") !=
            nlohmann::json({7, 8, 9}) ||
        body->inspect(entity).at("velocity") != nlohmann::json({4, 5, 6}) ||
        body->inspect(entity).at("body_type") != "dynamic" ||
        transform->inspect(entity).at("scale") != nlohmann::json({1, 1, 1}) ||
        entity.serializedComponents != authored ||
        transform->serialize(entity).at("position") !=
            nlohmann::json({1, 2, 3}))
      throw std::runtime_error("Runtime inspection must read native values "
                               "without changing authored serialization");
    entity.updateComponent<Transform3DComponent>(
        [](Transform3DComponent &value) { value.position.x = 12; });
    if (transform->inspect(entity).at("position")[0] != 12)
      throw std::runtime_error("Runtime inspection cached an earlier snapshot");
    Entity absent;
    if (!transform->inspect(absent).empty())
      throw std::runtime_error("Absent component produced inspection values");

    std::ifstream input(DEMI_SOURCE_DIR "/schemas/components.schema.json");
    if (!input) {
      std::cerr << "Could not read checked-in component schema.\n";
      return 1;
    }
    const auto checkedIn = nlohmann::json::parse(input);
    const auto generated =
        demi::runtime::scene_loading::canonicalComponentSchema();
    if (checkedIn != generated) {
      std::cerr << "Component schema differs from the runtime registry. "
                   "Regenerate it with demi schema export.\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "Component schema generation failed: " << error.what() << '\n';
    return 1;
  }
}
