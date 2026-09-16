#include "demi/runtime/physics/JoltLifetime.h"
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <mutex>

namespace demi::runtime {
namespace {
std::mutex lifetimeMutex;
unsigned references = 0;
} // namespace
JoltLifetime::JoltLifetime() {
  std::scoped_lock lock(lifetimeMutex);
  if (references == 0) {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  }
  ++references;
}
JoltLifetime::~JoltLifetime() {
  std::scoped_lock lock(lifetimeMutex);
  if (--references != 0)
    return;
  JPH::UnregisterTypes();
  delete JPH::Factory::sInstance;
  JPH::Factory::sInstance = nullptr;
}
} // namespace demi::runtime
