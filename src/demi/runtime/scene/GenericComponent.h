#pragma once

#include "demi/runtime/scene/Component.h"

namespace demi::runtime {

class GenericComponent final : public Component {
public:
  using Component::Component;
  [[nodiscard]] std::string_view domain() const override { return "generic"; }
};

} // namespace demi::runtime
