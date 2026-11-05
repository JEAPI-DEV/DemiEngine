#pragma once
#include "demi/runtime/physics/Physics2D.h"
#include <memory>
namespace demi::runtime {
// Operation-scoped native shape snapshot for rays from one origin. New batches
// observe authored poses, hierarchy, disabled colliders and membership
// immediately.
class NativeQueryShapes2D;
class RadialRaycast2D {
public:
  RadialRaycast2D(const World &world, Vec2 origin, float radius,
                  const std::string &layer,
                  const std::string &ignoredEntityId = {});
  ~RadialRaycast2D();
  // Candidate silhouette directions for visibility fans. Shape intersections
  // still go through cast(); these angles only prevent missing thin blockers.
  [[nodiscard]] std::vector<float> silhouetteAngles() const;
  [[nodiscard]] std::optional<PhysicsRaycastHit2D> cast(Vec2 direction) const;
  [[nodiscard]] std::optional<PhysicsRaycastHit2D> cast(Vec2 direction,
                                                        float distance) const;

private:
  Vec2 origin_;
  float radius_;
  std::unique_ptr<NativeQueryShapes2D> candidates_;
};
} // namespace demi::runtime
