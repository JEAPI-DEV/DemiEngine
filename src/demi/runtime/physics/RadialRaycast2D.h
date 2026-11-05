#pragma once
#include "demi/runtime/physics/Physics2D.h"
namespace demi::runtime {
// Call-scoped broad phase for rays from one origin. The world must remain
// unchanged (poses, layers, membership) until the batch is destroyed. Do not
// cache across simulation steps: new batches observe moved/disabled colliders
// immediately.
class RadialRaycast2D {
public:
  RadialRaycast2D(const World &world, Vec2 origin, float radius,
                  const std::string &layer,
                  const std::string &ignoredEntityId = {});
  // Candidate silhouette directions for visibility fans. Shape intersections
  // still go through cast(); these angles only prevent missing thin blockers.
  [[nodiscard]] std::vector<float> silhouetteAngles() const;
  [[nodiscard]] std::optional<PhysicsRaycastHit2D> cast(Vec2 direction) const;
  [[nodiscard]] std::optional<PhysicsRaycastHit2D> cast(Vec2 direction,
                                                        float distance) const;

private:
  Vec2 origin_;
  float radius_;
  std::string layer_;
  std::string ignoredEntityId_;
  std::vector<const Entity *> candidates_;
};
} // namespace demi::runtime
