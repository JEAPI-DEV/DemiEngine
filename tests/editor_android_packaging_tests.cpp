#include "editor/EditorProjectOperations.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

int main() {
  namespace fs = std::filesystem;
  using namespace std::chrono_literals;

  demi::editor::EditorProjectOperations operations;
  std::string error;
  assert(operations.start(
      {.operation = demi::build::ProjectOperation::PackageAndroid,
       .projectFile = fs::path(DEMI_SOURCE_DIR) /
                      "examples/main_menu_lang/demi.project.json",
       .engineRoot = DEMI_SOURCE_DIR},
      error));

  const auto deadline = std::chrono::steady_clock::now() + 90s;
  demi::editor::EditorProjectOperationSnapshot snapshot;
  do {
    std::this_thread::sleep_for(50ms);
    snapshot = operations.snapshot();
    if (std::chrono::steady_clock::now() >= deadline) {
      operations.cancel();
      assert(false && "Editor Android packaging did not complete in 90s");
    }
  } while (snapshot.running);

  assert(snapshot.result && snapshot.result->succeeded());
  assert(snapshot.progress.fraction == 1.0F);
  assert(fs::is_regular_file(snapshot.result->artifact));
  assert(snapshot.result->artifact ==
         fs::path(DEMI_SOURCE_DIR) /
             "examples/main_menu_lang/build/android/main_menu_lang-debug.apk");
  return 0;
}
