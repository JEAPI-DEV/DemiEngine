#include "demi/filesystem/AtomicTextFile.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct Fixture {
  fs::path root;
  Fixture() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
      auto candidate = fs::temp_directory_path() /
                       ("demi-atomic-text-test-" + std::to_string(stamp) + "-" +
                        std::to_string(attempt));
      if (fs::create_directory(candidate)) {
        root = std::move(candidate);
        return;
      }
    }
    throw std::runtime_error("Could not reserve test directory");
  }
  ~Fixture() {
    std::error_code ignored;
    if (!root.empty())
      fs::remove_all(root, ignored);
  }
};

std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(input.good(), "Could not read fixture file");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::set<fs::path> entries(const fs::path &root) {
  std::set<fs::path> result;
  for (const auto &entry : fs::recursive_directory_iterator(root))
    result.insert(entry.path());
  return result;
}

void run() {
  Fixture fixture;
  std::error_code error;
  const auto target = fixture.root / "nested" / "schema.json";
  require(demi::atomicWriteText(target, "original\n", error) && !error,
          "Creating a file and missing parent failed");
  require(read(target) == "original\n", "Initial content differs");
  const auto legacyTemporary = fs::path(target.string() + ".tmp");
  require(demi::atomicWriteText(legacyTemporary, "not ours", error),
          "Could not create unrelated temporary file");

#if !defined(_WIN32)
  const auto permissions =
      fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read;
  fs::permissions(target, permissions);
#endif
  const std::string binaryText("complete\0replacement\n", 21);
  require(demi::atomicWriteText(target, binaryText, error) && !error,
          "Replacing an existing file failed");
  require(read(target) == binaryText,
          "Replacement was truncated or translated");
#if !defined(_WIN32)
  require(fs::status(target).permissions() == permissions,
          "Existing permissions changed");
#endif
  require(read(legacyTemporary) == "not ours", "Unowned temporary was changed");

  const auto emptyDirectory = fixture.root / "empty-directory";
  const auto fullDirectory = fixture.root / "full-directory";
  fs::create_directory(emptyDirectory);
  require(demi::atomicWriteText(fullDirectory / "keep", "keep", error),
          "Could not prepare directory fixture");
  const auto before = entries(fixture.root);
  for (const auto &directory : {emptyDirectory, fullDirectory}) {
    require(!demi::atomicWriteText(directory, "bad", error) && error,
            "Directory target was accepted");
    require(fs::is_directory(directory), "Directory target was removed");
  }
  require(read(fullDirectory / "keep") == "keep", "Directory content changed");
  require(!demi::atomicWriteText(target / "child", "bad", error) && error,
          "Regular-file parent was accepted");
  auto nulPath = target.native();
  nulPath.push_back(fs::path::value_type{});
  nulPath += fs::path("suffix").native();
  require(!demi::atomicWriteText(fs::path(nulPath), "bad", error) && error,
          "Embedded NUL was accepted");
  require(read(target) == binaryText,
          "Validation failure changed prior output");
  require(entries(fixture.root) == before, "Validation failure left artifacts");

#if defined(_WIN32)
  // Permit reads but not deletion: writing the sibling succeeds, commit fails.
  const HANDLE lock =
      CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  require(lock != INVALID_HANDLE_VALUE, "Could not lock target");
  const bool replaced =
      demi::atomicWriteText(target, "must not replace", error);
  const bool failedWithError = !replaced && static_cast<bool>(error);
  CloseHandle(lock);
  require(failedWithError, "Replacement of locked target succeeded");
#else
  // A child-local file-size limit forces a partial write followed by EFBIG,
  // even when the test is run as root. Parent limits/signals remain untouched.
  const pid_t child = fork();
  require(child != -1, "Could not fork write-failure probe");
  if (child == 0) {
    struct rlimit limit{};
    if (getrlimit(RLIMIT_FSIZE, &limit) != 0)
      _exit(2);
    limit.rlim_cur = 1024;
    if (setrlimit(RLIMIT_FSIZE, &limit) != 0 ||
        std::signal(SIGXFSZ, SIG_IGN) == SIG_ERR)
      _exit(3);
    std::error_code childError;
    const bool replaced =
        demi::atomicWriteText(target, std::string(8192, 'x'), childError);
    _exit(!replaced && childError ? 0 : 4);
  }
  int status = 0;
  require(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0,
          "Partial-write failure was not reported");
#endif
  require(read(target) == binaryText, "I/O failure changed prior output");
  require(entries(fixture.root) == before, "I/O failure leaked temporary file");
  require(read(legacyTemporary) == "not ours", "Failure removed unowned file");

#if !defined(_WIN32)
  const auto link = fixture.root / "schema-link";
  fs::create_symlink(target, link);
  require(!demi::atomicWriteText(link, "bad", error) && error,
          "Symlink destination was accepted");
  require(fs::is_symlink(link) && read(target) == binaryText,
          "Symlink or its target changed");
#endif
  require(demi::atomicWriteText(target, "", error) && !error,
          "Writing empty content failed");
  require(read(target).empty(), "Empty replacement contains data");
}
} // namespace

int main() {
  try {
    run();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
