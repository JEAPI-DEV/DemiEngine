#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::packages {

struct SemanticVersion {
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::string prerelease;

  [[nodiscard]] static std::optional<SemanticVersion>
  parse(std::string_view value);
  [[nodiscard]] std::string string() const;
  [[nodiscard]] std::strong_ordering
  operator<=>(const SemanticVersion &other) const;
  [[nodiscard]] bool operator==(const SemanticVersion &other) const = default;
};

class VersionConstraint {
public:
  [[nodiscard]] static std::optional<VersionConstraint>
  parse(std::string_view value);
  [[nodiscard]] bool accepts(const SemanticVersion &version) const;
  [[nodiscard]] const std::string &text() const;

private:
  enum class Operation { Any, Equal, Greater, GreaterEqual, Less, LessEqual };
  struct Clause {
    Operation operation = Operation::Any;
    SemanticVersion version;
  };

  std::string text_ = "*";
  std::vector<Clause> clauses_;
};

} // namespace demi::packages
