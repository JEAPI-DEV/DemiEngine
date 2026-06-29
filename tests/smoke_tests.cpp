#include "demi/diagnostics/Diagnostic.h"
#include "demi/schema/Validation.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
  const std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
  const demi::ValidationSummary summary = demi::validatePath(root / "examples");
  if (demi::hasErrors(summary.diagnostics)) {
    demi::printDiagnosticsText(std::cerr, summary.diagnostics);
    return 1;
  }

  std::cout << "Smoke validation checked " << summary.checkedFiles << " file(s).\n";
  return 0;
}
