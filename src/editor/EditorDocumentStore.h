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

class EditorDocumentStore {
public:
  [[nodiscard]] bool read(const std::filesystem::path &path, std::string &text,
                          FileRevision &revision, std::string &error) const;
  [[nodiscard]] bool writeIfUnchanged(const std::filesystem::path &path,
                                      const std::string &text,
                                      const FileRevision &expected,
                                      FileRevision &revision,
                                      std::string &error) const;

private:
  [[nodiscard]] static bool revision(const std::filesystem::path &path,
                                     FileRevision &revision,
                                     std::string &error);
};

} // namespace demi::editor
