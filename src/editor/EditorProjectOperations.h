#pragma once

#include "cli/build/BuildService.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace demi::editor {

struct EditorProjectOperationSnapshot {
  std::uint64_t generation = 0;
  bool running = false;
  build::ProjectOperation operation = build::ProjectOperation::Validate;
  build::ProjectOperationProgress progress;
  std::optional<build::ProjectOperationResult> result;
};

// Owns the single background project workflow allowed by the editor. The
// structured service remains synchronous and reusable; this adapter owns
// cancellation, joining, and thread-safe presentation snapshots.
class EditorProjectOperations {
public:
  EditorProjectOperations() = default;
  ~EditorProjectOperations();
  EditorProjectOperations(const EditorProjectOperations &) = delete;
  EditorProjectOperations &operator=(const EditorProjectOperations &) = delete;

  [[nodiscard]] bool start(build::ProjectOperationRequest request,
                           std::string &error);
  [[nodiscard]] bool start(std::vector<build::ProjectOperationRequest> requests,
                           std::string &error);
  void cancel();
  [[nodiscard]] EditorProjectOperationSnapshot snapshot() const;

private:
  mutable std::mutex mutex_;
  std::jthread worker_;
  std::atomic_bool isCancelled_ = false;
  EditorProjectOperationSnapshot state_;
  std::uint64_t nextGeneration_ = 1;
};

} // namespace demi::editor
