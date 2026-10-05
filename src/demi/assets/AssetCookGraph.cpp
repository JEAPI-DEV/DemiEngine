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
  finalizedGraph_ = {};
  return true;
}

bool AssetCookGraph::finalize(Diagnostics *diagnostics) {
  keys_.clear();
  finalizedGraph_ = {};
  graph::DependencyGraph graph;
  for (const auto &[assetId, unused] : nodes_) {
    (void)unused;
    (void)graph.addNode(assetId);
  }
  for (const auto &[assetId, node] : nodes_)
    for (const auto &dependency : node.dependencies)
      if (!graph.addDependency(assetId, dependency)) {
        error(diagnostics, "COOK_GRAPH_DEPENDENCY_MISSING",
              "Cook graph dependency is missing: " + dependency, dependency);
        return false;
      }

  const auto order = graph.topologicalOrder();
  if (order.blockedNode) {
    error(diagnostics, "COOK_GRAPH_CYCLE",
          "Cook graph dependency cycle blocks " + *order.blockedNode,
          *order.blockedNode);
    return false;
  }

  std::map<std::string, std::string> calculatedKeys;
  for (const auto &assetId : order.nodes) {
    const auto &node = nodes_.at(assetId);
    nlohmann::json dependencyKeys = nlohmann::json::array();
    for (const auto &dependency : node.dependencies)
      dependencyKeys.push_back(
          {{"id", dependency}, {"key", calculatedKeys.at(dependency)}});
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
    calculatedKeys.emplace(assetId, hashText(keyDocument.dump()));
  }
  keys_ = std::move(calculatedKeys);
  finalizedGraph_ = std::move(graph);
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
  return finalizedGraph_.dependentsReachable(changed);
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
