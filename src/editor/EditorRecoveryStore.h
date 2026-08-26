#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace demi::editor {

struct EditorRecoveryDocument {
  std::filesystem::path path;
  std::string kind;
  nlohmann::json content;
};

struct EditorRecoverySnapshot {
  std::filesystem::path projectPath;
  std::vector<EditorRecoveryDocument> documents;
};

[[nodiscard]] std::filesystem::path defaultEditorCacheDirectory();
[[nodiscard]] std::filesystem::path defaultEditorDataDirectory();

// Recovery is an explicit cache, never an authored document store. Loading a
// snapshot does not apply it; the caller must present a restore/discard choice.
class EditorRecoveryStore {
public:
  explicit EditorRecoveryStore(std::filesystem::path root);

  [[nodiscard]] bool
  update(const std::filesystem::path &projectPath,
         const std::vector<EditorRecoveryDocument> &documents,
         std::string &error);
  [[nodiscard]] std::optional<EditorRecoverySnapshot>
  load(const std::filesystem::path &projectPath, std::string &error) const;
  [[nodiscard]] bool discard(const std::filesystem::path &projectPath,
                             std::string &error) const;
  [[nodiscard]] std::filesystem::path
  recoveryPath(const std::filesystem::path &projectPath) const;

private:
  std::filesystem::path root_;
};

} // namespace demi::editor
