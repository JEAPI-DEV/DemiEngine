#include "editor/EditorRecoveryStore.h"

#include "editor/EditorDocumentStore.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace demi::editor {
namespace {

std::filesystem::path environmentPath(const char *name) {
  const char *value = std::getenv(name);
  return value == nullptr || *value == '\0' ? std::filesystem::path{}
                                            : std::filesystem::path(value);
}

std::uint64_t pathHash(const std::filesystem::path &path) {
  constexpr std::uint64_t Offset = 14695981039346656037ULL;
  constexpr std::uint64_t Prime = 1099511628211ULL;
  std::uint64_t result = Offset;
  for (const unsigned char byte :
       std::filesystem::absolute(path).lexically_normal().generic_string()) {
    result ^= byte;
    result *= Prime;
  }
  return result;
}

std::filesystem::path fallbackHomeRoot(const char *child) {
  const std::filesystem::path home = environmentPath("HOME");
  return home.empty() ? std::filesystem::temp_directory_path() : home / child;
}

} // namespace

std::filesystem::path defaultEditorCacheDirectory() {
  std::filesystem::path root = environmentPath("XDG_CACHE_HOME");
  if (root.empty())
    root = fallbackHomeRoot(".cache");
  return root / "demi-editor";
}

std::filesystem::path defaultEditorDataDirectory() {
  std::filesystem::path root = environmentPath("XDG_DATA_HOME");
  if (root.empty())
    root = fallbackHomeRoot(".local/share");
  return root / "demi-editor";
}

EditorRecoveryStore::EditorRecoveryStore(std::filesystem::path root)
    : root_(std::move(root)) {}

std::filesystem::path EditorRecoveryStore::recoveryPath(
    const std::filesystem::path &projectPath) const {
  std::ostringstream name;
  name << std::hex << std::setfill('0') << std::setw(16)
       << pathHash(projectPath) << ".recovery.json";
  return root_ / "recovery" / name.str();
}

bool EditorRecoveryStore::update(
    const std::filesystem::path &projectPath,
    const std::vector<EditorRecoveryDocument> &documents, std::string &error) {
  if (documents.empty())
    return discard(projectPath, error);
  nlohmann::json entries = nlohmann::json::array();
  for (const EditorRecoveryDocument &document : documents)
    entries.push_back({{"path", document.path.string()},
                       {"kind", document.kind},
                       {"content", document.content}});
  const nlohmann::json snapshot{
      {"format_version", 1},
      {"project",
       std::filesystem::absolute(projectPath).lexically_normal().string()},
      {"documents", std::move(entries)}};
  std::error_code directoryError;
  std::filesystem::create_directories(recoveryPath(projectPath).parent_path(),
                                      directoryError);
  if (directoryError) {
    error = "Could not create editor recovery directory: " +
            directoryError.message();
    return false;
  }
  return EditorDocumentStore::writeAtomically(recoveryPath(projectPath),
                                              snapshot.dump(2) + '\n', error);
}

std::optional<EditorRecoverySnapshot>
EditorRecoveryStore::load(const std::filesystem::path &projectPath,
                          std::string &error) const {
  const std::filesystem::path path = recoveryPath(projectPath);
  if (!std::filesystem::is_regular_file(path))
    return std::nullopt;
  try {
    std::ifstream input(path);
    nlohmann::json value = nlohmann::json::parse(input);
    const std::string expected =
        std::filesystem::absolute(projectPath).lexically_normal().string();
    if (value.value("format_version", 0) != 1 ||
        value.value("project", "") != expected ||
        !value.contains("documents") || !value["documents"].is_array()) {
      error =
          "The editor recovery cache is invalid or belongs to another project.";
      return std::nullopt;
    }
    EditorRecoverySnapshot snapshot{.projectPath = expected};
    for (const nlohmann::json &document : value["documents"])
      if (document.is_object() && document.contains("content"))
        snapshot.documents.push_back(
            {.path = document.value("path", ""),
             .kind = document.value("kind", "document"),
             .content = document["content"]});
    return snapshot.documents.empty() ? std::optional<EditorRecoverySnapshot>{}
                                      : std::make_optional(std::move(snapshot));
  } catch (const std::exception &exception) {
    error = exception.what();
    return std::nullopt;
  }
}

bool EditorRecoveryStore::discard(const std::filesystem::path &projectPath,
                                  std::string &error) const {
  std::error_code code;
  std::filesystem::remove(recoveryPath(projectPath), code);
  if (code) {
    error = "Could not remove editor recovery cache: " + code.message();
    return false;
  }
  error.clear();
  return true;
}

} // namespace demi::editor
