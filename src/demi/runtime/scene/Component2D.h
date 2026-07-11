#pragma once

#include "demi/runtime/scene/Component.h"

namespace demi::runtime {

class Component2D : public Component {
public:
  using Component::Component;
  [[nodiscard]] std::string_view domain() const override { return "2d"; }
};

} // namespace demi::runtime
