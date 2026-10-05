#pragma once

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::editor {

using EditorClipboardIdMap = std::unordered_map<std::string, std::string>;

// Clipboard subtrees keep authored nesting. Only durable IDs and typed native
// references are rewritten; gameplay strings and resource URIs remain intact.
[[nodiscard]] bool collectClipboardTreeIds(const nlohmann::json &roots,
                                           std::vector<std::string> &ids,
                                           std::string &error);
[[nodiscard]] EditorClipboardIdMap
allocateClipboardIds(const std::vector<std::string> &ids,
                     std::set<std::string> reserved);
void remapClipboardEntities(nlohmann::json &roots,
                            const EditorClipboardIdMap &ids);
void remapClipboardHudNodes(nlohmann::json &roots,
                            const EditorClipboardIdMap &ids);

} // namespace demi::editor
