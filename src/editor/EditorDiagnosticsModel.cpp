#include "editor/EditorDiagnosticsModel.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace demi::editor {
namespace {

bool containsCaseInsensitive(const std::string_view value,
                             const std::string_view query) {
  if (query.empty())
    return true;
  std::string haystack(value);
  std::string needle(query);
  const auto lower = [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  };
  std::ranges::transform(haystack, haystack.begin(), lower);
  std::ranges::transform(needle, needle.begin(), lower);
  return haystack.find(needle) != std::string::npos;
}

EditorDiagnosticRecord makeRecord(Diagnostic diagnostic,
                                  const EditorDiagnosticOrigin origin) {
  EditorDiagnosticRecord result{.diagnostic = std::move(diagnostic),
                                .origin = origin};
  if (result.diagnostic.code == "SCENE_UNKNOWN_COMPONENT" ||
      result.diagnostic.code == "SCENE_INVALID_COMPONENT_FIELD") {
    constexpr std::string_view prefix = "Entity ";
    if (result.diagnostic.message.starts_with(prefix)) {
      const std::size_t end =
          result.diagnostic.message.find_first_of(", ", prefix.size());
      result.entityId =
          result.diagnostic.message.substr(prefix.size(), end - prefix.size());
    }
  }
  constexpr std::string_view componentMarker = ", component ";
  if (const std::size_t begin = result.diagnostic.message.find(componentMarker);
      begin != std::string::npos) {
    const std::size_t tokenBegin = begin + componentMarker.size();
    const std::size_t tokenEnd =
        result.diagnostic.message.find(' ', tokenBegin);
    std::string token =
        result.diagnostic.message.substr(tokenBegin, tokenEnd - tokenBegin);
    if (const std::size_t dot = token.find('.'); dot != std::string::npos) {
      result.component = token.substr(0, dot);
      result.field = token.substr(dot + 1);
    } else {
      result.component = std::move(token);
    }
  } else if (const std::size_t begin =
                 result.diagnostic.message.find("unknown component: ");
             begin != std::string::npos) {
    result.component = result.diagnostic.message.substr(begin + 19);
  }
  return result;
}

bool matches(const EditorDiagnosticRecord &record,
             const std::string_view query) {
  return containsCaseInsensitive(record.diagnostic.code, query) ||
         containsCaseInsensitive(record.diagnostic.message, query) ||
         containsCaseInsensitive(record.diagnostic.path, query) ||
         containsCaseInsensitive(record.entityId, query) ||
         containsCaseInsensitive(record.component, query) ||
         containsCaseInsensitive(record.field, query);
}

} // namespace

std::vector<EditorDiagnosticRecord>
collectEditorDiagnostics(const Diagnostics &project, const Diagnostics &build) {
  std::vector<EditorDiagnosticRecord> result;
  result.reserve(project.size() + build.size());
  for (const Diagnostic &diagnostic : project)
    result.push_back(makeRecord(diagnostic, EditorDiagnosticOrigin::Project));
  for (const Diagnostic &diagnostic : build)
    result.push_back(makeRecord(diagnostic, EditorDiagnosticOrigin::Build));
  return result;
}

std::vector<EditorDiagnosticRecord>
filterEditorDiagnostics(const std::vector<EditorDiagnosticRecord> &records,
                        const std::string_view query, const bool showInfo,
                        const bool showWarnings, const bool showErrors) {
  std::vector<EditorDiagnosticRecord> result;
  for (const EditorDiagnosticRecord &record : records) {
    const bool severity =
        (record.diagnostic.severity == Severity::Info && showInfo) ||
        (record.diagnostic.severity == Severity::Warning && showWarnings) ||
        (record.diagnostic.severity == Severity::Error && showErrors);
    if (severity && matches(record, query))
      result.push_back(record);
  }
  return result;
}

} // namespace demi::editor
