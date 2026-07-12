#pragma once

#include "demi/runtime/scene/Component.h"

namespace demi::runtime {

class Component3D : public Component {
public:
  using Component::Component;
  [[nodiscard]] std::string_view domain() const override { return "3d"; }
};

} // namespace demi::runtime
