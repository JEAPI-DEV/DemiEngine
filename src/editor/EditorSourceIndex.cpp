#include "editor/EditorSourceIndex.h"
#include "editor/EditorProjectFolders.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <tuple>

namespace demi::editor {
void EditorSourceIndex::updateScript(const std::filesystem::path &path) {
  if (path.extension() != ".lua")
    return;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    scripts_.erase(path);
    return;
  }
  std::string content{std::istreambuf_iterator<char>(input), {}};
  auto found = scripts_.find(path);
  if (found != scripts_.end() && found->second.content == content)
    return;
  Script script;
  script.content = std::move(content);
  script.metadata =
      parseEditorLuaComponentMetadata(path, root_, &script.diagnostic);
  scripts_.insert_or_assign(path, std::move(script));
  ++parseCount_;
}
void EditorSourceIndex::rebuildCatalog() {
  catalog_ = {};
  reportedWatchError_ = watcher_.error();
  for (const auto &[path, script] : scripts_) {
    if (script.metadata)
      catalog_.components.push_back(*script.metadata);
    if (!script.diagnostic.code.empty())
      catalog_.diagnostics.push_back(script.diagnostic);
  }
  if (!watcher_.error().empty())
    catalog_.diagnostics.push_back(
        {.severity = Severity::Warning,
         .code = "EDITOR_SOURCE_WATCH_UNAVAILABLE",
         .message = "Automatic script discovery: " + watcher_.error(),
         .path = root_.string(),
         .suggestion = "Use F5 to rescan project sources."});
  std::ranges::sort(catalog_.components, [](const auto &a, const auto &b) {
    return std::tie(a.category, a.displayName, a.module) <
           std::tie(b.category, b.displayName, b.module);
  });
  ++revision_;
}
void EditorSourceIndex::rescan(const std::filesystem::path &root) {
  if (root_ != root)
    scripts_.clear();
  root_ = root;
  sources_.clear();
  directories_.clear();
  pending_.clear();
  rescanPending_ = false;
  watcher_.reset();
  watcher_.watch(root_);
  std::error_code error;
  std::filesystem::recursive_directory_iterator it(
      root_, std::filesystem::directory_options::skip_permission_denied, error),
      end;
  for (; !error && it != end; it.increment(error)) {
    const auto status = it->symlink_status(error);
    if (error)
      break;
    if (std::filesystem::is_symlink(status)) {
      it.disable_recursion_pending();
      continue;
    }
    if (std::filesystem::is_directory(status)) {
      if (isEditorInternalDirectory(it->path().filename().string()))
        it.disable_recursion_pending();
      else {
        directories_.insert(it->path().lexically_relative(root_));
        watcher_.watch(it->path());
      }
    } else if (std::filesystem::is_regular_file(status)) {
      sources_.push_back(it->path());
      updateScript(it->path());
    }
  }
  std::ranges::sort(sources_);
  std::erase_if(scripts_, [&](const auto &entry) {
    return !std::ranges::binary_search(sources_, entry.first);
  });
  rebuildCatalog();
}
void EditorSourceIndex::changed(const std::filesystem::path &path) {
  updatePath(path);
  rebuildCatalog();
}
void EditorSourceIndex::updatePath(const std::filesystem::path &path) {
  const auto relative = path.lexically_relative(root_);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    return;
  for (const auto &part : relative)
    if (isEditorInternalDirectory(part.string()))
      return;
  std::error_code error;
  auto parent = root_;
  for (const auto &part : relative.parent_path()) {
    parent /= part;
    if (std::filesystem::is_symlink(parent, error))
      return;
    if (directories_.insert(parent.lexically_relative(root_)).second)
      watcher_.watch(parent);
  }
  const auto status = std::filesystem::symlink_status(path, error);
  const auto position = std::ranges::lower_bound(sources_, path);
  if (!error && std::filesystem::is_regular_file(status)) {
    if (position == sources_.end() || *position != path)
      sources_.insert(position, path);
    updateScript(path);
  } else {
    if (position != sources_.end() && *position == path)
      sources_.erase(position);
    scripts_.erase(path);
  }
}
void EditorSourceIndex::poll(Clock::time_point now) {
  auto changes = watcher_.poll();
  if (watcher_.error() != reportedWatchError_)
    rebuildCatalog();
  std::erase_if(changes.paths,
                [](const auto &path) { return path.extension() != ".lua"; });
  if (changes.rescan || !changes.paths.empty()) {
    if (pending_.empty() && !rescanPending_)
      firstEvent_ = now;
    lastEvent_ = now;
    rescanPending_ |= changes.rescan;
    pending_.insert(changes.paths.begin(), changes.paths.end());
  }
  if (pending_.empty() && !rescanPending_)
    return;
  if (now - lastEvent_ < std::chrono::milliseconds(150) &&
      now - firstEvent_ < std::chrono::milliseconds(500))
    return;
  if (rescanPending_) {
    rescan(root_);
    return;
  }
  auto pending = std::move(pending_);
  pending_.clear();
  for (const auto &path : pending)
    updatePath(path);
  rebuildCatalog();
}
} // namespace demi::editor
