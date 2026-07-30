#pragma once

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace demi::runtime::render {

template <typename Tag> struct ResourceHandle {
  std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t generation = 0;

  [[nodiscard]] explicit operator bool() const {
    return index != std::numeric_limits<std::uint32_t>::max();
  }
  bool operator==(const ResourceHandle &) const = default;
};

// Internal ownership primitive shared by all backend resources. Reusing a
// slot always increments its generation, so stale game/renderer handles can
// never resolve to a newly-created GPU object.
template <typename Tag, typename Value> class ResourceHandlePool {
public:
  using Handle = ResourceHandle<Tag>;

  [[nodiscard]] Handle insert(Value value) {
    if (!free_.empty()) {
      const std::uint32_t index = free_.back();
      free_.pop_back();
      Slot &slot = slots_[index];
      slot.value = std::move(value);
      slot.occupied = true;
      return {.index = index, .generation = slot.generation};
    }
    slots_.push_back(
        Slot{.value = std::move(value), .generation = 1, .occupied = true});
    return {.index = static_cast<std::uint32_t>(slots_.size() - 1),
            .generation = 1};
  }

  [[nodiscard]] Value *find(const Handle handle) {
    if (!valid(handle))
      return nullptr;
    return &slots_[handle.index].value;
  }

  [[nodiscard]] const Value *find(const Handle handle) const {
    if (!valid(handle))
      return nullptr;
    return &slots_[handle.index].value;
  }

  [[nodiscard]] bool valid(const Handle handle) const {
    return handle.index < slots_.size() && slots_[handle.index].occupied &&
           slots_[handle.index].generation == handle.generation;
  }

  [[nodiscard]] bool remove(const Handle handle, Value *removed = nullptr) {
    if (!valid(handle))
      return false;
    Slot &slot = slots_[handle.index];
    if (removed != nullptr)
      *removed = std::move(slot.value);
    slot.value = {};
    slot.occupied = false;
    ++slot.generation;
    if (slot.generation == 0)
      slot.generation = 1;
    free_.push_back(handle.index);
    return true;
  }

  template <typename Destroy> void clear(Destroy destroy) {
    free_.clear();
    for (std::uint32_t index = 0; index < slots_.size(); ++index) {
      Slot &slot = slots_[index];
      if (slot.occupied) {
        destroy(slot.value);
        slot.value = {};
        slot.occupied = false;
      }
      ++slot.generation;
      if (slot.generation == 0)
        slot.generation = 1;
      free_.push_back(index);
    }
  }

  [[nodiscard]] std::size_t size() const {
    return slots_.size() - free_.size();
  }

private:
  struct Slot {
    Value value{};
    std::uint32_t generation = 1;
    bool occupied = false;
  };
  std::vector<Slot> slots_;
  std::vector<std::uint32_t> free_;
};

} // namespace demi::runtime::render
