#include "demi/runtime/platform/ProjectFileWatcher.h"
#include "demi/filesystem/ProjectPaths.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <uv.h>

namespace demi::runtime::platform {
namespace {

bool ignored(const std::filesystem::path &relative) {
  if (relative.empty())
    return true;
  const std::string first = (*relative.begin()).string();
  return first == "saves" ||
         std::ranges::any_of(relative, [](const auto &part) {
           return isInternalProjectDirectory(part.string());
         });
}

} // namespace

// libuv owns the platform event backend. One watch per admitted directory also
// works on Linux, where recursive fs-event watches are not supported. Events
// invalidate the existing snapshot; idle frames do no directory/stat traversal.
class ProjectFileWatcher::NativeEvents {
public:
  explicit NativeEvents(std::filesystem::path root) : root_(std::move(root)) {
    initialized_ = uv_loop_init(&loop_) == 0;
    fallback = !initialized_;
  }
  ~NativeEvents() {
    if (!initialized_) {
      return;
    }
    for (auto &[path, watch] : watches_) {
      close(watch.release());
    }
    watches_.clear();
    uv_run(&loop_, UV_RUN_DEFAULT);
    uv_loop_close(&loop_);
  }
  bool poll() {
    if (initialized_) {
      uv_run(&loop_, UV_RUN_NOWAIT);
    }
    return std::exchange(dirty_, false);
  }
  void beginScan() {
    seen_.clear();
    // Keep observing root replacement/removal without watching sibling trees.
    add(root_.parent_path());
  }
  void add(const std::filesystem::path &directory) {
    if (!initialized_) {
      return;
    }
    const auto path = directory.lexically_normal().string();
    seen_.insert(path);
    if (watches_.contains(path)) {
      return;
    }
    auto watch = std::make_unique<Watch>();
    watch->owner = this;
    watch->directory = directory;
    if (uv_fs_event_init(&loop_, &watch->handle) != 0) {
      fallback = true;
      return;
    }
    watch->handle.data = watch.get();
    if (uv_fs_event_start(&watch->handle, changed, path.c_str(), 0) != 0) {
      fallback = true;
      close(watch.release());
      return;
    }
    watches_.emplace(path, std::move(watch));
  }
  void finishScan() {
    std::erase_if(watches_, [&](auto &entry) {
      if (seen_.contains(entry.first)) {
        return false;
      }
      close(entry.second.release());
      return true;
    });
  }
  bool fallback = false;

private:
  struct Watch {
    uv_fs_event_t handle{};
    NativeEvents *owner = nullptr;
    std::filesystem::path directory;
  };
  static void close(Watch *watch) {
    uv_close(
        reinterpret_cast<uv_handle_t *>(&watch->handle),
        [](uv_handle_t *handle) { delete static_cast<Watch *>(handle->data); });
  }
  static void changed(uv_fs_event_t *handle, const char *filename, int,
                      int status) {
    auto &watch = *static_cast<Watch *>(handle->data);
    auto &owner = *watch.owner;
    if (status < 0) {
      uv_fs_event_stop(handle);
      owner.fallback = true;
      owner.dirty_ = true;
      return;
    }
    const auto path = filename ? watch.directory / filename : watch.directory;
    const auto relative =
        path.lexically_normal().lexically_relative(owner.root_);
    if (relative == "." || (!relative.empty() && *relative.begin() != ".." &&
                            !ignored(relative))) {
      owner.dirty_ = true;
    }
  }
  uv_loop_t loop_{};
  bool initialized_ = false;
  bool dirty_ = false;
  std::filesystem::path root_;
  std::unordered_set<std::string> seen_;
  std::unordered_map<std::string, std::unique_ptr<Watch>> watches_;
};

ProjectFileWatcher::ProjectFileWatcher() = default;
ProjectFileWatcher::~ProjectFileWatcher() = default;
ProjectFileWatcher::ProjectFileWatcher(ProjectFileWatcher &&) noexcept =
    default;
ProjectFileWatcher &
ProjectFileWatcher::operator=(ProjectFileWatcher &&) noexcept = default;

void ProjectFileWatcher::reset(const std::filesystem::path &projectDirectory) {
  projectDirectory_ =
      std::filesystem::absolute(projectDirectory).lexically_normal();
  events_ = std::make_unique<NativeEvents>(projectDirectory_);
  nextFallbackScan_ = std::chrono::steady_clock::now();
  snapshot_ = scan();
  generation_ = 0;
}

std::unordered_map<std::string, ProjectFileWatcher::Signature>
ProjectFileWatcher::scan() {
  std::unordered_map<std::string, Signature> result;
  std::error_code error;
  events_->beginScan();
  if (!std::filesystem::is_directory(projectDirectory_, error)) {
    events_->finishScan();
    return result;
  }
  events_->add(projectDirectory_);
  std::filesystem::recursive_directory_iterator iterator(
      projectDirectory_,
      std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; iterator != end; iterator.increment(error)) {
    const auto &entry = *iterator;
    if (error) {
      error.clear();
      continue;
    }
    const auto relative = entry.path().lexically_relative(projectDirectory_);
    if (entry.is_directory(error)) {
      if (ignored(relative)) {
        iterator.disable_recursion_pending();
      } else if (!entry.is_symlink(error)) {
        events_->add(entry.path());
      }
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
  events_->finishScan();
  return result;
}

ProjectFileChangeBatch ProjectFileWatcher::poll() {
  ProjectFileChangeBatch batch;
  if (!events_) {
    return batch;
  }
  const auto now = std::chrono::steady_clock::now();
  const bool fallbackDue = events_->fallback && now >= nextFallbackScan_;
  if (!events_->poll() && !fallbackDue) {
    return batch;
  }
  // Unsupported/network filesystems and exhausted watch limits still reload,
  // but fallback polling is bounded instead of scanning on every game frame.
  nextFallbackScan_ = now + std::chrono::milliseconds(500);
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
