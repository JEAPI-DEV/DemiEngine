#include "demi/assets/RenderAsset.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/runtime/camera/CameraRenderScheduler3D.h"
#include "demi/runtime/render/Lighting3D.h"
#include "demi/runtime/render/ParticleSystem2D.h"
#include "demi/runtime/render/ParticleSystem3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/ParticleEmitter2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PointLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

using namespace demi;
using namespace demi::runtime;

void write(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream output(path);
  output << contents;
}

Entity camera(std::string id, const int priority, const bool primary,
              const bool enabled = true) {
  Entity result;
  result.id = std::move(id);
  result.enabled = enabled;
  result.setComponent(Transform3DComponent{});
  result.setComponent(
      Camera3DComponent{.priority = priority, .primary = primary});
  return result;
}

Entity emitter3D(std::string id, const int burst, const int budget,
                 const float lifetime = 1.0F) {
  Entity result;
  result.id = std::move(id);
  result.setComponent(Transform3DComponent{});
  result.setComponent(ParticleEmitter3DComponent{
      .rate = 0.0F,
      .burst = burst,
      .lifetime = lifetime,
      .maxParticles = budget,
      .mobileMaxParticles = budget,
  });
  return result;
}

Entity emitter2D(std::string id, const int burst, const int budget,
                 const float lifetime = 1.0F) {
  Entity result;
  result.id = std::move(id);
  result.setComponent(Transform2DComponent{});
  result.setComponent(ParticleEmitter2DComponent{
      .rate = 0.0F,
      .burst = burst,
      .lifetime = lifetime,
      .maxParticles = budget,
      .mobileMaxParticles = budget,
  });
  return result;
}

bool assetContracts() {
  const auto root =
      std::filesystem::temp_directory_path() / "demi_render_phase6_assets";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  write(
      root / "valid.material.json",
      R"({"format_version":1,"shader":"builtin://lit","textures":{"albedo":"asset://texture"},"parameters":{"roughness":0.3,"base_color":[1,0.5,0.25,1]},"render_state":{"blend":"alpha","cull":"none","depth_write":false}})");
  write(root / "bad.material.json",
        R"({"format_version":1,"render_state":{"blend":"magic"}})");
  write(root / "shader.vert", "void main(){}\n");
  write(root / "shader.frag", "void main(){}\n");
  write(root / "shader_android.vert", "void main(){}\n");
  write(root / "shader_android.frag", "void main(){}\n");
  write(
      root / "valid.shader.json",
      R"({"format_version":1,"vertex":"shader.vert","fragment":"shader.frag","platform_sources":{"android":{"vertex":"shader_android.vert","fragment":"shader_android.frag"}},"platform_fallbacks":{"android":"builtin://unlit","linux":"builtin://lit"}})");
  write(
      root / "bad.shader.json",
      R"({"format_version":1,"vertex":"shader.vert","fragment":"shader.frag","platform_fallbacks":{"android":"not-an-asset"}})");
  write(
      root / "valid.target.json",
      R"({"format_version":1,"width":320,"height":180,"format":"rgba8","depth":true})");
  write(root / "bad.target.json",
        R"({"format_version":1,"width":0,"height":9000,"format":"rgba16f"})");
  write(
      root / "typed-wrong.material.json",
      R"({"format_version":1,"shader":42,"render_state":{"depth_test":"yes"}})");
  write(
      root / "bad-texture.material.json",
      R"({"format_version":1,"textures":{"albedo":"textures/no-scheme.png"}})");
  write(
      root / "missing-stage.shader.json",
      R"({"format_version":1,"vertex":"missing.vert","fragment":"shader.frag"})");
  write(
      root / "bad-platform-stage.shader.json",
      R"({"format_version":1,"vertex":"shader.vert","fragment":"shader.frag","platform_sources":{"android":{"vertex":"shader_android.vert"}}})");
  write(root / "typed-wrong.target.json",
        R"({"format_version":1,"width":"wide","height":180})");
  write(root / "no-depth.target.json",
        R"({"format_version":1,"width":16,"height":16,"depth":false})");
  write(root / "invalid.json", "{");
  write(root / "missing-reference.material.json",
        R"({"format_version":1,"shader":"asset://shaders/missing"})");
  write(
      root / "missing-fallback.shader.json",
      R"({"format_version":1,"vertex":"shader.vert","fragment":"shader.frag","platform_fallbacks":{"linux":"asset://shaders/missing"}})");

  Diagnostics invalidDiagnostics;
  const auto material = assets::loadMaterialAsset(root / "valid.material.json");
  const auto invalid = assets::loadMaterialAsset(root / "bad.material.json",
                                                 &invalidDiagnostics);
  const auto shader = assets::loadShaderAsset(root / "valid.shader.json");
  const auto shaderSourceFiles =
      assets::collectReferencedSourceFiles(root / "valid.shader.json");
  Diagnostics shaderDiagnostics;
  const auto invalidShader =
      assets::loadShaderAsset(root / "bad.shader.json", &shaderDiagnostics);
  const auto target = assets::loadRenderTargetAsset(root / "valid.target.json");
  Diagnostics targetDiagnostics;
  const auto invalidTarget = assets::loadRenderTargetAsset(
      root / "bad.target.json", &targetDiagnostics);
  Diagnostics malformedDiagnostics;
  const bool malformedAssetsRejected =
      !assets::loadMaterialAsset(root / "typed-wrong.material.json",
                                 &malformedDiagnostics) &&
      !assets::loadMaterialAsset(root / "bad-texture.material.json",
                                 &malformedDiagnostics) &&
      !assets::loadShaderAsset(root / "missing-stage.shader.json",
                               &malformedDiagnostics) &&
      !assets::loadShaderAsset(root / "bad-platform-stage.shader.json",
                               &malformedDiagnostics) &&
      !assets::loadRenderTargetAsset(root / "typed-wrong.target.json",
                                     &malformedDiagnostics) &&
      !assets::loadRenderTargetAsset(root / "no-depth.target.json",
                                     &malformedDiagnostics) &&
      !assets::loadMaterialAsset(root / "invalid.json",
                                 &malformedDiagnostics) &&
      !assets::loadMaterialAsset(root / "absent.material.json",
                                 &malformedDiagnostics);
  AssetRegistry referenceRegistry;
  referenceRegistry.assets = {
      {.id = "asset://materials/missing_shader",
       .type = "Material",
       .manifestPath = root / "missing-reference.material.asset.json",
       .sourcePath = root / "missing-reference.material.json",
       .sourcePaths = {root / "missing-reference.material.json"}},
      {.id = "asset://shaders/missing_fallback",
       .type = "Shader",
       .manifestPath = root / "missing-fallback.shader.asset.json",
       .sourcePath = root / "missing-fallback.shader.json",
       .sourcePaths = {root / "missing-fallback.shader.json",
                       root / "shader.vert", root / "shader.frag"}},
  };
  const Diagnostics referenceDiagnostics =
      validateAssetRegistry(referenceRegistry);
  const auto hasDiagnostic = [&](const std::string &code) {
    return std::ranges::any_of(referenceDiagnostics,
                               [&](const Diagnostic &diagnostic) {
                                 return diagnostic.code == code;
                               });
  };
  std::filesystem::remove_all(root);
  return material && material->textures.contains("albedo") &&
         material->numbers.contains("roughness") &&
         material->colors.contains("base_color") &&
         material->renderState.blend == "alpha" &&
         !material->renderState.depthWrite && !invalid &&
         !invalidDiagnostics.empty() && shader && !invalidShader &&
         !shaderDiagnostics.empty() && shader &&
         shader->stagesFor("linux").vertex.filename() == "shader.vert" &&
         shader->stagesFor("android").vertex.filename() ==
             "shader_android.vert" &&
         shader->fallbackFor("linux") == "builtin://lit" &&
         shaderSourceFiles.size() == 5 &&
         target && target->width == 320 &&
         target->height == 180 && !invalidTarget &&
         !targetDiagnostics.empty() && malformedAssetsRejected &&
         malformedDiagnostics.size() == 8 &&
         hasDiagnostic("MATERIAL_SHADER_NOT_FOUND") &&
         hasDiagnostic("SHADER_FALLBACK_NOT_FOUND");
}

bool cameraContracts() {
  World world;
  world.entities.push_back(camera("overlay", 100, false));
  world.entities.push_back(camera("gameplay", -10, true));
  world.entities.push_back(camera("disabled_primary", 999, true, false));
  const Entity *active = activeCamera3DEntity(world);
  const auto ordered = renderCameras3D(world);
  if (active == nullptr || active->id != "gameplay" || ordered.size() != 2 ||
      ordered.front()->id != "gameplay" || ordered.back()->id != "overlay")
    return false;

  World tie;
  tie.entities.push_back(camera("z_camera", 5, false));
  tie.entities.push_back(camera("a_camera", 5, false));
  const Entity *tieActive = activeCamera3DEntity(tie);
  if (tieActive == nullptr || tieActive->id != "a_camera" ||
      renderCameras3D(tie).front()->id != "a_camera")
    return false;

  World noUsableCamera;
  Entity missingTransform;
  missingTransform.id = "missing_transform";
  missingTransform.setComponent(Camera3DComponent{.primary = true});
  noUsableCamera.entities.push_back(std::move(missingTransform));
  noUsableCamera.entities.push_back(camera("disabled", 0, true, false));
  return activeCamera3DEntity(noUsableCamera) == nullptr &&
         renderCameras3D(noUsableCamera).empty();
}

bool cameraRenderCadenceContracts() {
  Entity parsedCamera;
  Camera3DComponent::parse(
      nlohmann::json{{"render_scale", 0.05}, {"update_interval", -4.0}},
      parsedCamera);
  const auto *parsed = parsedCamera.component<Camera3DComponent>();
  if (parsed == nullptr || parsed->renderScale != 0.25F ||
      parsed->updateInterval != 0.0F)
    return false; // malformed runtime budgets must clamp to safe values
  Entity supersampledCamera;
  Camera3DComponent::parse(nlohmann::json{{"render_scale", 9.0}},
                           supersampledCamera);
  if (supersampledCamera.component<Camera3DComponent>()->renderScale != 2.0F)
    return false;

  CameraRenderScheduler3D scheduler;

  scheduler.beginFrame();
  if (!scheduler.shouldRender("realtime", 0.0F, 0.0F) ||
      !scheduler.shouldRender("cached", 0.1F, 0.0F))
    return false; // both camera modes must render their first frame
  scheduler.endFrame();

  for (int frame = 0; frame < 5; ++frame) {
    scheduler.beginFrame();
    if (!scheduler.shouldRender("realtime", 0.0F, 0.016F) ||
        scheduler.shouldRender("cached", 0.1F, 0.016F))
      return false;
    scheduler.endFrame();
  }

  scheduler.beginFrame();
  if (!scheduler.shouldRender("cached", 0.1F, 0.021F) ||
      !scheduler.shouldRender("invalid_delta", 0.1F, -1.0F))
    return false; // accumulated cadence and negative-delta first frames
  scheduler.endFrame();

  scheduler.beginFrame();
  if (scheduler.shouldRender("invalid_delta", 0.1F, -1.0F) ||
      !scheduler.shouldRender("large_delta", 0.1F, 0.0F))
    return false; // invalid deltas cannot advance a cached camera
  scheduler.endFrame();

  scheduler.beginFrame();
  if (!scheduler.shouldRender("large_delta", 0.1F, 0.35F))
    return false; // long frames still produce only one refresh decision
  scheduler.endFrame();

  scheduler.beginFrame();
  scheduler.endFrame(); // prune cameras absent from a whole frame
  scheduler.beginFrame();
  const bool reappearingCameraRenders =
      scheduler.shouldRender("cached", 0.1F, 0.0F);
  scheduler.endFrame();
  return reappearingCameraRenders;
}

bool lightingContracts() {
  World world;
  Entity environment;
  environment.id = "environment";
  environment.setComponent(Environment3DComponent{.maxShadowLights = 1});
  world.entities.push_back(std::move(environment));
  for (int index = 0; index < 6; ++index) {
    Entity light;
    light.id = "light_" + std::to_string(index);
    light.setComponent(Transform3DComponent{});
    light.setComponent(PointLightComponent{
        .castsShadows = true,
        .renderMask = index == 0 ? "ui" : "world",
    });
    world.entities.push_back(std::move(light));
  }
  RenderStatistics statistics;
  const LightingFrame3D frame = collectLighting3D(world, "world", statistics);
  if (frame.lightCount != 4 || frame.shadowLightCount != 1 ||
      statistics.lights != 4 || statistics.shadowPasses != 1)
    return false;

  auto *environmentConfig =
      world.entities.front().component<Environment3DComponent>();
  environmentConfig->maxShadowLights = 0;
  RenderStatistics noShadowStatistics;
  const LightingFrame3D noShadows =
      collectLighting3D(world, "world", noShadowStatistics);
  if (noShadows.lightCount != 4 || noShadows.shadowLightCount != 0 ||
      noShadowStatistics.shadowPasses != 0)
    return false;

  for (Entity &entity : world.entities)
    if (entity.id != "environment")
      entity.enabled = false;
  RenderStatistics disabledStatistics;
  const LightingFrame3D disabled =
      collectLighting3D(world, "world", disabledStatistics);
  return disabled.lightCount == 0 && disabledStatistics.lights == 0;
}

bool particleContracts() {
  World world3D;
  world3D.entities.push_back(emitter3D("burst", 20, 4));
  ParticleSystem3D particles3D;
  particles3D.update(world3D, 0.0F);
  if (particles3D.particleCount() != 4)
    return false;
  particles3D.update(world3D, 0.0F);
  if (particles3D.particleCount() != 4)
    return false; // a second camera/frame-zero draw must not repeat the burst
  particles3D.update(world3D, -10.0F);
  if (particles3D.particleCount() != 4)
    return false; // invalid time input must not rewind or expire particles
  world3D.entities.front()
      .component<ParticleEmitter3DComponent>()
      ->maxParticles = 2;
  particles3D.update(world3D, 0.0F);
  if (particles3D.particleCount() != 2)
    return false; // lowering a live budget must release pooled particles
  particles3D.update(world3D, 1.1F);
  if (particles3D.particleCount() != 0)
    return false;
  world3D.entities.clear();
  particles3D.update(world3D, 0.0F);
  if (particles3D.particleCount() != 0)
    return false;

  World world2D;
  world2D.entities.push_back(emitter2D("burst", 8, 3, 0.25F));
  ParticleSystem2D particles2D;
  particles2D.update(world2D, 0.0F);
  if (particles2D.particleCount() != 3)
    return false;
  particles2D.update(world2D, 0.3F);
  if (particles2D.particleCount() != 0)
    return false;

  World oneShot;
  Entity oneShotEmitter = emitter3D("one_shot", 2, 8, 10.0F);
  auto *oneShotConfig = oneShotEmitter.component<ParticleEmitter3DComponent>();
  oneShotConfig->rate = 100.0F;
  oneShotConfig->loop = false;
  oneShot.entities.push_back(std::move(oneShotEmitter));
  ParticleSystem3D oneShotParticles;
  oneShotParticles.update(oneShot, 1.0F);
  if (oneShotParticles.particleCount() != 2)
    return false; // non-looping emitters emit their burst exactly once
  oneShotConfig =
      oneShot.entities.front().component<ParticleEmitter3DComponent>();
  oneShotConfig->playing = false;
  oneShotParticles.update(oneShot, 0.0F);
  oneShotConfig->playing = true;
  oneShotParticles.update(oneShot, 0.0F);
  if (oneShotParticles.particleCount() != 4)
    return false; // an explicit stop/start restarts a one-shot emitter

  oneShotConfig->maxParticles = -1;
  oneShotParticles.update(oneShot, 0.0F);
  if (oneShotParticles.particleCount() != 0)
    return false; // malformed runtime values cannot wrap the budget
  oneShot.entities.front().enabled = false;
  oneShotParticles.update(oneShot, 0.0F);
  return oneShotParticles.particleCount() == 0;
}

} // namespace

int main() {
  if (!assetContracts()) {
    std::cerr << "Material/shader asset validation contracts failed.\n";
    return 1;
  }
  if (!cameraContracts()) {
    std::cerr << "Primary camera and render ordering contracts failed.\n";
    return 1;
  }
  if (!cameraRenderCadenceContracts()) {
    std::cerr << "Camera render cadence contracts failed.\n";
    return 1;
  }
  if (!lightingContracts()) {
    std::cerr << "Light mask, budget, or shadow budget contracts failed.\n";
    return 1;
  }
  if (!particleContracts()) {
    std::cerr << "Particle pooling, burst, budget, or expiry failed.\n";
    return 1;
  }
  return 0;
}
