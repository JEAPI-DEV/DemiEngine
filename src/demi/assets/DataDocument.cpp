#include "demi/assets/DataDocument.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace demi::assets {
namespace {

using Json = nlohmann::json;

void addError(Diagnostics &diagnostics, std::string code, std::string message,
              const std::filesystem::path &path,
              const std::string &pointer = {}) {
  diagnostics.push_back(
      {.severity = Severity::Error,
       .code = std::move(code),
       .message = std::move(message),
       .path = pointer.empty() ? path.string() : path.string() + "#" + pointer,
       .suggestion = {}});
}

bool validUtf8(const std::string_view text) {
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t continuation = 0;
    std::uint32_t codepoint = 0;
    if (lead <= 0x7F) {
      ++index;
      continue;
    }
    if ((lead & 0xE0U) == 0xC0U) {
      continuation = 1;
      codepoint = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
      continuation = 2;
      codepoint = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
      continuation = 3;
      codepoint = lead & 0x07U;
    } else {
      return false;
    }
    if (index + continuation >= text.size())
      return false;
    for (std::size_t offset = 1; offset <= continuation; ++offset) {
      const auto byte = static_cast<unsigned char>(text[index + offset]);
      if ((byte & 0xC0U) != 0x80U)
        return false;
      codepoint = (codepoint << 6U) | (byte & 0x3FU);
    }
    const bool overlong = (continuation == 1 && codepoint < 0x80U) ||
                          (continuation == 2 && codepoint < 0x800U) ||
                          (continuation == 3 && codepoint < 0x10000U);
    if (overlong || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
      return false;
    index += continuation + 1;
  }
  return true;
}

std::optional<DataValue>
convert(const Json &input, const DataDocumentLimits &limits, std::size_t depth,
        std::size_t &elements, Diagnostics &diagnostics,
        const std::filesystem::path &path, const std::string &pointer) {
  if (depth > limits.maximumDepth) {
    addError(diagnostics, "DATA_DOCUMENT_DEPTH_EXCEEDED",
             "Data document exceeds the configured nesting limit.", path,
             pointer);
    return std::nullopt;
  }
  if (++elements > limits.maximumElements) {
    addError(diagnostics, "DATA_DOCUMENT_ELEMENTS_EXCEEDED",
             "Data document exceeds the configured element limit.", path,
             pointer);
    return std::nullopt;
  }
  if (input.is_null())
    return DataValue{};
  if (input.is_boolean())
    return DataValue{.value = input.get<bool>()};
  if (input.is_number_unsigned()) {
    const auto value = input.get<std::uint64_t>();
    if (value >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      addError(diagnostics, "DATA_NUMBER_OUT_OF_RANGE",
               "Unsigned integer cannot be represented exactly by Lua.", path,
               pointer);
      return std::nullopt;
    }
    return DataValue{.value = static_cast<std::int64_t>(value)};
  }
  if (input.is_number_integer())
    return DataValue{.value = input.get<std::int64_t>()};
  if (input.is_number_float()) {
    const double value = input.get<double>();
    if (!std::isfinite(value)) {
      addError(diagnostics, "DATA_NUMBER_NON_FINITE",
               "Data numbers must be finite.", path, pointer);
      return std::nullopt;
    }
    return DataValue{.value = value};
  }
  if (input.is_string()) {
    std::string value = input.get<std::string>();
    if (value.size() > limits.maximumStringBytes) {
      addError(diagnostics, "DATA_STRING_TOO_LARGE",
               "Data string exceeds the configured byte limit.", path, pointer);
      return std::nullopt;
    }
    return DataValue{.value = std::move(value)};
  }
  if (input.is_array()) {
    DataValue::Array result;
    result.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      auto child =
          convert(input[index], limits, depth + 1, elements, diagnostics, path,
                  pointer + "/" + std::to_string(index));
      if (!child)
        return std::nullopt;
      result.push_back(std::move(*child));
    }
    return DataValue{.value = std::move(result)};
  }
  if (input.is_object()) {
    DataValue::Object result;
    for (const auto &[key, value] : input.items()) {
      auto child = convert(value, limits, depth + 1, elements, diagnostics,
                           path, pointer + "/" + key);
      if (!child)
        return std::nullopt;
      result.emplace(key, std::move(*child));
    }
    return DataValue{.value = std::move(result)};
  }
  addError(diagnostics, "DATA_VALUE_UNSUPPORTED",
           "Data document contains an unsupported JSON value.", path, pointer);
  return std::nullopt;
}

} // namespace

bool DataValue::isNull() const {
  return std::holds_alternative<std::monostate>(value);
}
bool DataValue::isBoolean() const {
  return std::holds_alternative<bool>(value);
}
bool DataValue::isInteger() const {
  return std::holds_alternative<std::int64_t>(value);
}
bool DataValue::isNumber() const {
  return isInteger() || std::holds_alternative<double>(value);
}
bool DataValue::isString() const {
  return std::holds_alternative<std::string>(value);
}
bool DataValue::isArray() const { return std::holds_alternative<Array>(value); }
bool DataValue::isObject() const {
  return std::holds_alternative<Object>(value);
}
const DataValue::Array *DataValue::array() const {
  return std::get_if<Array>(&value);
}
const DataValue::Object *DataValue::object() const {
  return std::get_if<Object>(&value);
}
const DataValue *DataValue::find(const std::string_view key) const {
  const Object *values = object();
  if (values == nullptr)
    return nullptr;
  const auto found = values->find(key);
  return found == values->end() ? nullptr : &found->second;
}

DataDocument::DataDocument(DataValue root, const std::size_t byteSize,
                           const std::size_t elementCount)
    : root_(std::move(root)), byteSize_(byteSize), elementCount_(elementCount) {
}
const DataValue &DataDocument::root() const { return root_; }
std::size_t DataDocument::byteSize() const { return byteSize_; }
std::size_t DataDocument::elementCount() const { return elementCount_; }

DataDocumentResult parseDataDocument(const std::string_view text,
                                     std::filesystem::path sourcePath,
                                     const DataDocumentLimits &limits) {
  DataDocumentResult result;
  if (text.size() > limits.maximumBytes) {
    addError(result.diagnostics, "DATA_DOCUMENT_TOO_LARGE",
             "Data document exceeds the configured byte limit.", sourcePath);
    return result;
  }
  if (!validUtf8(text)) {
    addError(result.diagnostics, "DATA_DOCUMENT_INVALID_UTF8",
             "Data document is not valid UTF-8.", sourcePath);
    return result;
  }
  Json parsed;
  try {
    parsed = Json::parse(text);
  } catch (const Json::parse_error &error) {
    addError(result.diagnostics, "DATA_DOCUMENT_INVALID_JSON", error.what(),
             sourcePath);
    return result;
  }
  std::size_t elements = 0;
  auto root =
      convert(parsed, limits, 0, elements, result.diagnostics, sourcePath, "");
  if (root)
    result.document = std::make_shared<const DataDocument>(
        std::move(*root), text.size(), elements);
  return result;
}

DataDocumentResult loadDataDocument(const std::filesystem::path &path,
                                    const DataDocumentLimits &limits) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    DataDocumentResult result;
    addError(result.diagnostics, "DATA_DOCUMENT_NOT_FOUND",
             "Data document could not be opened.", path);
    return result;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return parseDataDocument(buffer.str(), path, limits);
}

} // namespace demi::assets
