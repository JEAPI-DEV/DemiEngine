#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace demi::editor {

struct FileRevision {
  std::filesystem::file_time_type modified;
  std::uintmax_t size = 0;
  std::uint64_t contentHash = 0;

  friend bool operator==(const FileRevision &, const FileRevision &) = default;
};

enum class DocumentWriteStatus { Written, Conflict, Failed };

class EditorDocumentStore {
public:
  [[nodiscard]] bool read(const std::filesystem::path &path, std::string &text,
                          FileRevision &revision, std::string &error) const;
  [[nodiscard]] DocumentWriteStatus
  writeIfUnchanged(const std::filesystem::path &path, const std::string &text,
                   const FileRevision &expected, FileRevision &revision,
                   std::string &error) const;
  [[nodiscard]] bool writeNew(const std::filesystem::path &path,
                              const std::string &text,
                              std::string &error) const;

private:
  [[nodiscard]] static bool revision(const std::filesystem::path &path,
                                     FileRevision &revision,
                                     std::string &error);
  [[nodiscard]] static bool writeAtomically(const std::filesystem::path &path,
                                            const std::string &text,
                                            std::string &error);
};

} // namespace demi::editor
