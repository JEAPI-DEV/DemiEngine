#include "demi/runtime/assets/RuntimeAssetService.h"

#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/scene/SceneAssetReferences.h"
#include "demi/runtime/scene/SceneLoader.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>

namespace demi::runtime {
namespace {

void report(Diagnostics *diagnostics, std::string code, std::string message,
            std::string path) {
  if (diagnostics != nullptr)
    diagnostics->push_back({.severity = Severity::Error,
                            .code = std::move(code),
                            .message = std::move(message),
                            .path = std::move(path),
                            .suggestion = {}});
}

struct ResidentSourcePayload {
  std::vector<std::vector<std::byte>> files;
};

class ResidentSourceAssetLoader final : public assets::AssetResourceLoader {
public:
  using SupportPredicate = std::function<bool(const AssetManifest &)>;

  explicit ResidentSourceAssetLoader(SupportPredicate supports = {})
      : supports_(std::move(supports)) {}

  bool supports(const AssetManifest &asset) const override {
    return !supports_ || supports_(asset);
  }

  std::optional<assets::DecodedAsset>
  readAndDecode(const AssetManifest &asset, const std::atomic_bool &isCancelled,
                std::string &error) override {
    auto payload = std::make_shared<ResidentSourcePayload>();
    std::size_t bytes = 0;
    for (const std::filesystem::path &path : asset.sourcePaths) {
      if (isCancelled.load()) {
        error = "Asset preparation was cancelled.";
        return std::nullopt;
      }
      std::ifstream input(path, std::ios::binary | std::ios::ate);
      if (!input) {
        error = "Could not read asset source: " + path.string();
        return std::nullopt;
      }
      const std::streamsize size = input.tellg();
      if (size < 0) {
        error = "Could not determine asset source size: " + path.string();
        return std::nullopt;
      }
      input.seekg(0);
      std::vector<std::byte> content(static_cast<std::size_t>(size));
      if (size > 0 &&
          !input.read(reinterpret_cast<char *>(content.data()), size)) {
        error = "Could not read complete asset source: " + path.string();
        return std::nullopt;
      }
      bytes += content.size();
      payload->files.push_back(std::move(content));
    }
    error.clear();
    return assets::DecodedAsset{.payload = std::move(payload),
                                .decodedBytes = bytes,
                                .residentBytes = bytes};
  }

  bool upload(const AssetManifest &asset, const assets::DecodedAsset &decoded,
              std::string &error) override {
    if (!decoded.payload) {
      error = "Resident source upload received an empty payload.";
      return false;
    }
    resident_[asset.id] =
        std::static_pointer_cast<ResidentSourcePayload>(decoded.payload);
    error.clear();
    return true;
  }

  void unload(const std::string_view assetId) override {
    resident_.erase(std::string(assetId));
  }

  [[nodiscard]] std::optional<std::string>
  text(const std::string_view assetId) const {
    const auto found = resident_.find(std::string(assetId));
    if (found == resident_.end() || found->second == nullptr ||
        found->second->files.size() != 1)
      return std::nullopt;
    const std::vector<std::byte> &bytes = found->second->files.front();
    return std::string(reinterpret_cast<const char *>(bytes.data()),
                       bytes.size());
  }

  std::string_view backendName() const override { return "resident-source"; }

private:
  SupportPredicate supports_;
  std::map<std::string, std::shared_ptr<ResidentSourcePayload>> resident_;
};

} // namespace

RuntimeAssetService::~RuntimeAssetService() = default;

void RuntimeAssetService::shutdown() {
  service_.reset();
  groups_.clear();
  loaders_.clear();
  fallbackLoader_.reset();
  pendingLoads_.clear();
  registry_ = nullptr;
  project_ = {};
}

bool RuntimeAssetService::configure(const ProjectData &project,
                                    const AssetRegistry &registry,
                                    Diagnostics *diagnostics) {
  project_ = project;
  registry_ = &registry;
  groups_.clear();
  pendingLoads_.clear();
  for (const std::filesystem::path &path :
       collectKnownSourceFiles(project.projectDirectory)) {
    if (!isAssetGroupFile(path))
      continue;
    const auto group = assets::loadAssetGroup(path, diagnostics);
    if (!group)
      continue;
    if (!groups_.emplace(group->id, *group).second)
      report(diagnostics, "ASSET_GROUP_ID_DUPLICATE",
             "Multiple asset-group manifests declare the same stable ID.",
             group->id);
  }
  service_ = std::make_unique<assets::AssetGroupService>(
      registry, [this](const std::string_view root, Diagnostics *issues) {
        return resolveRoot(root, issues);
      });
  if (!fallbackLoader_) {
    fallbackLoader_ = std::make_shared<ResidentSourceAssetLoader>(
        ResidentSourceAssetLoader::SupportPredicate{
            [this](const AssetManifest &asset) {
              return isFallbackAsset(asset);
            }});
    loaders_.push_back(fallbackLoader_);
  }
  for (const auto &loader : loaders_)
    service_->registerLoader(loader);
  return diagnostics == nullptr || !hasErrors(*diagnostics);
}

void RuntimeAssetService::registerLoader(
    std::shared_ptr<assets::AssetResourceLoader> loader) {
  if (!loader)
    return;
  loaders_.push_back(loader);
  if (service_)
    service_->registerLoader(std::move(loader));
}

assets::AssetGroupRequestHandle
RuntimeAssetService::prepare(const std::string_view uri,
                             Diagnostics *diagnostics) {
  if (!service_) {
    report(diagnostics, "ASSET_SERVICE_UNAVAILABLE",
           "Runtime asset service is not configured.", std::string(uri));
    return 0;
  }
  if (uri.starts_with("asset-group://")) {
    const auto group = groups_.find(std::string(uri));
    if (group != groups_.end())
      return service_->prepare(group->second, diagnostics);
    report(diagnostics, "ASSET_GROUP_NOT_FOUND",
           "Asset group is not registered: " + std::string(uri),
           std::string(uri));
    return 0;
  }
  if (uri.starts_with("asset://") && registry_ != nullptr &&
      findAsset(*registry_, std::string(uri)) != nullptr)
    return service_->prepare(
        assets::AssetGroupDescriptor{.id = std::string(uri),
                                     .roots = {std::string(uri)},
                                     .budget = {},
                                     .sourcePath = {}},
        diagnostics);
  report(diagnostics, "ASSET_LOAD_URI_INVALID",
         "Expected a registered asset:// or asset-group:// URI.",
         std::string(uri));
  return 0;
}

assets::AssetGroupRequestHandle
RuntimeAssetService::load(const std::string_view uri,
                          Diagnostics *diagnostics) {
  const std::string id(uri);
  if ((service_ && service_->isGroupActive(id)) ||
      std::ranges::any_of(pendingLoads_, [&id](const auto &pending) {
        return pending.second == id;
      })) {
    report(diagnostics, "ASSET_ALREADY_LOADED",
           "Asset or asset group is already loaded: " + id, id);
    return 0;
  }

  const assets::AssetGroupRequestHandle request = prepare(uri, diagnostics);
  if (request != 0)
    pendingLoads_.emplace(request, id);
  return request;
}

assets::AssetGroupRequestHandle
RuntimeAssetService::prepareScene(const std::string_view sceneId,
                                  Diagnostics *diagnostics) {
  if (!service_ || !sceneId.starts_with("scene://")) {
    report(diagnostics, "ASSET_GROUP_SCENE_INVALID",
           "Implicit asset preparation requires a scene:// ID.",
           std::string(sceneId));
    return 0;
  }

  // An asset-free scene needs no request, but is still a valid transition.
  Diagnostics resolutionDiagnostics;
  const auto referencedAssets = resolveRoot(sceneId, &resolutionDiagnostics);
  if (hasErrors(resolutionDiagnostics)) {
    if (diagnostics != nullptr)
      diagnostics->insert(diagnostics->end(), resolutionDiagnostics.begin(),
                          resolutionDiagnostics.end());
    return 0;
  }
  if (referencedAssets.empty())
    return 0;

  assets::AssetGroupDescriptor group{.id = sceneGroupId(sceneId),
                                     .roots = {std::string(sceneId)},
                                     .budget = {},
                                     .sourcePath = {}};
  return service_->prepare(group, diagnostics);
}

std::string RuntimeAssetService::sceneGroupId(const std::string_view sceneId) {
  constexpr std::string_view prefix = "scene://";
  if (!sceneId.starts_with(prefix) || sceneId.size() == prefix.size())
    return {};
  return "asset-group://implicit-scene/" +
         std::string(sceneId.substr(prefix.size()));
}

void RuntimeAssetService::update(const double uploadBudgetMilliseconds) {
  if (!service_)
    return;
  service_->update(uploadBudgetMilliseconds);
  for (auto pending = pendingLoads_.begin(); pending != pendingLoads_.end();) {
    const auto state = service_->progress(pending->first).stage;
    if (state == assets::AssetGroupStage::Ready) {
      (void)service_->activate(pending->first);
      pending = pendingLoads_.erase(pending);
    } else if (state == assets::AssetGroupStage::Failed ||
               state == assets::AssetGroupStage::Cancelled) {
      pending = pendingLoads_.erase(pending);
    } else {
      ++pending;
    }
  }
}

assets::AssetGroupProgress RuntimeAssetService::progress(
    const assets::AssetGroupRequestHandle request) const {
  return service_ ? service_->progress(request)
                  : assets::AssetGroupProgress{
                        .stage = assets::AssetGroupStage::Failed,
                        .error = "Runtime asset service is not configured."};
}

bool RuntimeAssetService::activate(
    const assets::AssetGroupRequestHandle request, Diagnostics *diagnostics) {
  return service_ && service_->activate(request, diagnostics);
}

bool RuntimeAssetService::cancel(
    const assets::AssetGroupRequestHandle request) {
  if (!service_ || !service_->cancel(request))
    return false;
  pendingLoads_.erase(request);
  return true;
}

bool RuntimeAssetService::unload(const std::string_view uri,
                                 Diagnostics *diagnostics) {
  const auto pending = std::ranges::find_if(
      pendingLoads_, [uri](const auto &entry) { return entry.second == uri; });
  if (pending != pendingLoads_.end())
    return cancel(pending->first);
  if (!service_ || !service_->releaseGroup(uri, diagnostics))
    return false;
  return true;
}

bool RuntimeAssetService::releaseScene(const std::string_view sceneId,
                                       Diagnostics *diagnostics) {
  const std::string groupId = sceneGroupId(sceneId);
  return !groupId.empty() && unload(groupId, diagnostics);
}

bool RuntimeAssetService::reload(const std::string_view assetId,
                                 Diagnostics *diagnostics) {
  return service_ && service_->reload(assetId, diagnostics);
}

std::optional<std::string>
RuntimeAssetService::text(const std::string_view assetId,
                          Diagnostics *diagnostics) const {
  const auto loader =
      std::dynamic_pointer_cast<ResidentSourceAssetLoader>(fallbackLoader_);
  if (loader != nullptr)
    if (auto content = loader->text(assetId))
      return content;
  report(diagnostics, "ASSET_TEXT_NOT_RESIDENT",
         "Text requires one loaded resident source: " + std::string(assetId),
         std::string(assetId));
  return std::nullopt;
}

bool RuntimeAssetService::reloadChangedResidentAssets(
    const AssetRegistry &previous, Diagnostics *diagnostics) {
  if (!service_ || registry_ == nullptr)
    return false;
  const auto changed = [](const AssetManifest &before,
                          const AssetManifest &after) {
    return before.type != after.type || before.importer != after.importer ||
           before.importerVersion != after.importerVersion ||
           before.sourceHash != after.sourceHash ||
           before.settingsJson != after.settingsJson ||
           before.dependencies != after.dependencies ||
           before.sourcePaths != after.sourcePaths;
  };
  for (const assets::AssetMemoryEntry &entry :
       service_->memoryReport().assets) {
    const AssetManifest *before = findAsset(previous, entry.assetId);
    const AssetManifest *after = findAsset(*registry_, entry.assetId);
    if (before == nullptr || after == nullptr) {
      report(diagnostics, "ASSET_RELOAD_RESIDENT_ID_REMOVED",
             "A watched reload cannot remove or replace a resident stable ID.",
             entry.assetId);
      return false;
    }
    if (changed(*before, *after) &&
        !service_->reload(entry.assetId, diagnostics))
      return false;
  }
  return true;
}

bool RuntimeAssetService::restoreResources(Diagnostics *diagnostics) {
  return service_ && service_->restoreResources(diagnostics);
}

void RuntimeAssetService::handleLowMemory() {
  if (service_)
    service_->cancelPending();
}

assets::AssetMemoryReport RuntimeAssetService::memoryReport() const {
  return service_ ? service_->memoryReport() : assets::AssetMemoryReport{};
}

bool RuntimeAssetService::isFallbackAsset(const AssetManifest &asset) const {
  for (const auto &loader : loaders_)
    if (loader.get() != fallbackLoader_.get() && loader->supports(asset))
      return false;
  return true;
}

std::vector<std::string>
RuntimeAssetService::resolveRoot(const std::string_view root,
                                 Diagnostics *diagnostics) const {
  if (!root.starts_with("scene://")) {
    report(diagnostics, "ASSET_GROUP_ROOT_UNSUPPORTED",
           "Runtime asset groups support asset:// and scene:// roots.",
           std::string(root));
    return {};
  }
  std::string error;
  auto world = loadScene(project_, std::string(root), error);
  if (!world) {
    report(diagnostics, "ASSET_GROUP_SCENE_LOAD_FAILED", std::move(error),
           std::string(root));
    return {};
  }
  return collectSceneAssetReferences(*world, root);
}

std::shared_ptr<assets::AssetResourceLoader> createResidentSourceAssetLoader() {
  return std::make_shared<ResidentSourceAssetLoader>();
}

} // namespace demi::runtime
