#include "demi/assets/AssetCookGraph.h"

#include "demi/assets/AssetHash.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <span>

namespace demi::assets {
namespace {

std::string hashText(const std::string &text) {
  return hashBytes(std::span(
      reinterpret_cast<const unsigned char *>(text.data()), text.size()));
}

void error(Diagnostics *diagnostics, std::string code, std::string message,
           std::string path) {
  if (diagnostics != nullptr)
    diagnostics->push_back({.severity = Severity::Error,
                            .code = std::move(code),
                            .message = std::move(message),
                            .path = std::move(path)});
}

std::string safeName(std::string value) {
  for (char &character : value)
    if (!std::isalnum(static_cast<unsigned char>(character)) &&
        character != '-' && character != '_')
      character = '_';
  return value;
}

} // namespace

bool AssetCookGraph::addNode(AssetCookNode node, Diagnostics *diagnostics) {
  if (node.assetId.empty() || node.importer.empty() ||
      node.importerVersion < 1 || node.settingsSchemaVersion < 1 ||
      node.platform.empty()) {
    error(diagnostics, "COOK_GRAPH_NODE_INVALID",
          "Cook nodes require an asset ID, importer, positive versions, and "
          "target platform.",
          node.assetId);
    return false;
  }
  try {
    const auto settings = nlohmann::json::parse(node.normalizedSettings);
    node.normalizedSettings = settings.dump();
  } catch (const nlohmann::json::exception &exception) {
    error(diagnostics, "COOK_GRAPH_SETTINGS_INVALID", exception.what(),
          node.assetId);
    return false;
  }
  std::ranges::sort(node.sourceHashes);
  std::ranges::sort(node.dependencies);
  if (!nodes_.emplace(node.assetId, std::move(node)).second) {
    error(diagnostics, "COOK_GRAPH_DUPLICATE_ASSET",
          "Cook graph contains the same stable ID more than once.",
          node.assetId);
    return false;
  }
  keys_.clear();
  return true;
}

std::optional<std::string>
AssetCookGraph::calculateKey(const std::string &assetId,
                             std::set<std::string> &visiting,
                             Diagnostics *diagnostics) {
  if (const auto found = keys_.find(assetId); found != keys_.end())
    return found->second;
  const auto found = nodes_.find(assetId);
  if (found == nodes_.end()) {
    error(diagnostics, "COOK_GRAPH_DEPENDENCY_MISSING",
          "Cook graph dependency is missing: " + assetId, assetId);
    return std::nullopt;
  }
  if (!visiting.insert(assetId).second) {
    error(diagnostics, "COOK_GRAPH_CYCLE",
          "Cook graph dependency cycle includes " + assetId, assetId);
    return std::nullopt;
  }
  nlohmann::json dependencyKeys = nlohmann::json::array();
  for (const std::string &dependency : found->second.dependencies) {
    const auto dependencyKey = calculateKey(dependency, visiting, diagnostics);
    if (!dependencyKey)
      return std::nullopt;
    dependencyKeys.push_back({{"id", dependency}, {"key", *dependencyKey}});
    reverseEdges_[dependency].insert(assetId);
  }
  visiting.erase(assetId);
  const auto &node = found->second;
  const nlohmann::json keyDocument{
      {"asset", node.assetId},
      {"importer", node.importer},
      {"importer_version", node.importerVersion},
      {"settings_schema_version", node.settingsSchemaVersion},
      {"settings", nlohmann::json::parse(node.normalizedSettings)},
      {"source_hashes", node.sourceHashes},
      {"platform", node.platform},
      {"profile", node.profile},
      {"source_package", node.sourcePackage},
      {"package_content_hash", node.packageContentHash},
      {"dependencies", std::move(dependencyKeys)}};
  return keys_.emplace(assetId, hashText(keyDocument.dump())).first->second;
}

bool AssetCookGraph::finalize(Diagnostics *diagnostics) {
  keys_.clear();
  reverseEdges_.clear();
  std::set<std::string> visiting;
  for (const auto &[assetId, unused] : nodes_) {
    (void)unused;
    if (!calculateKey(assetId, visiting, diagnostics))
      return false;
  }
  return true;
}

std::optional<std::string>
AssetCookGraph::key(const std::string_view assetId) const {
  const auto found = keys_.find(std::string(assetId));
  return found == keys_.end() ? std::nullopt
                              : std::make_optional(found->second);
}

std::set<std::string>
AssetCookGraph::reverseReachable(const std::set<std::string> &changed) const {
  std::set<std::string> result = changed;
  std::vector<std::string> pending(changed.begin(), changed.end());
  while (!pending.empty()) {
    const std::string current = std::move(pending.back());
    pending.pop_back();
    if (const auto found = reverseEdges_.find(current);
        found != reverseEdges_.end())
      for (const std::string &dependent : found->second)
        if (result.insert(dependent).second)
          pending.push_back(dependent);
  }
  return result;
}

const std::map<std::string, AssetCookNode> &AssetCookGraph::nodes() const {
  return nodes_;
}

AssetCookCache::AssetCookCache(std::filesystem::path directory)
    : directory_(std::move(directory)) {}

AssetCookDecision AssetCookCache::inspect(std::string assetId,
                                          std::string key) const {
  AssetCookDecision result{.assetId = std::move(assetId),
                           .key = std::move(key),
                           .reason = "metadata_missing"};
  const auto metadataPath =
      directory_ / ".cook-cache" / (safeName(result.assetId) + ".json");
  std::ifstream input(metadataPath);
  if (!input)
    return result;
  try {
    nlohmann::json metadata;
    input >> metadata;
    if (metadata.value("key", "") != result.key) {
      result.reason = "key_changed";
      return result;
    }
    for (const auto &entry :
         metadata.value("outputs", nlohmann::json::array())) {
      const auto path = directory_ / entry.value("path", "");
      const auto actual = hashFile(path);
      if (!actual || *actual != entry.value("hash", "")) {
        result.reason = "output_missing_or_corrupt";
        result.outputs.clear();
        return result;
      }
      result.outputs.push_back(path);
    }
    result.isCacheHit = true;
    result.reason = "key_and_outputs_match";
  } catch (const nlohmann::json::exception &) {
    result.reason = "metadata_invalid";
  }
  return result;
}

Diagnostics AssetCookCache::store(const AssetCookDecision &decision,
                                  std::string sourcePackage,
                                  std::string contentHash) const {
  Diagnostics diagnostics;
  nlohmann::json outputs = nlohmann::json::array();
  for (const auto &output : decision.outputs) {
    const auto hash = hashFile(output);
    std::error_code errorCode;
    const auto relative =
        std::filesystem::relative(output, directory_, errorCode);
    if (!hash || errorCode || relative.empty() ||
        relative.string().starts_with("..")) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "COOK_CACHE_OUTPUT_INVALID",
           .message = "Cached output must exist inside the cache directory.",
           .path = output.string()});
      continue;
    }
    outputs.push_back({{"path", relative.generic_string()}, {"hash", *hash}});
  }
  if (hasErrors(diagnostics))
    return diagnostics;
  std::error_code errorCode;
  const auto metadataDirectory = directory_ / ".cook-cache";
  std::filesystem::create_directories(metadataDirectory, errorCode);
  const auto path = metadataDirectory / (safeName(decision.assetId) + ".json");
  const auto lock = path.string() + ".lock";
  if (errorCode || !std::filesystem::create_directory(lock, errorCode)) {
    diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "COOK_CACHE_BUSY",
         .message = errorCode ? errorCode.message()
                              : "Another cook owns this cache entry.",
         .path = path.string()});
    return diagnostics;
  }
  const auto temporary = path.string() + ".tmp";
  std::ofstream output(temporary);
  if (!errorCode && output)
    output << nlohmann::json{{"format_version", 1},
                             {"asset", decision.assetId},
                             {"key", decision.key},
                             {"source_package", sourcePackage},
                             {"content_hash", contentHash},
                             {"outputs", std::move(outputs)}}
                  .dump(2)
           << '\n';
  output.close();
  if (!errorCode)
    std::filesystem::remove(path, errorCode);
  if (!errorCode)
    std::filesystem::rename(temporary, path, errorCode);
  std::error_code unlockError;
  std::filesystem::remove(lock, unlockError);
  if (errorCode || !std::filesystem::is_regular_file(path))
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "COOK_CACHE_METADATA_WRITE_FAILED",
                           .message = errorCode
                                          ? errorCode.message()
                                          : "Could not write cache metadata.",
                           .path = path.string()});
  return diagnostics;
}

} // namespace demi::assets
