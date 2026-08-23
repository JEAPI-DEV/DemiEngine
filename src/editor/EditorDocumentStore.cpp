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

DocumentWriteStatus EditorDocumentStore::writeIfUnchanged(
    const std::filesystem::path &path, const std::string &text,
    const FileRevision &expected, FileRevision &revision,
    std::string &error) const {
  FileRevision current;
  if (!EditorDocumentStore::revision(path, current, error)) {
    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError) && !filesystemError) {
      error =
          "The document was removed from disk. Reload it or save your changes "
          "to a new file.";
      return DocumentWriteStatus::Conflict;
    }
    return DocumentWriteStatus::Failed;
  }
  if (current != expected) {
    error =
        "The document changed on disk. Choose whether to reload it, keep the "
        "in-memory version, or save a copy.";
    return DocumentWriteStatus::Conflict;
  }

  if (!writeAtomically(path, text, error))
    return DocumentWriteStatus::Failed;
  if (!EditorDocumentStore::revision(path, revision, error))
    return DocumentWriteStatus::Failed;
  return DocumentWriteStatus::Written;
}

bool EditorDocumentStore::writeNew(const std::filesystem::path &path,
                                   const std::string &text,
                                   std::string &error) const {
  std::error_code filesystemError;
  const bool exists = std::filesystem::exists(path, filesystemError);
  if (filesystemError) {
    error =
        "Could not inspect the copy destination: " + filesystemError.message();
    return false;
  }
  if (exists) {
    error = "The copy destination already exists; choose a new path.";
    return false;
  }
  return writeAtomically(path, text, error);
}

bool EditorDocumentStore::writeAtomically(const std::filesystem::path &path,
                                          const std::string &text,
                                          std::string &error) {
  const std::filesystem::path temporary = path.string() + ".demi-editor.tmp";
  std::error_code statusError;
  const std::filesystem::file_status temporaryStatus =
      std::filesystem::symlink_status(temporary, statusError);
  if (statusError && statusError != std::errc::no_such_file_or_directory) {
    error = "Could not inspect the temporary document file: " +
            statusError.message();
    return false;
  }
  if (!statusError && std::filesystem::exists(temporaryStatus) &&
      !std::filesystem::is_regular_file(temporaryStatus)) {
    error = "The temporary document path is not a regular file.";
    return false;
  }
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "Could not create the temporary document file.";
      return false;
    }
    output << text;
    output.flush();
    if (!output) {
      output.close();
      std::error_code cleanupError;
      std::filesystem::remove(temporary, cleanupError);
      error = "Could not finish writing the temporary document file.";
      return false;
    }
  }

  std::error_code filesystemError;
  std::filesystem::rename(temporary, path, filesystemError);
  if (filesystemError) {
    std::error_code cleanupError;
    std::filesystem::remove(temporary, cleanupError);
    error = "Could not replace the document atomically: " +
            filesystemError.message();
    return false;
  }
  return true;
}

} // namespace demi::editor
