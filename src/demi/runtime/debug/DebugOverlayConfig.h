#pragma once

namespace demi::runtime {

struct DebugOverlayConfig {
  bool colliders = false;
  bool contacts = false;
  bool grid = false;
  bool entityIds = false;
  bool drawOrder = false;
  bool uiBounds = false;
  bool profilerHud = false;
};

} // namespace demi::runtime
