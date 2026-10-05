#include "demi/graph/DependencyGraph.h"

#include <cassert>
#include <set>
#include <string>
#include <vector>

using demi::graph::DependencyGraph;

namespace {

void deterministicOrderAndReachability() {
  DependencyGraph graph;
  for (const auto *id : {"output", "unrelated", "right", "left", "root"})
    assert(graph.addNode(id));
  assert(!graph.addNode("root"));
  assert(!graph.addNode(""));
  assert(!graph.addDependency("missing", "root"));
  assert(!graph.addDependency("output", "missing"));
  assert(graph.addDependency("output", "right"));
  assert(graph.addDependency("output", "left"));
  assert(graph.addDependency("right", "root"));
  assert(graph.addDependency("left", "root"));
  assert(graph.addDependency("left", "root")); // Duplicate edges are idempotent.

  const auto order = graph.topologicalOrder();
  assert(!order.blockedNode);
  assert((order.nodes ==
          std::vector<std::string>{"root", "left", "right", "output",
                                   "unrelated"}));
  assert(graph.dependenciesReachable({"output"}) ==
         std::set<std::string>({"output", "left", "right", "root"}));
  assert(graph.dependentsReachable({"root"}) ==
         std::set<std::string>({"root", "left", "right", "output"}));
  assert(graph.dependentsReachable({"unknown"}) ==
         std::set<std::string>{"unknown"});
}

void cycleDoesNotPreventTraversal() {
  DependencyGraph graph;
  assert(graph.addNode("a"));
  assert(graph.addNode("b"));
  assert(graph.addNode("consumer"));
  assert(graph.addDependency("a", "b"));
  assert(graph.addDependency("b", "a"));
  assert(graph.addDependency("consumer", "a"));
  const auto order = graph.topologicalOrder();
  assert(order.blockedNode);
  assert(graph.dependentsReachable({"b"}) ==
         std::set<std::string>({"a", "b", "consumer"}));
}

void longChainUsesIterativeTraversal() {
  DependencyGraph graph;
  constexpr int count = 20000;
  for (int i = 0; i < count; ++i)
    assert(graph.addNode(std::to_string(i)));
  for (int i = 1; i < count; ++i)
    assert(graph.addDependency(std::to_string(i), std::to_string(i - 1)));
  const auto order = graph.topologicalOrder();
  assert(!order.blockedNode && order.nodes.size() == count);
  assert(order.nodes.front() == "0");
  assert(order.nodes.back() == std::to_string(count - 1));
  assert(graph.dependenciesReachable({std::to_string(count - 1)}).size() ==
         count);
  assert(graph.dependentsReachable({"0"}).size() == count);
}

} // namespace

int main() {
  deterministicOrderAndReachability();
  cycleDoesNotPreventTraversal();
  longChainUsesIterativeTraversal();
}
