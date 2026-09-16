#pragma once

#include "demi/runtime/destruction/BlastFamily3D.h"

namespace demi::runtime {
struct ColliderAsset3D;

// Builds an independent family from a decoded immutable asset snapshot.
// Derives local-space centroids/volumes from native convex hulls, never bounds.
// No world/body mutation. Returns null and an error for missing/invalid data.
[[nodiscard]] std::unique_ptr<BlastFamily3D>
createColliderFractureFamily3D(const ColliderAsset3D &collider,
                               std::string &error);
} // namespace demi::runtime
