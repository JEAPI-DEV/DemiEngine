#include "demi/runtime/tilemap/TilemapRuntime.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/navigation/NavigationGrid2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"
#include "demi/runtime/tilemap/TilemapAsset.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {

namespace {
std::string overrideKey(const std::string &layer, const int column,
                        const int row) {
  return layer + "/" + std::to_string(column) + "/" + std::to_string(row);
}
} // namespace

void TilemapRuntime::attach(World *world, const AssetRegistry *assets,
                            navigation::NavigationGrid2D *navigation) {
  world_ = world;
  assets_ = assets;
  navigation_ = navigation;
}

std::optional<int> TilemapRuntime::tile(const std::string &entityId,
                                        const std::string &layerName,
                                        const int column, const int row) const {
  if (world_ == nullptr || assets_ == nullptr)
    return std::nullopt;
  const Entity *entity = findEntity(*world_, entityId);
  const auto *component =
      entity != nullptr ? entity->component<Tilemap2DComponent>() : nullptr;
  if (component == nullptr)
    return std::nullopt;
  const auto overridden =
      component->tileOverrides.find(overrideKey(layerName, column, row));
  if (overridden != component->tileOverrides.end())
    return overridden->second;
  const AssetManifest *manifest = findAsset(*assets_, component->asset);
  if (manifest == nullptr)
    return std::nullopt;
  std::string error;
  const auto asset = loadTilemapAsset(*manifest, error);
  if (!asset || column < 0 || row < 0 || column >= asset->columns ||
      row >= asset->rows)
    return std::nullopt;
  for (const TilemapLayer2D &layer : asset->layers) {
    if (layer.name == layerName)
      return layer
          .tiles[static_cast<std::size_t>(row * asset->columns + column)];
  }
  return std::nullopt;
}

bool TilemapRuntime::setTile(const std::string &entityId,
                             const std::string &layerName, const int column,
                             const int row, const int tileValue) {
  if (tileValue < 0 || !tile(entityId, layerName, column, row).has_value())
    return false;
  Entity *entity = findEntity(*world_, entityId);
  auto *component = entity->component<Tilemap2DComponent>();
  component->tileOverrides.insert_or_assign(overrideKey(layerName, column, row),
                                            tileValue);
  component->dirtyChunks.insert(layerName + "/" + std::to_string(column / 16) +
                                "/" + std::to_string(row / 16));
  world_->tilemapCollisionDirty = true;
  if (navigationEntityId_ == entityId)
    return refreshNavigation();
  return true;
}

bool TilemapRuntime::clearOverrides(const std::string &entityId) {
  if (world_ == nullptr)
    return false;
  Entity *entity = findEntity(*world_, entityId);
  auto *component =
      entity != nullptr ? entity->component<Tilemap2DComponent>() : nullptr;
  if (component == nullptr)
    return false;
  component->tileOverrides.clear();
  component->dirtyChunks.insert("*");
  world_->tilemapCollisionDirty = true;
  if (navigationEntityId_ == entityId)
    return refreshNavigation();
  return true;
}

bool TilemapRuntime::bakeNavigation(const std::string &entityId) {
  navigationEntityId_ = entityId;
  if (refreshNavigation())
    return true;
  navigationEntityId_.clear();
  return false;
}

std::vector<TilemapObject2D>
TilemapRuntime::objects(const std::string &entityId,
                        const std::string &layerName) const {
  if (world_ == nullptr || assets_ == nullptr)
    return {};
  const Entity *entity = findEntity(*world_, entityId);
  const auto *component =
      entity != nullptr ? entity->component<Tilemap2DComponent>() : nullptr;
  const AssetManifest *manifest =
      component != nullptr ? findAsset(*assets_, component->asset) : nullptr;
  if (manifest == nullptr)
    return {};
  std::string error;
  const auto asset = loadTilemapAsset(*manifest, error);
  if (!asset)
    return {};
  for (const TilemapObjectLayer2D &layer : asset->objectLayers)
    if (layer.name == layerName)
      return layer.objects;
  return {};
}

bool TilemapRuntime::refreshNavigation() {
  if (world_ == nullptr || assets_ == nullptr || navigation_ == nullptr ||
      navigationEntityId_.empty())
    return false;
  const Entity *entity = findEntity(*world_, navigationEntityId_);
  const auto *component =
      entity != nullptr ? entity->component<Tilemap2DComponent>() : nullptr;
  const AssetManifest *manifest =
      component != nullptr ? findAsset(*assets_, component->asset) : nullptr;
  if (component == nullptr || manifest == nullptr)
    return false;
  std::string error;
  const auto asset = loadTilemapAsset(*manifest, error);
  if (!asset)
    return false;
  const float cellWidth =
      static_cast<float>(asset->tileWidth) / component->pixelsPerUnit;
  const float cellHeight =
      static_cast<float>(asset->tileHeight) / component->pixelsPerUnit;
  if (std::abs(cellWidth - cellHeight) > 0.0001F)
    return false;
  if (!navigation_->configure(asset->columns, asset->rows, cellWidth,
                              worldPosition2D(*world_, *entity)))
    return false;

  for (const TilemapLayer2D &layer : asset->layers) {
    if (!layer.navigationBlocked && layer.navigationCost <= 1.0F)
      continue;
    for (int row = 0; row < asset->rows; ++row) {
      for (int column = 0; column < asset->columns; ++column) {
        const auto overridden =
            component->tileOverrides.find(overrideKey(layer.name, column, row));
        const int tileValue = overridden != component->tileOverrides.end()
                                  ? overridden->second
                                  : layer.tiles[static_cast<std::size_t>(
                                        row * asset->columns + column)];
        if (tileValue <= 0)
          continue;
        const navigation::NavigationCell2D cell{column, asset->rows - row - 1};
        if (layer.navigationBlocked)
          (void)navigation_->setBlocked(cell, true);
        if (layer.navigationCost > 1.0F)
          (void)navigation_->setCost(
              cell, std::max(navigation_->cost(cell), layer.navigationCost));
      }
    }
  }
  return true;
}

} // namespace demi::runtime
