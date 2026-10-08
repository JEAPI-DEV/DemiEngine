#pragma once
#include "demi/runtime/platform/DirectoryChangeWatcher.h"
#include "editor/EditorLuaComponentMetadata.h"
#include <chrono>
#include <map>
#include <set>

namespace demi::editor {
// One read-only source/annotation projection shared by a project's document
// sessions. Updating it never reloads authored documents or alters their Undo
// histories.
class EditorSourceIndex {
public:
  using Clock = std::chrono::steady_clock;
  const std::filesystem::path &root() const { return root_; }
  void rescan(const std::filesystem::path &root);
  void changed(const std::filesystem::path &path);
  void poll(Clock::time_point now = Clock::now());
  const std::vector<std::filesystem::path> &sources() const { return sources_; }
  const std::set<std::filesystem::path> &directories() const {
    return directories_;
  }
  const EditorLuaComponentCatalog &scripts() const { return catalog_; }
  std::uint64_t revision() const { return revision_; }
  std::uint64_t parseCount() const { return parseCount_; }

private:
  struct Script {
    std::string content;
    std::optional<EditorLuaComponentMetadata> metadata;
    Diagnostic diagnostic;
  };
  void updatePath(const std::filesystem::path &path);
  void updateScript(const std::filesystem::path &path);
  void rebuildCatalog();
  std::string reportedWatchError_;
  std::filesystem::path root_;
  std::vector<std::filesystem::path> sources_;
  std::set<std::filesystem::path> directories_;
  std::map<std::filesystem::path, Script> scripts_;
  EditorLuaComponentCatalog catalog_;
  runtime::platform::DirectoryChangeWatcher watcher_;
  std::set<std::filesystem::path> pending_;
  bool rescanPending_ = false;
  Clock::time_point firstEvent_{}, lastEvent_{};
  std::uint64_t revision_ = 0, parseCount_ = 0;
};
} // namespace demi::editor
