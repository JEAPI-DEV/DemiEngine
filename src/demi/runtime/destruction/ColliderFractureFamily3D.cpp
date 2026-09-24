#include "demi/runtime/destruction/ColliderFractureFamily3D.h"
#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/physics/JoltLifetime.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace demi::runtime {
std::unique_ptr<BlastFamily3D>
createColliderFractureFamily3D(const ColliderAsset3D &collider,
                               std::string &error) {
  error.clear();
  try {
    if (!collider.fracture || collider.parts.empty() ||
        collider.parts.size() >= UINT32_MAX || collider.fracture->bonds.size() >= UINT32_MAX ||
        collider.fracture->anchors.size() > collider.parts.size())
      throw std::invalid_argument(
          "Collider requires an authored compound fracture graph");
    std::set<std::string> partIds;
    for (const auto &part : collider.parts)
      partIds.insert(part.id);
    std::set<std::string> anchors;
    for (const auto &anchor : collider.fracture->anchors)
      if (!partIds.contains(anchor) || !anchors.insert(anchor).second)
        throw std::invalid_argument(
            "Fracture anchors must reference unique existing parts");
    const JoltLifetime lifetime;
    std::vector<DestructionChunk3D> chunks;
    for (const auto &part : collider.parts) {
      if (part.points.size() < 4 || part.points.size() > 256)
        throw std::invalid_argument(
            "Fracture part requires 4..256 hull points");
      JPH::Array<JPH::Vec3> points;
      for (const auto &point : part.points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z))
          throw std::invalid_argument(
              "Fracture hull contains non-finite coordinates");
        points.emplace_back(point.x, point.y, point.z);
      }
      const auto shape = JPH::ConvexHullShapeSettings(points).Create();
      if (shape.HasError())
        throw std::runtime_error("Fracture hull " + part.id + ": " +
                                 shape.GetError().c_str());
      const auto center = shape.Get()->GetCenterOfMass();
      chunks.push_back(
          {part.id,
           {center.GetX(), center.GetY(), center.GetZ()},
           shape.Get()->GetVolume(),
           std::ranges::find(collider.fracture->anchors, part.id) !=
               collider.fracture->anchors.end()});
    }
    std::vector<DestructionBond3D> bonds;
    for (const auto &bond : collider.fracture->bonds)
      bonds.push_back({bond.id, bond.firstPart, bond.secondPart, bond.health});
    return std::make_unique<BlastFamily3D>(chunks, bonds);
  } catch (const std::exception &exception) {
    error = exception.what();
    return {};
  }
}
} // namespace demi::runtime
