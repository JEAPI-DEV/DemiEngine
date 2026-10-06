#include "demi/runtime/assets/TerrainAssetResourceLoader.h"
#include "demi/assets/TerrainAsset.h"

#include <map>

namespace demi::runtime {
namespace {
struct TerrainResource {
  std::shared_ptr<const HeightField> field;
};

class TerrainAssetResourceLoader final : public assets::AssetResourceLoader {
public:
  explicit TerrainAssetResourceLoader(const AssetRegistry &registry)
      : registry_(registry) {}

  bool supports(const AssetManifest &asset) const override {
    return asset.type == "Terrain";
  }

  std::optional<assets::DecodedAsset>
  readAndDecode(const AssetManifest &asset, const std::atomic_bool &cancelled,
                std::string &error) override {
    if (cancelled.load()) {
      error = "Terrain asset loading cancelled.";
      return std::nullopt;
    }
    try {
      auto resource = std::make_shared<TerrainResource>();
      resource->field = assets::loadTerrainAsset(registry_, asset.id);
      if (cancelled.load()) {
        error = "Terrain asset loading cancelled.";
        return std::nullopt;
      }
      const auto &field = *resource->field;
      const auto bytes =
          sizeof(HeightField) +
          field.heights.size() *
              (sizeof(float) * 3 + sizeof(Vec3) + sizeof(std::size_t)) +
          field.scatterPlacements.size() * sizeof(TerrainScatterPlacement);
      error.clear();
      return assets::DecodedAsset{.payload = std::move(resource),
                                  .decodedBytes = bytes,
                                  .residentBytes = bytes};
    } catch (const std::exception &exception) {
      error = exception.what();
      return std::nullopt;
    }
  }

  bool upload(const AssetManifest &asset, const assets::DecodedAsset &decoded,
              std::string &error) override {
    if (!decoded.payload) {
      error = "Terrain asset has no decoded data.";
      return false;
    }
    resident_.insert_or_assign(
        asset.id, std::static_pointer_cast<TerrainResource>(decoded.payload));
    error.clear();
    return true;
  }

  void unload(std::string_view assetId) override {
    resident_.erase(std::string(assetId));
  }

  std::string_view backendName() const override { return "terrain_cpu"; }

private:
  const AssetRegistry &registry_;
  std::map<std::string, std::shared_ptr<TerrainResource>> resident_;
};
} // namespace

std::shared_ptr<assets::AssetResourceLoader>
createTerrainAssetResourceLoader(const AssetRegistry &registry) {
  return std::make_shared<TerrainAssetResourceLoader>(registry);
}
} // namespace demi::runtime
