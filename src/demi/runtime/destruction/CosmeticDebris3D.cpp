#include "demi/runtime/destruction/CosmeticDebris3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Destructible3DComponent.h"
#include <algorithm>
#include <cmath>

namespace demi::runtime {
float CosmeticFragment3D::opacity() const {
  if (age >= lifetime)
    return 0;
  return color.a *
         (fadeDuration > 0
              ? std::clamp((lifetime - age) / std::min(fadeDuration, lifetime),
                           0.F, 1.F)
              : 1.F);
}
void CosmeticDebris3D::emit(const std::string &owner, std::uint64_t attachment,
                            const FractureDebris3DComponent &c, Vec3 position,
                            Vec3 direction) {
  if (c.count <= 0 || c.maxFragments <= 0 || c.lifetime <= 0)
    return;
  auto &group = groups_[owner];
  if (group.attachment != attachment) {
    group = {};
    group.attachment = attachment;
    group.random = c.seed;
  }
  const auto random = [&]() {
    group.random = group.random * 1664525U + 1013904223U;
    return float(group.random >> 8) * (1.F / 16777216.F);
  };
  const auto signedRandom = [&]() { return random() * 2.F - 1.F; };
  const auto count =
      static_cast<std::size_t>(std::min(c.count, c.maxFragments));
  const auto retained = static_cast<std::size_t>(c.maxFragments) - count;
  auto &items = group.fragments;
  if (items.size() > retained)
    items.erase(items.begin(), items.end() - retained);
  items.reserve(items.size() + count);
  for (std::size_t index = 0; index < count; ++index) {
    const float scale = .5F + random();
    Vec3 velocity{direction.x + signedRandom(), direction.y + signedRandom(),
                  direction.z + signedRandom()};
    const float length =
        std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y +
                  velocity.z * velocity.z);
    const float speed = c.speed * (.5F + random()) / std::max(length, .001F);
    items.push_back(
        {.position = position,
         .rotation = {random() * 360, random() * 360, random() * 360},
         .size = {c.size.x * scale, c.size.y * scale, c.size.z * scale},
         .velocity = {velocity.x * speed, velocity.y * speed,
                      velocity.z * speed},
         .angularVelocity = {signedRandom() * c.spin, signedRandom() * c.spin,
                             signedRandom() * c.spin},
         .gravity = c.gravity,
         .color = c.color,
         .model = c.model,
         .texture = c.texture,
         .renderLayer = c.renderLayer,
         .lifetime = c.lifetime,
         .fadeDuration = c.fadeDuration});
  }
}
void CosmeticDebris3D::update(const World &world, float dt) {
  if (!std::isfinite(dt) || dt < 0)
    return;
  std::erase_if(groups_, [&](auto &entry) {
    const Entity *entity = findEntity(world, entry.first);
    const auto *owner =
        entity ? entity->component<Destructible3DComponent>() : nullptr;
    return !entity || !entity->enabled || !owner ||
           owner->attachmentKey != entry.second.attachment;
  });
  for (auto &[owner, group] : groups_) {
    for (auto &f : group.fragments) {
      f.age += dt;
      f.position = {
          f.position.x + f.velocity.x * dt + .5F * f.gravity.x * dt * dt,
          f.position.y + f.velocity.y * dt + .5F * f.gravity.y * dt * dt,
          f.position.z + f.velocity.z * dt + .5F * f.gravity.z * dt * dt};
      f.velocity = {f.velocity.x + f.gravity.x * dt,
                    f.velocity.y + f.gravity.y * dt,
                    f.velocity.z + f.gravity.z * dt};
      f.rotation = {std::fmod(f.rotation.x + f.angularVelocity.x * dt, 360.F),
                    std::fmod(f.rotation.y + f.angularVelocity.y * dt, 360.F),
                    std::fmod(f.rotation.z + f.angularVelocity.z * dt, 360.F)};
    }
    std::erase_if(group.fragments,
                  [](const auto &f) { return f.age >= f.lifetime; });
  }
  std::erase_if(groups_, [](const auto &entry) {
    return entry.second.fragments.empty();
  });
}
std::vector<CosmeticFragment3D> CosmeticDebris3D::snapshot() const {
  std::vector<CosmeticFragment3D> result;
  for (const auto &[owner, group] : groups_)
    result.insert(result.end(), group.fragments.begin(), group.fragments.end());
  return result;
}
} // namespace demi::runtime
