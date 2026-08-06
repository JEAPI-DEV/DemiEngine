#include "demi/runtime/render/backend/ResourceHandle.h"

#include <cassert>
#include <string>

namespace {

struct TextureTag;
using Pool =
    demi::runtime::render::ResourceHandlePool<TextureTag, std::string>;

void staleHandlesNeverAliasReusedSlots() {
  Pool pool;
  const auto first = pool.insert("first");
  assert(pool.valid(first));
  assert(*pool.find(first) == "first");
  std::string removed;
  assert(pool.remove(first, &removed));
  assert(removed == "first");
  assert(!pool.valid(first));
  assert(pool.find(first) == nullptr);

  const auto second = pool.insert("second");
  assert(second.index == first.index);
  assert(second.generation != first.generation);
  assert(!pool.remove(first));
  assert(pool.valid(second));
  assert(*pool.find(second) == "second");
}

void invalidAndDuplicateRemovalAreSafe() {
  Pool pool;
  Pool::Handle invalid;
  assert(!pool.valid(invalid));
  assert(!pool.remove(invalid));
  const auto handle = pool.insert("value");
  assert(pool.remove(handle));
  assert(!pool.remove(handle));
  assert(pool.size() == 0);
}

void clearDestroysOnlyLiveValues() {
  Pool pool;
  const auto removed = pool.insert("removed");
  const auto live = pool.insert("live");
  assert(pool.remove(removed));
  int destroyed = 0;
  pool.clear([&](const std::string &value) {
    assert(value == "live");
    ++destroyed;
  });
  assert(destroyed == 1);
  assert(pool.size() == 0);
  assert(!pool.valid(live));

  const auto afterClear = pool.insert("after clear");
  assert(afterClear.index == live.index);
  assert(afterClear.generation != live.generation);
  assert(!pool.valid(live));

  pool.clear([](const std::string &) {});
  pool.clear([](const std::string &) {});
  const auto afterRepeatedClear = pool.insert("after repeated clear");
  assert(afterRepeatedClear.index == afterClear.index);
  assert(afterRepeatedClear.generation != afterClear.generation);
  assert(!pool.valid(afterClear));
}

} // namespace

int main() {
  staleHandlesNeverAliasReusedSlots();
  invalidAndDuplicateRemovalAreSafe();
  clearDestroysOnlyLiveValues();
  return 0;
}
