#include "editor/EditorDocumentStore.h"

#include <fstream>
#include <sstream>

namespace demi::editor {

bool EditorDocumentStore::revision(const std::filesystem::path &path,
                                   FileRevision &revision, std::string &error) {
  std::error_code filesystemError;
  revision.modified = std::filesystem::last_write_time(path, filesystemError);
  if (filesystemError) {
    error =
        "Could not inspect " + path.string() + ": " + filesystemError.message();
    return false;
  }
  revision.size = std::filesystem::file_size(path, filesystemError);
  if (filesystemError) {
    error =
        "Could not inspect " + path.string() + ": " + filesystemError.message();
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not inspect " + path.string() + '.';
    return false;
  }
  constexpr std::uint64_t OffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t Prime = 1099511628211ULL;
  std::uint64_t hash = OffsetBasis;
  char byte = 0;
  while (input.get(byte)) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= Prime;
  }
  if (!input.eof()) {
    error = "Could not finish inspecting " + path.string() + '.';
    return false;
  }
  revision.contentHash = hash;
  return true;
}

bool EditorDocumentStore::read(const std::filesystem::path &path,
                               std::string &text, FileRevision &revision,
                               std::string &error) const {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not read " + path.string() + '.';
    return false;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    error = "Could not finish reading " + path.string() + '.';
    return false;
  }
  text = contents.str();
  return EditorDocumentStore::revision(path, revision, error);
}

bool EditorDocumentStore::writeIfUnchanged(const std::filesystem::path &path,
                                           const std::string &text,
                                           const FileRevision &expected,
                                           FileRevision &revision,
                                           std::string &error) const {
  FileRevision current;
  if (!EditorDocumentStore::revision(path, current, error))
    return false;
  if (current != expected) {
    error = "The scene changed on disk. Refresh before saving or preserve your "
            "changes elsewhere.";
    return false;
  }

  const std::filesystem::path temporary = path.string() + ".demi-editor.tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "Could not create the temporary scene file.";
      return false;
    }
    output << text;
    output.flush();
    if (!output) {
      error = "Could not finish writing the temporary scene file.";
      return false;
    }
  }

  std::error_code filesystemError;
  std::filesystem::rename(temporary, path, filesystemError);
  if (filesystemError) {
    std::error_code cleanupError;
    std::filesystem::remove(temporary, cleanupError);
    error =
        "Could not replace the scene atomically: " + filesystemError.message();
    return false;
  }
  return EditorDocumentStore::revision(path, revision, error);
}

} // namespace demi::editor
