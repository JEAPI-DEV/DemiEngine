#pragma once

#include "demi/assets/AssetGroup.h"
#include "demi/runtime/scene/model/ProjectData.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace demi::runtime {

// Runtime facade for authored asset groups. It owns group discovery and the
// scene-root adapter while AssetGroupService remains the lifetime authority.
class RuntimeAssetService {
public:
  RuntimeAssetService() = default;
  ~RuntimeAssetService();

  RuntimeAssetService(const RuntimeAssetService &) = delete;
  RuntimeAssetService &operator=(const RuntimeAssetService &) = delete;

  [[nodiscard]] bool configure(const ProjectData &project,
                               const AssetRegistry &registry,
                               Diagnostics *diagnostics = nullptr);
  void shutdown();
  void registerLoader(std::shared_ptr<assets::AssetResourceLoader> loader);
  [[nodiscard]] assets::AssetGroupRequestHandle
  prepare(std::string_view uri, Diagnostics *diagnostics = nullptr);
  // Starts an asynchronous load for either one asset:// resource or an
  // asset-group:// batch. Ready requests become resident automatically.
  [[nodiscard]] assets::AssetGroupRequestHandle
  load(std::string_view uri, Diagnostics *diagnostics = nullptr);
  // Scenes are implicit groups. This keeps scene data as the source of truth
  // while still giving transitions an explicit prepare/activate lifetime.
  [[nodiscard]] assets::AssetGroupRequestHandle
  prepareScene(std::string_view sceneId, Diagnostics *diagnostics = nullptr);
  [[nodiscard]] static std::string sceneGroupId(std::string_view sceneId);
  void update(double uploadBudgetMilliseconds = -1.0);
  [[nodiscard]] assets::AssetGroupProgress
  progress(assets::AssetGroupRequestHandle request) const;
  [[nodiscard]] bool activate(assets::AssetGroupRequestHandle request,
                              Diagnostics *diagnostics = nullptr);
  [[nodiscard]] bool cancel(assets::AssetGroupRequestHandle request);
  [[nodiscard]] bool unload(std::string_view uri,
                            Diagnostics *diagnostics = nullptr);
  [[nodiscard]] bool releaseScene(std::string_view sceneId,
                                  Diagnostics *diagnostics = nullptr);
  [[nodiscard]] bool reload(std::string_view assetId,
                            Diagnostics *diagnostics = nullptr);
  [[nodiscard]] bool
  reloadChangedResidentAssets(const AssetRegistry &previous,
                              Diagnostics *diagnostics = nullptr);
  [[nodiscard]] bool restoreResources(Diagnostics *diagnostics = nullptr);
  void handleLowMemory();
  [[nodiscard]] assets::AssetMemoryReport memoryReport() const;

private:
  [[nodiscard]] bool isFallbackAsset(const AssetManifest &asset) const;
  [[nodiscard]] std::vector<std::string>
  resolveRoot(std::string_view root, Diagnostics *diagnostics) const;

  ProjectData project_;
  const AssetRegistry *registry_ = nullptr;
  std::map<std::string, assets::AssetGroupDescriptor> groups_;
  std::vector<std::shared_ptr<assets::AssetResourceLoader>> loaders_;
  std::shared_ptr<assets::AssetResourceLoader> fallbackLoader_;
  std::unique_ptr<assets::AssetGroupService> service_;
  std::map<assets::AssetGroupRequestHandle, std::string> pendingLoads_;
};

[[nodiscard]] std::shared_ptr<assets::AssetResourceLoader>
createResidentSourceAssetLoader();

} // namespace demi::runtime
