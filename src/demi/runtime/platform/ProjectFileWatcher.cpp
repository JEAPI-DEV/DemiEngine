#include "demi/runtime/platform/ProjectFileWatcher.h"

#include <algorithm>

namespace demi::runtime::platform {
namespace {

bool ignored(const std::filesystem::path &relative) {
  if (relative.empty())
    return true;
  const std::string first = (*relative.begin()).string();
  return first == "build" || first == "generated" || first == ".git" ||
         first == ".demi" || first == "saves";
}

} // namespace

void ProjectFileWatcher::reset(const std::filesystem::path &projectDirectory) {
  projectDirectory_ = std::filesystem::absolute(projectDirectory);
  snapshot_ = scan();
  generation_ = 0;
}

std::unordered_map<std::string, ProjectFileWatcher::Signature>
ProjectFileWatcher::scan() const {
  std::unordered_map<std::string, Signature> result;
  std::error_code error;
  if (!std::filesystem::is_directory(projectDirectory_, error))
    return result;
  std::filesystem::recursive_directory_iterator iterator(
      projectDirectory_,
      std::filesystem::directory_options::skip_permission_denied, error);
  for (const auto &entry : iterator) {
    if (error) {
      error.clear();
      continue;
    }
    const auto relative = entry.path().lexically_relative(projectDirectory_);
    if (entry.is_directory(error) && ignored(relative)) {
      iterator.disable_recursion_pending();
      continue;
    }
    if (!entry.is_regular_file(error) || ignored(relative))
      continue;
    Signature signature;
    signature.writeTime = entry.last_write_time(error);
    if (error) {
      error.clear();
      continue;
    }
    signature.size = entry.file_size(error);
    if (error) {
      error.clear();
      continue;
    }
    result.emplace(relative.generic_string(), signature);
  }
  return result;
}

ProjectFileChangeBatch ProjectFileWatcher::poll() {
  ProjectFileChangeBatch batch;
  const auto current = scan();
  for (const auto &[path, signature] : current) {
    const auto previous = snapshot_.find(path);
    if (previous == snapshot_.end() || previous->second != signature)
      batch.changed.push_back(projectDirectory_ / path);
  }
  for (const auto &[path, unused] : snapshot_) {
    (void)unused;
    if (!current.contains(path))
      batch.removed.push_back(projectDirectory_ / path);
  }
  snapshot_ = current;
  if (!batch.empty())
    batch.generation = ++generation_;
  std::ranges::sort(batch.changed);
  std::ranges::sort(batch.removed);
  return batch;
}

} // namespace demi::runtime::platform
