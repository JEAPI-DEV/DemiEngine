#include "demi/runtime/physics/ColliderAsset3D.h"

#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace demi::runtime {
namespace {

std::optional<Vec3> vec3(const nlohmann::json &document, const char *key) {
  const auto value = document.find(key);
  if (value == document.end() || !value->is_array() || value->size() != 3)
    return std::nullopt;
  return Vec3{.x = (*value)[0].get<float>(),
              .y = (*value)[1].get<float>(),
              .z = (*value)[2].get<float>()};
}

std::optional<ColliderAsset3D> loadColliderAsset3D(const AssetManifest &asset,
                                                   std::string &error) {
  if (asset.type != "Collider3D") {
    error = "Asset is not a Collider3D asset: " + asset.id;
    return std::nullopt;
  }
  try {
    std::ifstream input(asset.manifestPath);
    const nlohmann::json document = nlohmann::json::parse(input);
    const auto size = vec3(document, "size");
    const auto offset = vec3(document, "offset").value_or(Vec3{});
    if (document.value("format_version", 0) != 1 ||
        document.value("shape", "") != "box" || !size || size->x <= 0.0F ||
        size->y <= 0.0F || size->z <= 0.0F) {
      error = "Collider asset must define format_version 1 and a positive "
              "box size: " +
              asset.manifestPath.string();
      return std::nullopt;
    }
    return ColliderAsset3D{.size = *size, .offset = offset};
  } catch (const nlohmann::json::exception &exception) {
    error = "Could not parse collider asset " + asset.manifestPath.string() +
            ": " + exception.what();
    return std::nullopt;
  }
}

} // namespace

bool resolveColliderAssets3D(World &world, const AssetRegistry &registry,
                             std::string &error) {
  world.colliderAssets3D.clear();
  for (const Entity &entity : world.entities) {
    const auto *collider = entity.component<ModelCollider3DComponent>();
    if (collider == nullptr)
      continue;
    const AssetManifest *asset = findAsset(registry, collider->asset);
    if (asset == nullptr) {
      error = "ModelCollider3D on " + entity.id +
              " references a missing asset: " + collider->asset;
      return false;
    }
    const auto loaded = loadColliderAsset3D(*asset, error);
    if (!loaded)
      return false;
    world.colliderAssets3D.insert_or_assign(collider->asset, *loaded);
  }
  return true;
}

std::optional<BoxColliderShape3D> resolvedBoxCollider3D(const World &world,
                                                        const Entity &entity) {
  if (const auto *box = entity.component<BoxCollider3DComponent>()) {
    return BoxColliderShape3D{
        .size = box->size, .offset = box->offset, .isTrigger = box->isTrigger};
  }
  const auto *model = entity.component<ModelCollider3DComponent>();
  if (model == nullptr)
    return std::nullopt;
  const auto asset = world.colliderAssets3D.find(model->asset);
  if (asset == world.colliderAssets3D.end())
    return std::nullopt;
  return BoxColliderShape3D{.size = asset->second.size,
                            .offset = asset->second.offset,
                            .isTrigger = model->isTrigger};
}

} // namespace demi::runtime
