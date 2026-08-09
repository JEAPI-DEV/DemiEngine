#pragma once

#include <cstddef>
#include <span>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "demi/runtime/ui/UiModel.h"

namespace demi::runtime::ui { class UiTweenSystem; }

namespace demi::runtime::ui {

struct UiVirtualRange {
  std::size_t first = 0;
  std::size_t count = 0;
};

class UiVirtualCollection {
public:
  [[nodiscard]] static UiVirtualRange
  visibleRange(std::size_t itemCount, float itemExtent, float scrollOffset,
               float viewportExtent, std::size_t overscan = 2);
};

class UiVirtualLayout {
public:
  [[nodiscard]] bool reset(std::span<const float> itemExtents,
                           std::string &error);
  [[nodiscard]] bool setItemExtent(std::size_t index, float itemExtent,
                                   std::string &error);

  [[nodiscard]] UiVirtualRange visibleRange(float scrollOffset,
                                            float viewportExtent,
                                            std::size_t overscan = 2) const;
  [[nodiscard]] float itemOffset(std::size_t index) const;
  [[nodiscard]] float itemExtent(std::size_t index) const;
  [[nodiscard]] float totalExtent() const;
  [[nodiscard]] std::size_t itemCount() const { return extents_.size(); }

private:
  std::vector<float> extents_;
  std::vector<float> offsets_{0.0F};
};

struct UiVirtualRowBinding {
  std::string key;
  std::size_t index = 0;
  UiNodeHandle node;
  float offset = 0.0F;
  float extent = 0.0F;
  bool rebound = false;
};

struct UiVirtualReconcileResult {
  bool applied = false;
  std::string error;
  UiVirtualRange range;
  std::vector<UiVirtualRowBinding> rows;
};

class UiVirtualRecycler {
public:
  explicit UiVirtualRecycler(std::string collectionId)
      : collectionId_(std::move(collectionId)) {}

  [[nodiscard]] UiVirtualReconcileResult
  reconcile(UiDocument &document, UiTweenSystem &tweens,
            UiNodeHandle rowTemplate, std::span<const std::string> stableKeys,
            std::span<const float> rowExtents, float scrollOffset,
            float viewportExtent, std::size_t overscan = 2);
  void clear(UiDocument &document, UiTweenSystem &tweens);
  [[nodiscard]] bool valid(const UiDocument &document) const;
  [[nodiscard]] std::size_t poolSize() const { return slots_.size(); }

private:
  struct Slot { std::string root; std::string key; };
  [[nodiscard]] bool resetSlot(UiDocument &document, UiTweenSystem &tweens,
                               const UiNodeHandle &rowTemplate, Slot &slot,
                               std::string &error) const;

  std::string collectionId_;
  std::vector<Slot> slots_;
  std::unordered_map<std::string, std::size_t> keyToSlot_;
  std::optional<UiNodeHandle> rowTemplate_;
};

} // namespace demi::runtime::ui
