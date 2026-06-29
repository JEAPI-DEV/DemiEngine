#include "demi/diagnostics/Diagnostic.h"

#include <ostream>

namespace demi {

const char* toString(const Severity severity) {
  switch (severity) {
  case Severity::Info:
    return "info";
  case Severity::Warning:
    return "warning";
  case Severity::Error:
    return "error";
  }
  return "unknown";
}

bool hasErrors(const Diagnostics& diagnostics) {
  for (const Diagnostic& diagnostic : diagnostics) {
    if (diagnostic.severity == Severity::Error) {
      return true;
    }
  }
  return false;
}

namespace {

void printJsonString(std::ostream& out, const std::string& value) {
  out << '"';
  for (const char c : value) {
    switch (c) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      out << c;
      break;
    }
  }
  out << '"';
}

} // namespace

void printDiagnosticsText(std::ostream& out, const Diagnostics& diagnostics) {
  if (diagnostics.empty()) {
    out << "No diagnostics.\n";
    return;
  }

  for (const Diagnostic& diagnostic : diagnostics) {
    out << toString(diagnostic.severity) << " " << diagnostic.code << ": " << diagnostic.message;
    if (!diagnostic.path.empty()) {
      out << " (" << diagnostic.path;
      if (diagnostic.line > 0) {
        out << ':' << diagnostic.line;
        if (diagnostic.column > 0) {
          out << ':' << diagnostic.column;
        }
      }
      out << ')';
    }
    out << '\n';

    if (!diagnostic.suggestion.empty()) {
      out << "  suggestion: " << diagnostic.suggestion << '\n';
    }
  }
}

void printDiagnosticsJson(std::ostream& out, const Diagnostics& diagnostics) {
  out << "{\"diagnostics\":[";
  for (std::size_t i = 0; i < diagnostics.size(); ++i) {
    const Diagnostic& diagnostic = diagnostics[i];
    if (i != 0) {
      out << ',';
    }
    out << '{';
    out << "\"severity\":";
    printJsonString(out, toString(diagnostic.severity));
    out << ",\"code\":";
    printJsonString(out, diagnostic.code);
    out << ",\"message\":";
    printJsonString(out, diagnostic.message);
    out << ",\"path\":";
    printJsonString(out, diagnostic.path);
    out << ",\"line\":" << diagnostic.line;
    out << ",\"column\":" << diagnostic.column;
    out << ",\"suggestion\":";
    printJsonString(out, diagnostic.suggestion);
    out << '}';
  }
  out << "]}\n";
}

} // namespace demi
