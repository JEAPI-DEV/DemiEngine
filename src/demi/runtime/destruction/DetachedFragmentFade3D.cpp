#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace demi::runtime {
void updateDetachedFragmentFades3D(World &world, float dt, Vec3 gravity) {
  std::map<std::string, float> opacity;
  std::set<std::string> expired;
  for (auto &e : world.entities)
    if (auto *f = e.component<DetachedFragmentFade3D>()) {
      auto *t = e.component<Transform3DComponent>();
      f->age += dt;
      if (!t || f->age >= f->lifetime) {
        expired.insert(e.id);
        continue;
      }
      f->center = {f->center.x + f->velocity.x * dt + .5F * gravity.x * dt * dt,
                   f->center.y + f->velocity.y * dt + .5F * gravity.y * dt * dt,
                   f->center.z + f->velocity.z * dt +
                       .5F * gravity.z * dt * dt};
      f->velocity = {f->velocity.x + gravity.x * dt,
                     f->velocity.y + gravity.y * dt,
                     f->velocity.z + gravity.z * dt};
      const auto a = f->angularVelocity;
      const float speed = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
      if (speed > 0)
        t->rotation = rotateWorldEuler3D(
            t->rotation, {a.x / speed, a.y / speed, a.z / speed}, speed * dt);
      const auto offset =
          transformPoint3D({{}, t->rotation, t->scale}, f->localCenter);
      t->position = {f->center.x - offset.x, f->center.y - offset.y,
                     f->center.z - offset.z};
      opacity[e.id] = f->fade > 0
                          ? std::clamp((f->lifetime - f->age) /
                                           std::min(f->lifetime, f->fade),
                                       0.F, 1.F)
                          : 1.F;
    }
  for (auto &e : world.entities)
    if (auto *f = e.component<FragmentOpacity3D>()) {
      if (auto it = opacity.find(f->body); it != opacity.end())
        f->value = it->second;
      else
        expired.insert(e.id);
    }
  std::erase_if(world.entities,
                [&](const Entity &e) { return expired.contains(e.id); });
}
} // namespace demi::runtime
