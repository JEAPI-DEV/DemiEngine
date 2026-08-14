#include "demi/runtime/assets/RuntimeAssetService.h"

#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/scene/SceneAssetReferences.h"
#include "demi/runtime/scene/SceneLoader.h"

#include <fstream>
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
  bool supports(const AssetManifest &) const override { return true; }

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
    resident_[asset.id] = decoded.payload;
    error.clear();
    return true;
  }

  void unload(const std::string_view assetId) override {
    resident_.erase(std::string(assetId));
  }

  std::string_view backendName() const override { return "resident-source"; }

private:
  std::map<std::string, std::shared_ptr<void>> resident_;
};

} // namespace

RuntimeAssetService::~RuntimeAssetService() = default;

bool RuntimeAssetService::configure(const ProjectData &project,
                                    const AssetRegistry &registry,
                                    Diagnostics *diagnostics) {
  project_ = project;
  registry_ = &registry;
  groups_.clear();
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
  if (loaders_.empty())
    loaders_.push_back(createResidentSourceAssetLoader());
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
RuntimeAssetService::prepare(const std::string_view groupId,
                             Diagnostics *diagnostics) {
  const auto group = groups_.find(std::string(groupId));
  if (!service_ || group == groups_.end()) {
    report(diagnostics, "ASSET_GROUP_NOT_FOUND",
           "Asset group is not registered: " + std::string(groupId),
           std::string(groupId));
    return 0;
  }
  return service_->prepare(group->second, diagnostics);
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
  if (service_)
    service_->update(uploadBudgetMilliseconds);
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
  return service_ && service_->cancel(request);
}

bool RuntimeAssetService::release(const std::string_view groupId,
                                  Diagnostics *diagnostics) {
  return service_ && service_->releaseGroup(groupId, diagnostics);
}

bool RuntimeAssetService::releaseScene(const std::string_view sceneId,
                                       Diagnostics *diagnostics) {
  const std::string groupId = sceneGroupId(sceneId);
  return !groupId.empty() && release(groupId, diagnostics);
}

bool RuntimeAssetService::reload(const std::string_view assetId,
                                 Diagnostics *diagnostics) {
  return service_ && service_->reload(assetId, diagnostics);
}

assets::AssetMemoryReport RuntimeAssetService::memoryReport() const {
  return service_ ? service_->memoryReport() : assets::AssetMemoryReport{};
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
