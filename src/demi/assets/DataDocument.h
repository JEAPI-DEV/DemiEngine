#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace demi::assets {

struct DataValue {
  using Array = std::vector<DataValue>;
  using Object = std::map<std::string, DataValue, std::less<>>;
  using Storage = std::variant<std::monostate, bool, std::int64_t, double,
                               std::string, Array, Object>;

  Storage value;

  [[nodiscard]] bool isNull() const;
  [[nodiscard]] bool isBoolean() const;
  [[nodiscard]] bool isInteger() const;
  [[nodiscard]] bool isNumber() const;
  [[nodiscard]] bool isString() const;
  [[nodiscard]] bool isArray() const;
  [[nodiscard]] bool isObject() const;
  [[nodiscard]] const Array *array() const;
  [[nodiscard]] const Object *object() const;
  [[nodiscard]] const DataValue *find(std::string_view key) const;
};

struct DataDocumentLimits {
  std::size_t maximumBytes = 4U * 1024U * 1024U;
  std::size_t maximumDepth = 64;
  std::size_t maximumElements = 100'000;
  std::size_t maximumStringBytes = 1U * 1024U * 1024U;
};

class DataDocument {
public:
  DataDocument(DataValue root, std::size_t byteSize, std::size_t elementCount);

  [[nodiscard]] const DataValue &root() const;
  [[nodiscard]] std::size_t byteSize() const;
  [[nodiscard]] std::size_t elementCount() const;

private:
  DataValue root_;
  std::size_t byteSize_ = 0;
  std::size_t elementCount_ = 0;
};

struct DataDocumentResult {
  std::shared_ptr<const DataDocument> document;
  Diagnostics diagnostics;
};

[[nodiscard]] DataDocumentResult
parseDataDocument(std::string_view text, std::filesystem::path sourcePath = {},
                  const DataDocumentLimits &limits = {});
[[nodiscard]] DataDocumentResult
loadDataDocument(const std::filesystem::path &path,
                 const DataDocumentLimits &limits = {});

} // namespace demi::assets
