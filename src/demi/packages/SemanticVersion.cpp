#include "demi/packages/SemanticVersion.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>

namespace demi::packages {
namespace {

bool parsePart(const std::string_view value, int &result) {
  if (value.empty() || (value.size() > 1 && value.front() == '0'))
    return false;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  return error == std::errc{} && end == value.data() + value.size() &&
         result >= 0;
}

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.remove_suffix(1);
  return value;
}

bool validIdentifier(const std::string_view value) {
  if (value.empty() || value.front() == '.' || value.back() == '.' ||
      value.find("..") != std::string_view::npos)
    return false;
  return
         std::ranges::all_of(value, [](const unsigned char character) {
           return std::isalnum(character) != 0 || character == '-' ||
                  character == '.';
         });
}

std::strong_ordering comparePrerelease(std::string_view left,
                                       std::string_view right) {
  while (true) {
    const auto leftDot = left.find('.');
    const auto rightDot = right.find('.');
    const auto leftPart = left.substr(0, leftDot);
    const auto rightPart = right.substr(0, rightDot);
    const bool leftNumeric = std::ranges::all_of(leftPart, [](unsigned char value) {
      return std::isdigit(value) != 0;
    });
    const bool rightNumeric = std::ranges::all_of(rightPart, [](unsigned char value) {
      return std::isdigit(value) != 0;
    });
    if (leftNumeric && rightNumeric) {
      if (leftPart.size() != rightPart.size())
        return leftPart.size() <=> rightPart.size();
      if (const auto order = leftPart <=> rightPart; order != 0)
        return order;
    } else if (leftNumeric != rightNumeric) {
      return leftNumeric ? std::strong_ordering::less
                         : std::strong_ordering::greater;
    } else if (const auto order = leftPart <=> rightPart; order != 0) {
      return order;
    }
    const bool leftDone = leftDot == std::string_view::npos;
    const bool rightDone = rightDot == std::string_view::npos;
    if (leftDone || rightDone) {
      if (leftDone == rightDone)
        return std::strong_ordering::equal;
      return leftDone ? std::strong_ordering::less
                      : std::strong_ordering::greater;
    }
    left.remove_prefix(leftDot + 1);
    right.remove_prefix(rightDot + 1);
  }
}

} // namespace

std::optional<SemanticVersion> SemanticVersion::parse(std::string_view value) {
  value = trim(value);
  const std::size_t build = value.find('+');
  if (build != std::string_view::npos) {
    if (!validIdentifier(value.substr(build + 1)))
      return std::nullopt;
    value = value.substr(0, build);
  }
  std::string prerelease;
  const std::size_t dash = value.find('-');
  if (dash != std::string_view::npos) {
    prerelease = std::string(value.substr(dash + 1));
    value = value.substr(0, dash);
    if (!validIdentifier(prerelease))
      return std::nullopt;
    std::string_view identifiers = prerelease;
    while (!identifiers.empty()) {
      const auto dot = identifiers.find('.');
      const auto identifier = identifiers.substr(0, dot);
      if (identifier.size() > 1 && identifier.front() == '0' &&
          std::ranges::all_of(identifier, [](unsigned char value) {
            return std::isdigit(value) != 0;
          }))
        return std::nullopt;
      if (dot == std::string_view::npos)
        break;
      identifiers.remove_prefix(dot + 1);
    }
  }
  const std::size_t first = value.find('.');
  const std::size_t second = first == std::string_view::npos
                                 ? std::string_view::npos
                                 : value.find('.', first + 1);
  if (first == std::string_view::npos || second == std::string_view::npos ||
      value.find('.', second + 1) != std::string_view::npos)
    return std::nullopt;
  SemanticVersion version;
  if (!parsePart(value.substr(0, first), version.major) ||
      !parsePart(value.substr(first + 1, second - first - 1), version.minor) ||
      !parsePart(value.substr(second + 1), version.patch))
    return std::nullopt;
  version.prerelease = std::move(prerelease);
  return version;
}

std::string SemanticVersion::string() const {
  std::string result = std::to_string(major) + "." + std::to_string(minor) +
                       "." + std::to_string(patch);
  if (!prerelease.empty())
    result += "-" + prerelease;
  return result;
}

std::strong_ordering
SemanticVersion::operator<=>(const SemanticVersion &other) const {
  if (const auto order = major <=> other.major; order != 0)
    return order;
  if (const auto order = minor <=> other.minor; order != 0)
    return order;
  if (const auto order = patch <=> other.patch; order != 0)
    return order;
  if (prerelease.empty() != other.prerelease.empty())
    return prerelease.empty() ? std::strong_ordering::greater
                              : std::strong_ordering::less;
  return comparePrerelease(prerelease, other.prerelease);
}

std::optional<VersionConstraint>
VersionConstraint::parse(std::string_view value) {
  value = trim(value);
  VersionConstraint constraint;
  constraint.text_ = value.empty() ? "*" : std::string(value);
  if (value.empty() || value == "*") {
    constraint.clauses_.push_back(
        {.operation = Operation::Any, .version = SemanticVersion{}});
    return constraint;
  }

  const auto addClause = [&](const Operation operation,
                             const std::string_view versionText) -> bool {
    const auto version = SemanticVersion::parse(trim(versionText));
    if (!version)
      return false;
    constraint.clauses_.push_back(
        {.operation = operation, .version = *version});
    return true;
  };

  if (value.front() == '^' || value.front() == '~') {
    const char kind = value.front();
    const auto minimum = SemanticVersion::parse(value.substr(1));
    if (!minimum)
      return std::nullopt;
    constraint.clauses_.push_back(
        {.operation = Operation::GreaterEqual, .version = *minimum});
    SemanticVersion maximum = *minimum;
    if (kind == '~') {
      ++maximum.minor;
      maximum.patch = 0;
    } else if (minimum->major > 0) {
      ++maximum.major;
      maximum.minor = 0;
      maximum.patch = 0;
    } else if (minimum->minor > 0) {
      ++maximum.minor;
      maximum.patch = 0;
    } else {
      ++maximum.patch;
    }
    maximum.prerelease.clear();
    constraint.clauses_.push_back(
        {.operation = Operation::Less, .version = maximum});
    return constraint;
  }

  std::size_t cursor = 0;
  while (cursor < value.size()) {
    const std::size_t comma = value.find(',', cursor);
    std::string_view clause = trim(value.substr(
        cursor, comma == std::string_view::npos ? value.size() - cursor
                                                : comma - cursor));
    Operation operation = Operation::Equal;
    std::size_t prefix = 0;
    if (clause.starts_with(">=")) {
      operation = Operation::GreaterEqual;
      prefix = 2;
    } else if (clause.starts_with("<=")) {
      operation = Operation::LessEqual;
      prefix = 2;
    } else if (clause.starts_with('>')) {
      operation = Operation::Greater;
      prefix = 1;
    } else if (clause.starts_with('<')) {
      operation = Operation::Less;
      prefix = 1;
    } else if (clause.starts_with('=')) {
      prefix = 1;
    }
    if (!addClause(operation, clause.substr(prefix)))
      return std::nullopt;
    if (comma == std::string_view::npos)
      break;
    cursor = comma + 1;
  }
  return constraint;
}

bool VersionConstraint::accepts(const SemanticVersion &version) const {
  return std::ranges::all_of(clauses_, [&](const Clause &clause) {
    switch (clause.operation) {
    case Operation::Any:
      return true;
    case Operation::Equal:
      return version == clause.version;
    case Operation::Greater:
      return version > clause.version;
    case Operation::GreaterEqual:
      return version >= clause.version;
    case Operation::Less:
      return version < clause.version;
    case Operation::LessEqual:
      return version <= clause.version;
    }
    return false;
  });
}

const std::string &VersionConstraint::text() const { return text_; }

} // namespace demi::packages
