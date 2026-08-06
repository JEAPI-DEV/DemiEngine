#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace demi::runtime::ui {

struct UiMutationResult {
  bool applied = false;
  std::string error;
};

class UiMutationQueue {
public:
  void create(std::string parent, UiNode node);
  void clone(UiNodeHandle source, std::string newRootId,
             std::string parent = {});
  void remove(UiNodeHandle node);
  void reparent(UiNodeHandle node, std::string parent);
  void clearChildren(std::string parent);

  [[nodiscard]] UiMutationResult apply(UiDocument &document);
  [[nodiscard]] bool empty() const { return mutations_.empty(); }
  [[nodiscard]] static std::optional<UiNodeHandle>
  handle(const UiDocument &document, std::string_view id);
  [[nodiscard]] static bool alive(const UiDocument &document,
                                  const UiNodeHandle &handle);
  static void initializeGenerations(UiDocument &document);

private:
  struct Create { std::string parent; UiNode node; };
  struct Clone { UiNodeHandle source; std::string root; std::string parent; };
  struct Remove { UiNodeHandle node; };
  struct Reparent { UiNodeHandle node; std::string parent; };
  struct Clear { std::string parent; };
  using Mutation = std::variant<Create, Clone, Remove, Reparent, Clear>;
  std::vector<Mutation> mutations_;
};

} // namespace demi::runtime::ui
