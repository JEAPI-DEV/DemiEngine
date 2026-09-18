#pragma once

namespace demi::runtime {
// Shared registration lease for worlds and off-world shape preparation.
class JoltLifetime {
public:
  JoltLifetime();
  ~JoltLifetime();
  JoltLifetime(const JoltLifetime &) = delete;
  JoltLifetime &operator=(const JoltLifetime &) = delete;
};
} // namespace demi::runtime
