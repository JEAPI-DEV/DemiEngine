#include "demi/assets/YamlDataDocument.h"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <charconv>
#include <cstdlib>
#include <set>

namespace demi::assets {
namespace {

using Json = nlohmann::json;

void error(Diagnostics &diagnostics, std::string code, std::string message,
           const std::filesystem::path &path) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string(),
                         .suggestion = {}});
}

Json scalar(const YAML::Node &node) {
  const std::string value = node.Scalar();
  // yaml-cpp marks explicitly quoted scalars with the non-specific string tag.
  if (node.Tag() == "!")
    return value;
  if (value == "null" || value == "Null" || value == "NULL" || value == "~")
    return nullptr;
  if (value == "true" || value == "True" || value == "TRUE")
    return true;
  if (value == "false" || value == "False" || value == "FALSE")
    return false;
  std::int64_t integer = 0;
  const auto integerResult =
      std::from_chars(value.data(), value.data() + value.size(), integer);
  if (integerResult.ec == std::errc{} &&
      integerResult.ptr == value.data() + value.size())
    return integer;
  char *end = nullptr;
  const double number = std::strtod(value.c_str(), &end);
  if (end == value.c_str() + value.size() && end != value.c_str())
    return number;
  return value;
}

std::optional<Json> convert(const YAML::Node &node, const std::size_t depth,
                            const DataDocumentLimits &limits,
                            std::size_t &elements, Diagnostics &diagnostics,
                            const std::filesystem::path &path) {
  if (depth > limits.maximumDepth) {
    error(diagnostics, "YAML_DOCUMENT_DEPTH_EXCEEDED",
          "YAML exceeds the configured nesting limit.", path);
    return std::nullopt;
  }
  if (++elements > limits.maximumElements) {
    error(diagnostics, "YAML_DOCUMENT_ELEMENTS_EXCEEDED",
          "YAML exceeds the configured element limit.", path);
    return std::nullopt;
  }
  if (!node || node.IsNull())
    return Json(nullptr);
  if (node.IsScalar())
    return scalar(node);
  if (node.IsSequence()) {
    Json result = Json::array();
    for (const YAML::Node &child : node) {
      auto converted =
          convert(child, depth + 1, limits, elements, diagnostics, path);
      if (!converted)
        return std::nullopt;
      result.push_back(std::move(*converted));
    }
    return result;
  }
  if (node.IsMap()) {
    Json result = Json::object();
    std::set<std::string> keys;
    for (const auto &entry : node) {
      if (!entry.first.IsScalar()) {
        error(diagnostics, "YAML_MAPPING_KEY_INVALID",
              "YAML mapping keys must be scalar strings.", path);
        return std::nullopt;
      }
      const std::string key = entry.first.Scalar();
      if (!keys.insert(key).second) {
        error(diagnostics, "YAML_MAPPING_KEY_DUPLICATE",
              "YAML contains a duplicate mapping key: " + key, path);
        return std::nullopt;
      }
      auto converted =
          convert(entry.second, depth + 1, limits, elements, diagnostics, path);
      if (!converted)
        return std::nullopt;
      result[key] = std::move(*converted);
    }
    return result;
  }
  error(diagnostics, "YAML_VALUE_UNSUPPORTED",
        "YAML contains an unsupported node type.", path);
  return std::nullopt;
}

} // namespace

DataDocumentResult parseYamlDataDocument(const std::string_view text,
                                         std::filesystem::path sourcePath,
                                         const DataDocumentLimits &limits) {
  DataDocumentResult result;
  if (text.size() > limits.maximumBytes) {
    error(result.diagnostics, "YAML_DOCUMENT_TOO_LARGE",
          "YAML exceeds the configured byte limit.", sourcePath);
    return result;
  }
  try {
    const YAML::Node root = YAML::Load(std::string(text));
    std::size_t elements = 0;
    auto json =
        convert(root, 0, limits, elements, result.diagnostics, sourcePath);
    if (!json)
      return result;
    return parseDataDocument(json->dump(), std::move(sourcePath), limits);
  } catch (const YAML::Exception &exception) {
    error(result.diagnostics, "YAML_DOCUMENT_INVALID", exception.what(),
          sourcePath);
    return result;
  }
}

} // namespace demi::assets
