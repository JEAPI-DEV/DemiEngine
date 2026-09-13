#include "demi/runtime/scripting/ScriptComponentMetadata.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <ranges>
#include <regex>
#include <sstream>
#include <utility>
#include <vector>

namespace demi::runtime {
namespace {

std::string trim(std::string value) {
  const auto content = [](const unsigned char character) {
    return !std::isspace(character);
  };
  value.erase(value.begin(), std::ranges::find_if(value, content));
  value.erase(std::ranges::find_if(value | std::views::reverse, content).base(),
              value.end());
  return value;
}

std::optional<std::string> annotation(const std::string &line,
                                      const std::string_view name) {
  const std::string prefix = "---@" + std::string(name);
  const std::string value = trim(line);
  if (!value.starts_with(prefix) ||
      (value.size() > prefix.size() &&
       !std::isspace(static_cast<unsigned char>(value[prefix.size()]))))
    return std::nullopt;
  return trim(value.substr(prefix.size()));
}

std::string inferredDisplayName(const std::filesystem::path &path) {
  std::string value = path.stem().string();
  bool uppercase = true;
  for (char &character : value) {
    if (character == '_' || character == '-') {
      character = ' ';
      uppercase = true;
    } else if (uppercase) {
      character = static_cast<char>(
          std::toupper(static_cast<unsigned char>(character)));
      uppercase = false;
    }
  }
  return value;
}

std::optional<nlohmann::json> parseDefault(const std::string &expression,
                                           std::string &type) {
  const std::string value = trim(expression.substr(0, expression.find("--")));
  if (value == "true" || value == "false") {
    if (type.empty())
      type = "boolean";
    return value == "true";
  }
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    if (type.empty())
      type = "string";
    return value.substr(1, value.size() - 2);
  }
  if (value.starts_with('{') && value.ends_with('}')) {
    nlohmann::json result = nlohmann::json::array();
    std::stringstream values(value.substr(1, value.size() - 2));
    std::string item;
    try {
      while (std::getline(values, item, ','))
        result.push_back(std::stod(trim(item)));
    } catch (const std::exception &) {
      return std::nullopt;
    }
    if (type.empty()) {
      if (result.size() == 2)
        type = "vec2";
      else if (result.size() == 3)
        type = "vec3";
      else if (result.size() == 4)
        type = "color";
      else
        type = "array";
    }
    return result;
  }
  try {
    std::size_t consumed = 0;
    const double number = std::stod(value, &consumed);
    if (consumed != value.size())
      return std::nullopt;
    if (type.empty())
      type = "number";
    return number;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

std::vector<std::string> commaSeparated(const std::string &value) {
  std::vector<std::string> result;
  std::stringstream values(value);
  std::string item;
  while (std::getline(values, item, ','))
    if (std::string cleaned = trim(item); !cleaned.empty())
      result.push_back(std::move(cleaned));
  return result;
}

} // namespace

std::optional<ScriptComponentMetadata>
parseScriptComponentMetadata(const std::filesystem::path &path,
                             std::string &error) {
  error.clear();
  std::ifstream input(path);
  if (!input)
    return std::nullopt;
  ScriptComponentMetadata metadata{.displayName = inferredDisplayName(path),
                                   .category = "Scripting",
                                   .description = {},
                                   .propertySchema = nlohmann::json::object()};
  bool declared = false;
  bool pendingProperty = false;
  std::string propertyType;
  std::string propertyLabel;
  std::string propertyDescription;
  std::optional<std::pair<double, double>> propertyRange;
  std::vector<std::string> propertyOptions;
  const std::regex assignment(
      R"(^\s*[A-Za-z_][A-Za-z0-9_]*\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?)\s*$)");
  for (std::string line; std::getline(input, line);) {
    if (annotation(line, "demi_component")) {
      declared = true;
      continue;
    }
    if (const auto value = annotation(line, "display_name")) {
      metadata.displayName = *value;
      continue;
    }
    if (const auto value = annotation(line, "category")) {
      metadata.category = *value;
      continue;
    }
    if (const auto value = annotation(line, "description")) {
      if (pendingProperty)
        propertyDescription = *value;
      else
        metadata.description = *value;
      continue;
    }
    if (const auto value = annotation(line, "demi_property")) {
      pendingProperty = true;
      propertyType = *value;
      propertyLabel.clear();
      propertyDescription.clear();
      propertyRange.reset();
      propertyOptions.clear();
      continue;
    }
    // Compact form: ---@prop number(0,20) Move Speed desugars to the
    // demi_property/label/range triple. ---@prop Type Label... with an
    // optional (min,max) range; label is everything after the type token.
    if (const auto value = annotation(line, "prop")) {
      pendingProperty = true;
      propertyLabel.clear();
      propertyDescription.clear();
      propertyRange.reset();
      propertyOptions.clear();
      std::string rest = trim(*value);
      std::string typeToken;
      const std::size_t space = rest.find_first_of(" \t");
      if (space == std::string::npos) {
        typeToken = rest;
        rest.clear();
      } else {
        typeToken = rest.substr(0, space);
        rest = trim(rest.substr(space + 1));
      }
      const std::size_t paren = typeToken.find('(');
      if (paren != std::string::npos) {
        const std::size_t close = typeToken.find(')', paren);
        if (close != std::string::npos) {
          std::string rangeText = typeToken.substr(paren + 1, close - paren - 1);
          std::replace(rangeText.begin(), rangeText.end(), ',', ' ');
          std::stringstream range(rangeText);
          double minimum = 0.0;
          double maximum = 0.0;
          if (!(range >> minimum >> maximum)) {
            error = "@prop expects (min max) numbers.";
            return std::nullopt;
          }
          propertyRange = {minimum, maximum};
        }
        typeToken = typeToken.substr(0, paren);
      }
      propertyType = trim(typeToken);
      propertyLabel = rest;
      continue;
    }
    if (const auto value = annotation(line, "type"); value && pendingProperty) {
      propertyType = *value;
      continue;
    }
    if (const auto value = annotation(line, "label");
        value && pendingProperty) {
      propertyLabel = *value;
      continue;
    }
    if (const auto value = annotation(line, "range");
        value && pendingProperty) {
      std::stringstream range(*value);
      double minimum = 0.0;
      double maximum = 0.0;
      if (!(range >> minimum >> maximum)) {
        error = "@range expects minimum and maximum numbers.";
        return std::nullopt;
      }
      propertyRange = {minimum, maximum};
      continue;
    }
    if (const auto value = annotation(line, "options");
        value && pendingProperty) {
      propertyOptions = commaSeparated(*value);
      propertyType = "enum";
      continue;
    }
    if (!pendingProperty)
      continue;
    std::smatch match;
    if (!std::regex_match(line, match, assignment)) {
      if (!trim(line).empty() && !trim(line).starts_with("---")) {
        error = "@demi_property must be followed by a table field assignment.";
        return std::nullopt;
      }
      continue;
    }
    const std::string name = match[1].str();
    auto defaultValue = parseDefault(match[2].str(), propertyType);
    if (!defaultValue || propertyType.empty()) {
      error = "Could not infer a supported default for property '" + name +
              "'. Add an explicit type after @demi_property.";
      return std::nullopt;
    }
    nlohmann::json definition{{"type", propertyType},
                              {"default", std::move(*defaultValue)}};
    if (!propertyLabel.empty())
      definition["label"] = propertyLabel;
    if (!propertyDescription.empty())
      definition["description"] = propertyDescription;
    if (propertyRange) {
      definition["minimum"] = propertyRange->first;
      definition["maximum"] = propertyRange->second;
    }
    if (!propertyOptions.empty())
      definition["values"] = propertyOptions;
    metadata.propertySchema[name] = std::move(definition);
    pendingProperty = false;
  }
  if (!declared)
    return std::nullopt;
  if (pendingProperty) {
    error = "The final @demi_property has no table field assignment.";
    return std::nullopt;
  }
  return metadata;
}

} // namespace demi::runtime
