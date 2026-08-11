#include "demi/runtime/physics/ColliderAsset3D.h"

#include "demi/assets/GltfSkinnedModel.h"
#include "demi/assets/ModelImportProfile.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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
    const float detail = document.value("detail", 0.0F);
    const std::string shape = document.value("shape", "");
    if (document.value("format_version", 0) != 1 ||
        (shape != "box" && shape != "triangle_mesh") || !size ||
        size->x <= 0.0F || size->y <= 0.0F || size->z <= 0.0F ||
        !std::isfinite(detail) || detail < 0.0F || detail > 1.0F) {
      error = "Collider asset must define format_version 1 and a positive "
              "box size: " +
              asset.manifestPath.string();
      return std::nullopt;
    }
    ColliderAsset3D collider{
        .size = *size, .offset = offset, .detail = detail, .triangles = {}};
    if (detail > 0.0F) {
      std::string geometryError;
      const nlohmann::json settings =
          nlohmann::json::parse(asset.settingsJson, nullptr, false);
      Diagnostics diagnostics;
      const auto profile = assets::parseModelImportProfile(
          settings.is_object() ? settings : nlohmann::json::object(),
          &diagnostics, asset.manifestPath.string());
      const auto geometry =
          profile ? assets::loadGltfSkinnedModel3D(asset.sourcePath, *profile,
                                                   geometryError)
                  : std::nullopt;
      std::vector<Vec3> positions;
      if (!geometry ||
          !geometry->bindPosePositions(positions, geometryError)) {
        error = "Could not load triangle geometry for collider asset " +
                asset.id + ": " + geometryError;
        return std::nullopt;
      }
      const std::size_t total = geometry->indices.size() / 3U;
      const std::size_t count =
          detail >= 1.0F
              ? total
              : std::max<std::size_t>(
                    1U, static_cast<std::size_t>(std::floor(total * detail)));
      collider.triangles.reserve(count);
      for (std::size_t index = 0; index < count; ++index) {
        const std::size_t triangle = index * total / count;
        const auto position = [&](const std::size_t corner) {
          const std::uint32_t vertex =
              geometry->indices[triangle * 3U + corner];
          return vertex < positions.size() ? positions[vertex] : Vec3{};
        };
        collider.triangles.push_back(
            {.a = position(0), .b = position(1), .c = position(2)});
      }
    }
    return collider;
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

const std::vector<TriangleCollider3D> *
resolvedTriangleCollider3D(const World &world, const Entity &entity) {
  const auto *model = entity.component<ModelCollider3DComponent>();
  if (model == nullptr)
    return nullptr;
  const auto asset = world.colliderAssets3D.find(model->asset);
  return asset == world.colliderAssets3D.end() ||
                 asset->second.triangles.empty()
             ? nullptr
             : &asset->second.triangles;
}

} // namespace demi::runtime
