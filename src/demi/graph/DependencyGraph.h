#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace demi::graph {

// Edges point from a node to the nodes it depends on. Cycles are allowed in
// storage; consumers that need a DAG request an order and handle failure.
class DependencyGraph {
public:
  struct Order {
    std::vector<std::string> nodes;
    std::optional<std::string> blockedNode;
  };

  [[nodiscard]] bool addNode(std::string id);
  [[nodiscard]] bool addDependency(const std::string &node,
                                   const std::string &dependency);
  [[nodiscard]] Order topologicalOrder() const;
  [[nodiscard]] std::set<std::string>
  dependenciesReachable(const std::set<std::string> &roots) const;
  [[nodiscard]] std::set<std::string>
  dependentsReachable(const std::set<std::string> &roots) const;

private:
  static std::set<std::string> reachable(
      const std::set<std::string> &roots,
      const std::map<std::string, std::set<std::string>> &edges);

  std::map<std::string, std::set<std::string>> dependencies_;
  std::map<std::string, std::set<std::string>> dependents_;
};

} // namespace demi::graph
