#include "demi/assets/ModelLodPublication.h"

#include <stdexcept>

namespace demi::assets {

bool publishModelLods(const std::vector<ModelLodReplacement> &replacements,
                      const std::filesystem::path &workspace,
                      std::string &error) {
  struct Installed {
    std::filesystem::path destination;
    std::filesystem::path backup;
    bool previous = false;
    bool replacement = false;
  };
  std::vector<Installed> installed;
  bool workspaceCleanupIsSafe = true;
  try {
    const auto containsPath = [](const std::filesystem::path &parent,
                                 const std::filesystem::path &child) {
      auto parentPart = parent.begin();
      auto childPart = child.begin();
      while (parentPart != parent.end() && childPart != child.end() &&
             *parentPart == *childPart) {
        ++parentPart;
        ++childPart;
      }
      return parentPart == parent.end();
    };
    const auto overlaps = [&](const std::filesystem::path &left,
                              const std::filesystem::path &right) {
      return containsPath(left, right) || containsPath(right, left);
    };
    std::vector<std::filesystem::path> destinations;
    std::vector<std::filesystem::path> stagedPaths;
    const auto canonicalWorkspace =
        std::filesystem::weakly_canonical(workspace);
    installed.reserve(replacements.size());
    destinations.reserve(replacements.size());
    stagedPaths.reserve(replacements.size());

    // Validate the whole transaction before moving even the first destination.
    for (std::size_t index = 0; index < replacements.size(); ++index) {
      const auto &replacement = replacements[index];
      const auto stagedStatus =
          std::filesystem::symlink_status(replacement.staged);
      const auto destinationStatus =
          std::filesystem::symlink_status(replacement.destination);
      const bool stagedIsFile = std::filesystem::is_regular_file(stagedStatus);
      const bool stagedIsDirectory =
          std::filesystem::is_directory(stagedStatus);
      if (std::filesystem::is_symlink(stagedStatus) ||
          (!stagedIsFile && !stagedIsDirectory)) {
        throw std::runtime_error(
            "Model LOD publication refuses an unexpected staged source: " +
            replacement.staged.string());
      }
      if (std::filesystem::is_symlink(destinationStatus) ||
          (std::filesystem::exists(destinationStatus) &&
           ((stagedIsFile &&
             !std::filesystem::is_regular_file(destinationStatus)) ||
            (stagedIsDirectory &&
             !std::filesystem::is_directory(destinationStatus))))) {
        throw std::runtime_error(
            "Model LOD publication refuses an incompatible destination: " +
            replacement.destination.string());
      }
      const auto destination =
          std::filesystem::weakly_canonical(replacement.destination);
      const auto staged = std::filesystem::weakly_canonical(replacement.staged);
      if (overlaps(destination, canonicalWorkspace)) {
        workspaceCleanupIsSafe = false;
        throw std::runtime_error("Model LOD publication destination overlaps "
                                 "its recovery workspace: " +
                                 replacement.destination.string());
      }
      for (const auto &previous : destinations) {
        if (overlaps(previous, destination)) {
          throw std::runtime_error(
              "Model LOD publication destinations overlap: " +
              replacement.destination.string());
        }
      }
      for (const auto &previous : stagedPaths) {
        if (overlaps(previous, staged)) {
          throw std::runtime_error(
              "Model LOD publication staged sources overlap: " +
              replacement.staged.string());
        }
      }
      const auto backup = workspace / ("backup-" + std::to_string(index));
      if (std::filesystem::exists(std::filesystem::symlink_status(backup))) {
        workspaceCleanupIsSafe = false;
        throw std::runtime_error(
            "Model LOD publication backup already exists: " + backup.string());
      }
      destinations.push_back(destination);
      stagedPaths.push_back(staged);
    }
    for (const auto &destination : destinations) {
      for (const auto &staged : stagedPaths) {
        if (overlaps(destination, staged)) {
          throw std::runtime_error(
              "Model LOD publication destination overlaps a staged source: " +
              destination.string());
        }
      }
    }

    for (const auto &replacement : replacements) {
      std::filesystem::create_directories(
          replacement.destination.parent_path());
      installed.push_back(
          {.destination = replacement.destination,
           .backup =
               workspace / ("backup-" + std::to_string(installed.size()))});
      auto &entry = installed.back();
      if (std::filesystem::exists(
              std::filesystem::symlink_status(entry.destination))) {
        std::filesystem::rename(entry.destination, entry.backup);
        entry.previous = true;
      }
      std::filesystem::rename(replacement.staged, entry.destination);
      entry.replacement = true;
    }
    error.clear();
    return true;
  } catch (const std::exception &failure) {
    error = failure.what();
    bool recoveryFailed = false;
    for (auto entry = installed.rbegin(); entry != installed.rend(); ++entry) {
      std::error_code recovery;
      if (entry->replacement) {
        std::filesystem::remove_all(entry->destination, recovery);
      }
      if (!recovery && entry->previous) {
        std::filesystem::rename(entry->backup, entry->destination, recovery);
      }
      if (recovery) {
        recoveryFailed = true;
        error += "; recovery failed for " + entry->destination.string() + ": " +
                 recovery.message();
      }
    }
    if (recoveryFailed) {
      error += ". Backups retained at " + workspace.string();
      return false;
    }
    if (!workspaceCleanupIsSafe) {
      error += ". Recovery workspace retained because its ownership could not "
               "be established: " +
               workspace.string();
      return false;
    }
    std::error_code cleanup;
    std::filesystem::remove_all(workspace, cleanup);
    if (cleanup) {
      error += "; could not remove recovery workspace " + workspace.string() +
               ": " + cleanup.message();
    }
    return false;
  }
}

} // namespace demi::assets
