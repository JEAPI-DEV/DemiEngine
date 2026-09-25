#pragma once

#include <filesystem>
#include <string_view>
#include <system_error>

namespace demi {

// Writes a complete file through an exclusively created temporary sibling.
// Creates missing parent directories. Rejects non-regular existing targets,
// including symlinks. Failure never removes the destination. Concurrent writers
// are last-commit-wins; this is not conflict detection or crash-durable
// storage. Preserves existing POSIX rwx permissions; new POSIX files are
// private. Windows copies the existing DACL as a protected snapshot; new files
// inherit the parent ACL. Ownership, extended attributes and future ACL
// inheritance are not preserved.
[[nodiscard]] bool atomicWriteText(const std::filesystem::path &path,
                                   std::string_view text,
                                   std::error_code &error);

} // namespace demi
