#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace demi::runtime {

// Polymorphism belongs at the authored-component boundary. Runtime systems use
// the typed value components below, while tools can uniformly inspect every
// component authored in JSON.
class Component {
public:
  Component(std::string name, std::string json)
      : name_(std::move(name)), json_(std::move(json)) {}
  virtual ~Component() = default;

  [[nodiscard]] std::string_view name() const { return name_; }
  [[nodiscard]] std::string_view json() const { return json_; }
  [[nodiscard]] virtual std::string_view domain() const = 0;

private:
  std::string name_;
  std::string json_;
};

} // namespace demi::runtime
