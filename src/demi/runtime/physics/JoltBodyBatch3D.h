#pragma once
#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyInterface.h>
#include <vector>

namespace demi::runtime {
// Owns unpublished native bodies. Aborting never touches their live sources.
class JoltBodyBatch3D {
public:
  JoltBodyBatch3D(JPH::BodyInterface &bodies, std::size_t capacity);
  ~JoltBodyBatch3D();
  JoltBodyBatch3D(const JoltBodyBatch3D &) = delete;
  JoltBodyBatch3D &operator=(const JoltBodyBatch3D &) = delete;
  JPH::Body *create(const JPH::BodyCreationSettings &settings);
  void prepare();
  void publish();

private:
  JPH::BodyInterface &bodies_;
  std::vector<JPH::BodyID> ids_;
  JPH::BodyInterface::AddState addState_ = nullptr;
  bool prepared_ = false;
  bool published_ = false;
};
} // namespace demi::runtime
