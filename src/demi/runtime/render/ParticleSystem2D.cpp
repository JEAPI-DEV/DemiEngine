#include "demi/runtime/render/ParticleSystem2D.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/ParticleEmitter2DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/render/RaylibMaterialBinding.h"

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

Vec2 randomVector(std::uint32_t &state, const Vec2 minimum,
                  const Vec2 maximum) {
  return {randomRange(state, minimum.x, maximum.x),
          randomRange(state, minimum.y, maximum.y)};
}

Vec2 emissionOffset(const ParticleEmitter2DComponent &emitter,
                    std::uint32_t &state) {
  if (emitter.emissionShape == "box")
    return randomVector(
        state, {-emitter.emissionSize.x * 0.5F, -emitter.emissionSize.y * 0.5F},
        {emitter.emissionSize.x * 0.5F, emitter.emissionSize.y * 0.5F});
  if (emitter.emissionShape == "circle") {
    const float radius = std::sqrt(randomUnit(state));
    const float angle = randomUnit(state) * 6.283185307F;
    return {std::cos(angle) * radius * emitter.emissionSize.x,
            std::sin(angle) * radius * emitter.emissionSize.y};
  }
  return {};
}

::Color toRlColor(const Color from, const Color to, const float t) {
  const auto channel = [t](const float a, const float b) {
    return static_cast<unsigned char>(
        std::round(std::clamp(a + (b - a) * t, 0.0F, 1.0F) * 255.0F));
  };
  return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b),
          channel(from.a, to.a)};
}

} // namespace

void ParticleSystem2D::update(const World &world, const float deltaTime) {
  const float step = std::max(deltaTime, 0.0F);
  std::unordered_set<std::string> liveEmitters;
  for (const Entity &entity : world.entities) {
    const auto *emitter = entity.component<ParticleEmitter2DComponent>();
    if (!entity.enabled || emitter == nullptr ||
        !entity.hasComponent<Transform2DComponent>())
      continue;

    liveEmitters.insert(entity.id);
    auto [iterator, inserted] =
        emitters_.try_emplace(entity.id, EmitterState{});
    EmitterState &state = iterator->second;
    if (inserted)
      state.randomState = emitter->seed == 0 ? 1U : emitter->seed;
    state.texture = emitter->texture;
    state.material = emitter->material;
    state.sortingOrder = emitter->sortingOrder;
#if defined(__ANDROID__)
    const std::size_t budget =
        static_cast<std::size_t>(std::max(emitter->mobileMaxParticles, 0));
#else
    const std::size_t budget =
        static_cast<std::size_t>(std::max(emitter->maxParticles, 0));
#endif
    if (state.particles.size() > budget)
      state.particles.resize(budget);

    int spawnCount = 0;
    if (emitter->playing) {
      if (emitter->loop) {
        state.emissionRemainder += std::max(emitter->rate, 0.0F) * step;
        spawnCount = static_cast<int>(state.emissionRemainder);
        state.emissionRemainder -= static_cast<float>(spawnCount);
      }
      if (!state.burstEmitted) {
        spawnCount += emitter->burst;
        state.burstEmitted = true;
      }
    } else {
      state.burstEmitted = false;
      state.emissionRemainder = 0.0F;
    }
    spawnCount = std::min(spawnCount,
                          static_cast<int>(budget > state.particles.size()
                                               ? budget - state.particles.size()
                                               : 0U));
    const Vec2 origin = worldPosition2D(world, entity);
    for (int index = 0; index < spawnCount; ++index) {
      const Vec2 offset = emissionOffset(*emitter, state.randomState);
      state.particles.push_back(
          {.position = {origin.x + offset.x, origin.y + offset.y},
           .velocity = randomVector(state.randomState, emitter->velocityMin,
                                    emitter->velocityMax),
           .gravity = emitter->gravity,
           .colorStart = emitter->colorStart,
           .colorEnd = emitter->colorEnd,
           .lifetime = emitter->lifetime,
           .sizeStart = emitter->sizeStart,
           .sizeEnd = emitter->sizeEnd,
           .rotation = randomRange(state.randomState, 0.0F, 360.0F),
           .rotationSpeed = emitter->rotationSpeed});
    }
    for (Particle &particle : state.particles) {
      particle.age += step;
      particle.velocity.x += particle.gravity.x * step;
      particle.velocity.y += particle.gravity.y * step;
      particle.position.x += particle.velocity.x * step;
      particle.position.y += particle.velocity.y * step;
      particle.rotation += particle.rotationSpeed * step;
    }
    std::erase_if(state.particles, [](const Particle &particle) {
      return particle.age >= particle.lifetime;
    });
  }
  std::erase_if(emitters_, [&](const auto &entry) {
    return !liveEmitters.contains(entry.first);
  });
}

void ParticleSystem2D::draw(
    const std::unordered_map<std::string, Texture2D> &textures,
    const std::unordered_map<std::string, assets::MaterialAsset> &materials,
    const ShaderResourceLibrary &shaders,
    const Vec2 cameraPosition, const float pixelsPerUnit, const int width,
    const int height) {
  std::vector<const EmitterState *> ordered;
  for (const auto &[id, state] : emitters_) {
    (void)id;
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
    const auto texture = textures.find(textureId);
    const ScopedRaylibMaterial2D materialScope(shaders, material);
    for (const Particle &particle : state->particles) {
      const float t = std::clamp(particle.age / particle.lifetime, 0.0F, 1.0F);
      const float size =
          (particle.sizeStart + (particle.sizeEnd - particle.sizeStart) * t) *
          pixelsPerUnit;
      const ::Vector2 center{
          static_cast<float>(width) * 0.5F +
              (particle.position.x - cameraPosition.x) * pixelsPerUnit,
          static_cast<float>(height) * 0.5F -
              (particle.position.y - cameraPosition.y) * pixelsPerUnit};
      const ::Color color =
          toRlColor(particle.colorStart, particle.colorEnd, t);
      if (texture != textures.end())
        DrawTexturePro(texture->second,
                       {0.0F, 0.0F, static_cast<float>(texture->second.width),
                        static_cast<float>(texture->second.height)},
                       {center.x, center.y, size, size},
                       {size * 0.5F, size * 0.5F}, particle.rotation, color);
      else
        DrawCircleV(center, size * 0.5F, color);
    }
  }
}

std::size_t ParticleSystem2D::particleCount() const {
  std::size_t count = 0;
  for (const auto &[id, state] : emitters_) {
    (void)id;
    count += state.particles.size();
  }
  return count;
}

std::vector<render::ParticleRenderData2D> ParticleSystem2D::renderData() const {
  std::vector<render::ParticleRenderData2D> result;
  result.reserve(particleCount());
  for (const auto &[id, state] : emitters_) {
    (void)id;
    for (const Particle &particle : state.particles) {
      const float progress =
          std::clamp(particle.age / std::max(particle.lifetime, 0.0001F),
                     0.0F, 1.0F);
      const auto interpolate = [progress](const float start,
                                          const float end) {
        return start + (end - start) * progress;
      };
      result.push_back({
          .position = particle.position,
          .size = interpolate(particle.sizeStart, particle.sizeEnd),
          .rotationRadians = particle.rotation * 0.017453292519943295F,
          .color =
              {
                  interpolate(particle.colorStart.r, particle.colorEnd.r),
                  interpolate(particle.colorStart.g, particle.colorEnd.g),
                  interpolate(particle.colorStart.b, particle.colorEnd.b),
                  interpolate(particle.colorStart.a, particle.colorEnd.a),
              },
          .texture = state.texture,
          .material = state.material,
          .sortingOrder = state.sortingOrder,
      });
    }
  }
  return result;
}

void ParticleSystem2D::clear() { emitters_.clear(); }

} // namespace demi::runtime
