#include "editor/EditorAssetGroupDocument.h"

#include <algorithm>
#include <set>

namespace demi::editor {

bool EditorAssetGroupDocument::open(const std::filesystem::path &path,
                                    std::string &error) {
  std::string text;
  FileRevision revision;
  if (!store_.read(path, text, revision, error))
    return false;
  try {
    nlohmann::json document = nlohmann::json::parse(text);
    if (!valid(document, error))
      return false;
    path_ = path;
    revision_ = revision;
    id_ = document.value("id", "");
    document_ = std::move(document);
    savedCanonical_ = document_.dump();
    undo_.clear();
    redo_.clear();
    return true;
  } catch (const nlohmann::json::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool EditorAssetGroupDocument::setRoots(std::vector<std::string> roots,
                                        std::string &error) {
  std::ranges::sort(roots);
  if (roots.empty() || std::ranges::adjacent_find(roots) != roots.end() ||
      std::ranges::any_of(roots, [](const std::string &root) {
        return !((root.starts_with("asset://") && root.size() > 8) ||
                 (root.starts_with("scene://") && root.size() > 8));
      })) {
    error = "Asset-group roots must be unique asset:// or scene:// IDs.";
    return false;
  }
  nlohmann::json replacement = document_;
  replacement["roots"] = std::move(roots);
  if (!valid(replacement, error))
    return false;
  if (replacement == document_)
    return true;
  undo_.push_back({.before = document_, .after = replacement});
  redo_.clear();
  document_ = std::move(replacement);
  return true;
}

bool EditorAssetGroupDocument::undo(std::string &error) {
  if (undo_.empty()) {
    error = "There is no asset-group edit to undo.";
    return false;
  }
  Change change = std::move(undo_.back());
  undo_.pop_back();
  document_ = change.before;
  redo_.push_back(std::move(change));
  return true;
}

bool EditorAssetGroupDocument::redo(std::string &error) {
  if (redo_.empty()) {
    error = "There is no asset-group edit to redo.";
    return false;
  }
  Change change = std::move(redo_.back());
  redo_.pop_back();
  document_ = change.after;
  undo_.push_back(std::move(change));
  return true;
}

bool EditorAssetGroupDocument::save(std::string &error) {
  FileRevision replacement;
  if (store_.writeIfUnchanged(path_, document_.dump(2) + '\n', revision_,
                              replacement,
                              error) != DocumentWriteStatus::Written)
    return false;
  revision_ = replacement;
  savedCanonical_ = document_.dump();
  return true;
}

std::vector<std::string> EditorAssetGroupDocument::roots() const {
  return document_.value("roots", std::vector<std::string>{});
}

bool EditorAssetGroupDocument::isDirty() const {
  return document_.dump() != savedCanonical_;
}

bool EditorAssetGroupDocument::valid(const nlohmann::json &document,
                                     std::string &error) const {
  if (document.value("format_version", 0) != 1 ||
      !document.value("id", "").starts_with("asset-group://") ||
      !document.contains("roots") || !document["roots"].is_array()) {
    error = "Invalid versioned asset-group document.";
    return false;
  }
  std::set<std::string> roots;
  for (const nlohmann::json &root : document["roots"]) {
    const std::string id = root.is_string() ? root.get<std::string>() : "";
    if (!root.is_string() ||
        !((id.starts_with("asset://") && id.size() > 8) ||
          (id.starts_with("scene://") && id.size() > 8)) ||
        !roots.insert(id).second) {
      error = "Invalid or duplicate asset-group root.";
      return false;
    }
  }
  if (roots.empty()) {
    error = "Asset groups require at least one root.";
    return false;
  }
  return true;
}

} // namespace demi::editor
