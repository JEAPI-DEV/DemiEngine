#include "demi/runtime/physics/JoltBodyBatch3D.h"
#include <Jolt/Physics/Body/Body.h>
namespace demi::runtime {
JoltBodyBatch3D::JoltBodyBatch3D(JPH::BodyInterface &bodies,
                                 std::size_t capacity)
    : bodies_(bodies) {
  ids_.reserve(capacity);
}
JoltBodyBatch3D::~JoltBodyBatch3D() {
  if (published_)
    return;
  if (prepared_ && !ids_.empty())
    bodies_.AddBodiesAbort(ids_.data(), ids_.size(), addState_);
  for (auto id : ids_)
    bodies_.DestroyBody(id);
}
JPH::Body *JoltBodyBatch3D::create(const JPH::BodyCreationSettings &settings) {
  if (prepared_ || ids_.size() == ids_.capacity())
    return nullptr;
  auto *body = bodies_.CreateBody(settings);
  if (body)
    ids_.push_back(body->GetID());
  return body;
}
void JoltBodyBatch3D::prepare() {
  if (!ids_.empty())
    addState_ = bodies_.AddBodiesPrepare(ids_.data(), ids_.size());
  prepared_ = true;
}
void JoltBodyBatch3D::publish() {
  if (!ids_.empty())
    bodies_.AddBodiesFinalize(ids_.data(), ids_.size(), addState_,
                              JPH::EActivation::Activate);
  published_ = true;
}
} // namespace demi::runtime
