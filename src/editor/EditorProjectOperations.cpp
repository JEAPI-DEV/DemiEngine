#include "editor/EditorProjectOperations.h"

namespace demi::editor {

EditorProjectOperations::~EditorProjectOperations() {
  cancel();
  if (worker_.joinable())
    worker_.join();
}

bool EditorProjectOperations::start(build::ProjectOperationRequest request,
                                    std::string &error) {
  std::vector<build::ProjectOperationRequest> requests;
  requests.push_back(std::move(request));
  return start(std::move(requests), error);
}

bool EditorProjectOperations::start(
    std::vector<build::ProjectOperationRequest> requests, std::string &error) {
  if (requests.empty()) {
    error = "Select at least one project operation.";
    return false;
  }
  {
    std::scoped_lock lock(mutex_);
    if (state_.running) {
      error = "A project operation is already running.";
      return false;
    }
  }
  if (worker_.joinable())
    worker_.join();
  isCancelled_ = false;
  {
    std::scoped_lock lock(mutex_);
    state_ = {.generation = nextGeneration_++,
              .running = true,
              .operation = requests.front().operation,
              .progress = {.stage = build::ProjectOperationStage::Validate,
                           .fraction = 0.0F,
                           .message = "Starting"}};
  }
  worker_ = std::jthread([this, requests = std::move(requests)]() mutable {
    build::ProjectOperationResult result;
    result.stage = build::ProjectOperationStage::Complete;
    for (std::size_t index = 0; index < requests.size(); ++index) {
      auto &request = requests[index];
      request.isCancelled = [this] { return isCancelled_.load(); };
      request.reportProgress = [this, index, count = requests.size(),
                                operation =
                                    request.operation](const auto &progress) {
        std::scoped_lock lock(mutex_);
        state_.operation = operation;
        state_.progress = progress;
        state_.progress.fraction =
            (static_cast<float>(index) + progress.fraction) /
            static_cast<float>(count);
      };
      build::ProjectOperationResult current =
          build::runProjectOperation(request);
      result.diagnostics.insert(result.diagnostics.end(),
                                current.diagnostics.begin(),
                                current.diagnostics.end());
      result.artifact = current.artifact;
      result.stage = current.stage;
      if (!current.succeeded())
        break;
    }
    std::scoped_lock lock(mutex_);
    state_.running = false;
    state_.progress.stage = result.stage;
    state_.progress.fraction =
        result.succeeded() ? 1.0F : state_.progress.fraction;
    state_.progress.message =
        std::string(build::projectOperationStageName(result.stage));
    state_.result = std::move(result);
  });
  return true;
}

void EditorProjectOperations::cancel() { isCancelled_ = true; }

EditorProjectOperationSnapshot EditorProjectOperations::snapshot() const {
  std::scoped_lock lock(mutex_);
  return state_;
}

} // namespace demi::editor
