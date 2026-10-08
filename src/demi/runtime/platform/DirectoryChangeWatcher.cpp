#include "demi/runtime/platform/DirectoryChangeWatcher.h"
#include "demi/filesystem/ProjectPaths.h"
#include <array>
#include <cerrno>
#include <cstring>
#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace demi::runtime::platform {
DirectoryChangeWatcher::~DirectoryChangeWatcher() {
#ifdef __linux__
  if (descriptor_ >= 0)
    ::close(descriptor_);
#endif
}
void DirectoryChangeWatcher::reset() {
  directories_.clear();
  error_.clear();
#ifdef __linux__
  if (descriptor_ >= 0)
    ::close(descriptor_);
  descriptor_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (descriptor_ < 0)
    error_ = std::strerror(errno);
#else
  error_ = "Native directory notifications are unavailable on this platform.";
#endif
}
void DirectoryChangeWatcher::watch(const std::filesystem::path &directory) {
#ifdef __linux__
  if (descriptor_ < 0)
    return;
  const int id = ::inotify_add_watch(
      descriptor_, directory.c_str(),
      IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE | IN_CREATE |
          IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_DONT_FOLLOW);
  if (id < 0)
    error_ = std::strerror(errno);
  else
    directories_[id] = directory;
#endif
}
DirectoryChanges DirectoryChangeWatcher::poll() {
  DirectoryChanges result;
#ifdef __linux__
  if (descriptor_ < 0)
    return result;
  alignas(inotify_event) std::array<char, 65536> buffer;
  // Bound work per editor frame even if an external tool floods the queue.
  for (int batch = 0; batch < 16; ++batch) {
    const auto count = ::read(descriptor_, buffer.data(), buffer.size());
    if (count < 0) {
      if (errno != EAGAIN && errno != EINTR)
        error_ = std::strerror(errno);
      break;
    }
    if (count == 0)
      break;
    for (std::size_t offset = 0; offset < static_cast<std::size_t>(count);) {
      const auto &event =
          *reinterpret_cast<const inotify_event *>(buffer.data() + offset);
      offset += sizeof(inotify_event) + event.len;
      if (event.mask & IN_Q_OVERFLOW) {
        result.rescan = true;
        continue;
      }
      const auto found = directories_.find(event.wd);
      if (found == directories_.end())
        continue;
      if (event.mask & IN_IGNORED) {
        directories_.erase(found);
        continue;
      }
      if (event.len && isInternalProjectDirectory(event.name))
        continue;
      if (event.mask & (IN_ISDIR | IN_DELETE_SELF | IN_MOVE_SELF)) {
        result.rescan = true;
      } else if (event.len && !(event.mask & IN_CREATE)) {
        result.paths.push_back(found->second / event.name);
      }
    }
  }
#endif
  return result;
}
} // namespace demi::runtime::platform
