#include "demi/runtime/physics/ColliderAssetLoader3D.h"
#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include <algorithm>

namespace demi::runtime {
namespace {
class ColliderAssetLoader3D final : public assets::AssetResourceLoader {
public:
  explicit ColliderAssetLoader3D(World &world) : world_(world) {}
  bool supports(const AssetManifest &asset) const override {
    return asset.type == "Collider3D";
  }
  std::optional<assets::DecodedAsset>
  readAndDecode(const AssetManifest &asset, const std::atomic_bool &cancelled,
                std::string &error) override {
    if (cancelled.load()) {
      error = "Collider load cancelled.";
      return std::nullopt;
    }
    auto collider = loadColliderAsset3D(asset, error);
    if (!collider)
      return std::nullopt;
    auto bytes = sizeof(ColliderAsset3D) +
                       collider->points.size() * sizeof(Vec3) +
                       collider->triangles.size() * sizeof(TriangleCollider3D);
    bytes += collider->parts.size() * sizeof(ColliderPart3D);
    for (const auto &part : collider->parts)
      bytes += part.id.size() + part.points.size() * sizeof(Vec3);
    if (collider->fracture) {
      bytes += collider->fracture->bonds.size() *
               sizeof(assets::ColliderFractureBond);
      for (const auto &bond : collider->fracture->bonds)
        bytes += bond.id.size() + bond.firstPart.size() + bond.secondPart.size();
      bytes += collider->fracture->anchors.size() * sizeof(std::string);
      for (const auto &anchor : collider->fracture->anchors)
        bytes += anchor.size();
    }
    return assets::DecodedAsset{
        .payload = std::make_shared<ColliderAsset3D>(std::move(*collider)),
        .decodedBytes = bytes,
        .residentBytes = bytes};
  }
  bool upload(const AssetManifest &asset, const assets::DecodedAsset &decoded,
              std::string &error) override {
    if (!decoded.payload) {
      error = "Empty collider payload.";
      return false;
    }
    world_.colliderAssets3D.insert_or_assign(
        asset.id, *std::static_pointer_cast<ColliderAsset3D>(decoded.payload));
    error.clear();
    return true;
  }
  void unload(std::string_view id) override {
    const auto found = world_.colliderAssets3D.find(std::string(id));
    if (found != world_.colliderAssets3D.end()) {
      const bool used =
          std::ranges::any_of(world_.entities, [&](const Entity &entity) {
            const auto *collider = entity.component<ModelCollider3DComponent>();
            return entity.enabled && collider && collider->asset == id;
          });
      if (!used) {
        world_.colliderAssets3D.erase(found);
        return;
      }
      // Live physics users retain their immutable shape snapshot. The physics
      // step retires it once no enabled collider uses it; never drop collision
      // underneath a live rigid body when a script releases an asset request.
      found->second.resident = false;
    }
  }
  std::string_view backendName() const override {
    return "physics-collider-3d";
  }

private:
  World &world_;
};
} // namespace
std::shared_ptr<assets::AssetResourceLoader>
createColliderAssetLoader3D(World &world) {
  return std::make_shared<ColliderAssetLoader3D>(world);
}
} // namespace demi::runtime
