#include "demi/filesystem/AtomicTextFile.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace demi {
namespace {

std::error_code nativeError() {
#if defined(_WIN32)
  return {static_cast<int>(GetLastError()), std::system_category()};
#else
  return {errno, std::generic_category()};
#endif
}

struct TemporaryFile {
  TemporaryFile() = default;
  TemporaryFile(const TemporaryFile &) = delete;
  TemporaryFile &operator=(const TemporaryFile &) = delete;

  std::filesystem::path path;
  bool owned = false;
#if defined(_WIN32)
  HANDLE handle = INVALID_HANDLE_VALUE;
  std::vector<unsigned char> security;
#else
  int handle = -1;
#endif

  bool close(std::error_code &error) {
#if defined(_WIN32)
    if (handle == INVALID_HANDLE_VALUE)
      return true;
    const auto previous = handle;
    handle = INVALID_HANDLE_VALUE;
    if (CloseHandle(previous))
      return true;
#else
    if (handle == -1)
      return true;
    const auto previous = handle;
    handle = -1;
    // Do not retry close on EINTR: Linux has already released the descriptor.
    if (::close(previous) == 0)
      return true;
#endif
    error = nativeError();
    return false;
  }

  ~TemporaryFile() {
    std::error_code ignored;
    close(ignored);
    if (owned)
      std::filesystem::remove(path, ignored);
  }
};

bool reserveTemporary(const std::filesystem::path &parent, TemporaryFile &temp,
                      std::error_code &error) {
  static std::atomic<unsigned long long> sequence{0};
#if defined(_WIN32)
  const auto process = GetCurrentProcessId();
  SECURITY_ATTRIBUTES attributes{
      sizeof(SECURITY_ATTRIBUTES),
      temp.security.empty() ? nullptr : temp.security.data(), FALSE};
#else
  const auto process = getpid();
#endif
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  for (unsigned attempt = 0; attempt < 128; ++attempt) {
    temp.path = parent / (".demi-write-" + std::to_string(process) + "-" +
                          std::to_string(stamp) + "-" +
                          std::to_string(sequence.fetch_add(1)) + ".tmp");
#if defined(_WIN32)
    temp.handle = CreateFileW(temp.path.c_str(), GENERIC_WRITE, 0, &attributes,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (temp.handle != INVALID_HANDLE_VALUE) {
#else
    temp.handle =
        ::open(temp.path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
               S_IRUSR | S_IWUSR);
    if (temp.handle != -1) {
#endif
      temp.owned = true;
      return true;
    }
    error = nativeError();
    if (error != std::errc::file_exists)
      return false;
  }
  error = std::make_error_code(std::errc::file_exists);
  return false;
}

} // namespace

bool atomicWriteText(const std::filesystem::path &path, std::string_view text,
                     std::error_code &error) {
  error.clear();
  // Native APIs treat an embedded NUL as a terminator, unlike path/string.
  if (path.empty() || path.filename().empty() ||
      path.native().find(std::filesystem::path::value_type{}) !=
          std::filesystem::path::string_type::npos) {
    error = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  const auto status = std::filesystem::symlink_status(path, error);
  if (error && error != std::errc::no_such_file_or_directory)
    return false;
  error.clear();
  if (std::filesystem::exists(status) &&
      !std::filesystem::is_regular_file(status)) {
    error = std::make_error_code(std::filesystem::is_directory(status)
                                     ? std::errc::is_a_directory
                                     : std::errc::invalid_argument);
    return false;
  }
  const auto parent =
      path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
  std::filesystem::create_directories(parent, error);
  if (error)
    return false;
  TemporaryFile temp;
#if defined(_WIN32)
  if (std::filesystem::exists(status)) {
    DWORD size = 0;
    GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0,
                     &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      error = nativeError();
      return false;
    }
    temp.security.resize(size);
    if (!GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION,
                          temp.security.data(), size, &size) ||
        !SetSecurityDescriptorControl(temp.security.data(), SE_DACL_PROTECTED,
                                      SE_DACL_PROTECTED)) {
      error = nativeError();
      return false;
    }
  }
#endif
  if (!reserveTemporary(parent, temp, error))
    return false;
  error.clear();
  while (!text.empty()) {
    const auto count = std::min<std::size_t>(text.size(), 1024 * 1024);
#if defined(_WIN32)
    DWORD written = 0;
    if (!WriteFile(temp.handle, text.data(), static_cast<DWORD>(count),
                   &written, nullptr)) {
#else
    const auto written = ::write(temp.handle, text.data(), count);
    if (written < 0) {
      if (errno == EINTR)
        continue;
#endif
      error = nativeError();
      return false;
    }
    if (written == 0) {
      error = std::make_error_code(std::errc::io_error);
      return false;
    }
    text.remove_prefix(static_cast<std::size_t>(written));
  }
#if defined(_WIN32)
  if (!FlushFileBuffers(temp.handle)) {
    error = nativeError();
    return false;
  }
#else
  if (std::filesystem::exists(status) &&
      ::fchmod(temp.handle, static_cast<mode_t>(status.permissions()) & 0777) !=
          0) {
    error = nativeError();
    return false;
  }
  int synced;
  do {
    synced = ::fsync(temp.handle);
  } while (synced != 0 && errno == EINTR);
  if (synced != 0) {
    error = nativeError();
    return false;
  }
#endif
  if (!temp.close(error))
    return false;
#if defined(_WIN32)
  // Same-directory replacement only: never permit a copy/delete fallback.
  if (!MoveFileExW(temp.path.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    error = nativeError();
    return false;
  }
#else
  if (::rename(temp.path.c_str(), path.c_str()) != 0) {
    error = nativeError();
    return false;
  }
#endif
  temp.owned = false;
  return true;
}

} // namespace demi
