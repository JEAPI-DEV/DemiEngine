#include "demi/runtime/scene/composition/PrefabResolutionContext.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/PackageContent.h"

#include <map>

namespace demi::runtime::composition {
namespace {

std::filesystem::path projectKey(const std::filesystem::path &project) {
  return std::filesystem::weakly_canonical(project);
}

} // namespace

struct PrefabResolutionContext::State {
  struct Sources {
    std::vector<std::filesystem::path> files;
    Diagnostics diagnostics;
  };
  std::map<std::filesystem::path, Sources> projects;
  std::size_t verifications = 0;
};

PrefabResolutionContext::PrefabResolutionContext()
    : state_(std::make_unique<State>()) {}

PrefabResolutionContext::PrefabResolutionContext(
    const std::filesystem::path &project)
    : PrefabResolutionContext() {
  (void)packageFiles(project);
}

PrefabResolutionContext::PrefabResolutionContext(
    const AssetRegistry &verifiedRegistry)
    : PrefabResolutionContext() {
  state_->projects.emplace(projectKey(verifiedRegistry.projectDirectory),
                           State::Sources{verifiedRegistry.packageFiles,
                                          verifiedRegistry.diagnostics});
}

PrefabResolutionContext::~PrefabResolutionContext() = default;

const std::vector<std::filesystem::path> *
PrefabResolutionContext::packageFiles(const std::filesystem::path &project) {
  const auto key = projectKey(project);
  auto found = state_->projects.find(key);
  if (found == state_->projects.end()) {
    auto content = assets::loadLockedPackageContent(project, "");
    ++state_->verifications;
    found = state_->projects
                .emplace(key, State::Sources{std::move(content.files),
                                             std::move(content.diagnostics)})
                .first;
  }
  return hasErrors(found->second.diagnostics) ? nullptr : &found->second.files;
}

Diagnostics PrefabResolutionContext::diagnostics() const {
  Diagnostics result;
  for (const auto &[project, sources] : state_->projects) {
    result.insert(result.end(), sources.diagnostics.begin(),
                  sources.diagnostics.end());
  }
  return result;
}

std::size_t PrefabResolutionContext::packageVerificationCount() const noexcept {
  return state_->verifications;
}

} // namespace demi::runtime::composition
