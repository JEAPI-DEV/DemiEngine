#include "demi/runtime/audio/AudioSceneSystem.h"

#include "demi/runtime/audio/AudioSystem.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/components/media/AudioListenerComponent.h"
#include "demi/runtime/scene/components/media/AudioSourceComponent.h"

namespace demi::runtime {
namespace {

AudioVector3 positionOf(const World &world, const Entity &entity) {
  if (entity.hasComponent<Transform3DComponent>()) {
    const Vec3 value = worldPosition3D(world, entity);
    return {value.x, value.y, value.z};
  }
  if (const auto *transform = entity.component<Transform2DComponent>())
    return {transform->position.x, transform->position.y, 0.0F};
  return {};
}

AudioPlaybackRequest requestFor(const World &world, const Entity &entity,
                                const AudioSourceComponent &source) {
  return {.assetId = source.clip,
          .bus = source.bus,
          .concurrencyGroup = source.concurrencyGroup,
          .loop = source.loop,
          .streaming = source.streaming,
          .volume = source.volume,
          .pitch = source.pitch,
          .pan = source.pan,
          .spatialMode = source.spatialMode,
          .attenuation = source.attenuation,
          .position = positionOf(world, entity),
          .velocity = {},
          .minDistance = source.minDistance,
          .maxDistance = source.maxDistance,
          .rolloff = source.rolloff,
          .doppler = source.doppler,
          .fadeInSeconds = source.fadeIn,
          .maxVoices = source.maxVoices,
          .voiceStealing = source.voiceStealing,
          .pauseWithGame = source.pauseWithGame};
}

} // namespace

void AudioSceneSystem::update(World &world, AudioSystem &audio) const {
  bool listenerSet = false;
  for (Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    if (!listenerSet) {
      if (const auto *listener = entity.component<AudioListenerComponent>();
          listener != nullptr && listener->primary) {
        audio.setListener({.position = positionOf(world, entity),
                           .velocity = {}});
        listenerSet = true;
      }
    }
    auto *source = entity.component<AudioSourceComponent>();
    if (source == nullptr || source->handle == 0)
      continue;
    if (!audio.isPlaying(source->handle)) {
      source->handle = 0;
      continue;
    }
    (void)audio.configure(source->handle, requestFor(world, entity, *source));
  }
}

} // namespace demi::runtime
