#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>

namespace demi::editor {
// Pure source mutations. Callers stage/validate through the document history.
void eraseEntitySubtrees(nlohmann::json &entities,
                         const std::unordered_set<std::string> &ids);
void insertEntityUnder(nlohmann::json &document, nlohmann::json entity,
                       const std::string &parent);
// Nest local links in a new copied hierarchy, leaving external parents and
// existing nested subtrees intact. Does not rewrite the original source.
nlohmann::json nestLocalEntityLinks(nlohmann::json entities);
} // namespace demi::editor
