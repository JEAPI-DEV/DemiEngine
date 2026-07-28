#include "demi/runtime/render/ParticleSystem3D.h"

#include "demi/runtime/render/Renderer3DInternal.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace demi::runtime {
namespace {

float randomUnit(std::uint32_t &state) {
  state = state * 1664525U + 1013904223U;
  return static_cast<float>(state >> 8U) / 16777215.0F;
}

float randomRange(std::uint32_t &state, const float minimum,
                  const float maximum) {
  return minimum + (maximum - minimum) * randomUnit(state);
}

Vec3 randomVector(std::uint32_t &state, const Vec3 minimum,
                  const Vec3 maximum) {
  return {randomRange(state, minimum.x, maximum.x),
          randomRange(state, minimum.y, maximum.y),
          randomRange(state, minimum.z, maximum.z)};
}

Color interpolate(const Color from, const Color to, const float t) {
  return {from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t,
          from.b + (to.b - from.b) * t, from.a + (to.a - from.a) * t};
}

Vec3 emissionOffset(const ParticleEmitter3DComponent &emitter,
                    std::uint32_t &state) {
  if (emitter.emissionShape == "box")
    return randomVector(
        state,
        {-emitter.emissionSize.x * 0.5F, -emitter.emissionSize.y * 0.5F,
         -emitter.emissionSize.z * 0.5F},
        {emitter.emissionSize.x * 0.5F, emitter.emissionSize.y * 0.5F,
         emitter.emissionSize.z * 0.5F});
  if (emitter.emissionShape == "sphere") {
    Vec3 direction =
        randomVector(state, {-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F});
    const float length =
        std::sqrt(direction.x * direction.x + direction.y * direction.y +
                  direction.z * direction.z);
    if (length > 0.0001F) {
      const float radius = randomUnit(state);
      direction = {direction.x / length * radius * emitter.emissionSize.x,
                   direction.y / length * radius * emitter.emissionSize.y,
                   direction.z / length * radius * emitter.emissionSize.z};
    }
    return direction;
  }
  if (emitter.emissionShape == "cone") {
    const float radius = randomUnit(state) * emitter.emissionSize.x;
    const float angle = randomUnit(state) * 6.283185307F;
    return {std::cos(angle) * radius, 0.0F, std::sin(angle) * radius};
  }
  return {};
}

} // namespace

void ParticleSystem3D::update(const World &world, const float deltaTime) {
  const float step = std::max(deltaTime, 0.0F);
  std::unordered_set<std::string> liveEmitters;
  for (const Entity &entity : world.entities) {
    if (entity.enabled && entity.hasComponent<ParticleEmitter3DComponent>() &&
        entity.hasComponent<Transform3DComponent>()) {
      liveEmitters.insert(entity.id);
      const auto &emitter = *entity.component<ParticleEmitter3DComponent>();
      auto [found, inserted] = emitters_.try_emplace(entity.id, EmitterState{});
      EmitterState &state = found->second;
      if (inserted)
        state.randomState = emitter.seed == 0 ? 1U : emitter.seed;
      state.renderMask = emitter.renderMask;
      state.texture = emitter.texture;
      state.material = emitter.material;
      state.simulationSpace = emitter.simulationSpace;
      state.sortingOrder = emitter.sortingOrder;

#if defined(__ANDROID__)
      const std::size_t budget =
          static_cast<std::size_t>(std::max(emitter.mobileMaxParticles, 0));
#else
      const std::size_t budget =
          static_cast<std::size_t>(std::max(emitter.maxParticles, 0));
#endif
      if (state.particles.size() > budget)
        state.particles.resize(budget);
      const auto transform = resolveWorldTransform3D(world, entity);
      const Vec3 origin = transform ? transform->position : Vec3{};
      state.origin = origin;

      int spawnCount = 0;
      if (emitter.playing) {
        if (emitter.loop) {
          state.emissionRemainder += std::max(emitter.rate, 0.0F) * step;
          spawnCount = static_cast<int>(state.emissionRemainder);
          state.emissionRemainder -= static_cast<float>(spawnCount);
        }
        if (!state.burstEmitted) {
          spawnCount += emitter.burst;
          state.burstEmitted = true;
        }
      } else {
        state.burstEmitted = false;
        state.emissionRemainder = 0.0F;
      }
      spawnCount = std::min(
          spawnCount, static_cast<int>(budget > state.particles.size()
                                           ? budget - state.particles.size()
                                           : 0U));
      for (int index = 0; index < spawnCount; ++index) {
        const Vec3 offset = emissionOffset(emitter, state.randomState);
        state.particles.push_back(
            {.position = emitter.simulationSpace == "local"
                             ? offset
                             : Vec3{origin.x + offset.x, origin.y + offset.y,
                                    origin.z + offset.z},
             .velocity = randomVector(state.randomState, emitter.velocityMin,
                                      emitter.velocityMax),
             .gravity = emitter.gravity,
             .colorStart = emitter.colorStart,
             .colorEnd = emitter.colorEnd,
             .age = 0.0F,
             .lifetime = emitter.lifetime,
             .sizeStart = emitter.sizeStart,
             .sizeEnd = emitter.sizeEnd,
             .rotation = randomRange(state.randomState, 0.0F, 360.0F),
             .rotationSpeed = emitter.rotationSpeed});
      }

      for (Particle &particle : state.particles) {
        particle.age += step;
        particle.velocity.x += particle.gravity.x * step;
        particle.velocity.y += particle.gravity.y * step;
        particle.velocity.z += particle.gravity.z * step;
        particle.position.x += particle.velocity.x * step;
        particle.position.y += particle.velocity.y * step;
        particle.position.z += particle.velocity.z * step;
        particle.rotation += particle.rotationSpeed * step;
      }
      std::erase_if(state.particles, [](const Particle &particle) {
        return particle.age >= particle.lifetime;
      });
    }
  }

  std::erase_if(emitters_, [&](const auto &entry) {
    return !liveEmitters.contains(entry.first);
  });
}

void ParticleSystem3D::draw(
    const World &, const ::Camera3D &camera,
    const std::unordered_map<std::string, Texture2D> &textures,
    const std::unordered_map<std::string, assets::MaterialAsset> &materials,
    const std::string &renderMask, RenderStatistics &statistics) {
  std::vector<const EmitterState *> ordered;
  for (const auto &[id, state] : emitters_) {
    (void)id;
    if (renderMask.empty() || state.renderMask.empty() ||
        renderMask == state.renderMask)
      ordered.push_back(&state);
  }
  std::ranges::stable_sort(
      ordered, [](const EmitterState *left, const EmitterState *right) {
        return left->sortingOrder < right->sortingOrder;
      });
  for (const EmitterState *state : ordered) {
    const assets::MaterialAsset *material = nullptr;
    if (const auto found = materials.find(state->material);
        found != materials.end())
      material = &found->second;
    std::string textureId = state->texture;
    if (textureId.empty() && material != nullptr)
      if (const auto found = material->textures.find("albedo");
          found != material->textures.end())
        textureId = found->second;
    const Texture2D *texture = nullptr;
    if (const auto found = textures.find(textureId); found != textures.end())
      texture = &found->second;
    if (material != nullptr && material->renderState.blend == "additive")
      BeginBlendMode(BLEND_ADDITIVE);
    for (const Particle &particle : state->particles) {
      const float t = std::clamp(particle.age / particle.lifetime, 0.0F, 1.0F);
      const float size =
          particle.sizeStart + (particle.sizeEnd - particle.sizeStart) * t;
      const ::Color color = renderer3d_detail::toRlColor(
          interpolate(particle.colorStart, particle.colorEnd, t));
      Vec3 position = particle.position;
      if (state->simulationSpace == "local") {
        position.x += state->origin.x;
        position.y += state->origin.y;
        position.z += state->origin.z;
      }
      if (texture != nullptr)
        DrawBillboardPro(camera, *texture,
                         {0.0F, 0.0F, static_cast<float>(texture->width),
                          static_cast<float>(texture->height)},
                         renderer3d_detail::toRlVec3(position),
                         {0.0F, 1.0F, 0.0F}, {size, size},
                         {size * 0.5F, size * 0.5F}, particle.rotation, color);
      else
        DrawSphere(renderer3d_detail::toRlVec3(position), size * 0.5F, color);
      ++statistics.particles;
    }
    if (material != nullptr && material->renderState.blend == "additive")
      EndBlendMode();
  }
}

std::size_t ParticleSystem3D::particleCount() const {
  std::size_t count = 0;
  for (const auto &[id, emitter] : emitters_) {
    (void)id;
    count += emitter.particles.size();
  }
  return count;
}

void ParticleSystem3D::clear() { emitters_.clear(); }

} // namespace demi::runtime
