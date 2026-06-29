#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace demi {

enum class Severity {
  Info,
  Warning,
  Error,
};

struct Diagnostic {
  Severity severity = Severity::Info;
  std::string code;
  std::string message;
  std::string path;
  int line = 0;
  int column = 0;
  std::string suggestion;
};

using Diagnostics = std::vector<Diagnostic>;

[[nodiscard]] const char* toString(Severity severity);
[[nodiscard]] bool hasErrors(const Diagnostics& diagnostics);

void printDiagnosticsText(std::ostream& out, const Diagnostics& diagnostics);
void printDiagnosticsJson(std::ostream& out, const Diagnostics& diagnostics);

} // namespace demi
