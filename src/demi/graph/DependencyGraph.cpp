#include "demi/graph/DependencyGraph.h"

#include <utility>

namespace demi::graph {

bool DependencyGraph::addNode(std::string id) {
  if (id.empty())
    return false;
  const bool inserted = dependencies_.try_emplace(id).second;
  if (inserted)
    dependents_.try_emplace(std::move(id));
  return inserted;
}

bool DependencyGraph::addDependency(const std::string &node,
                                    const std::string &dependency) {
  const auto found = dependencies_.find(node);
  if (found == dependencies_.end() || !dependencies_.contains(dependency))
    return false;
  if (found->second.insert(dependency).second)
    dependents_.at(dependency).insert(node);
  return true;
}

DependencyGraph::Order DependencyGraph::topologicalOrder() const {
  std::map<std::string, std::size_t> remaining;
  std::set<std::string> ready;
  for (const auto &[id, dependencies] : dependencies_) {
    remaining.emplace(id, dependencies.size());
    if (dependencies.empty())
      ready.insert(id);
  }

  Order result;
  result.nodes.reserve(dependencies_.size());
  while (!ready.empty()) {
    const std::string current = *ready.begin();
    ready.erase(ready.begin());
    result.nodes.push_back(current);
    for (const auto &dependent : dependents_.at(current))
      if (--remaining.at(dependent) == 0)
        ready.insert(dependent);
  }
  if (result.nodes.size() != dependencies_.size())
    for (const auto &[id, count] : remaining)
      if (count != 0) {
        result.blockedNode = id;
        break;
      }
  return result;
}

std::set<std::string> DependencyGraph::reachable(
    const std::set<std::string> &roots,
    const std::map<std::string, std::set<std::string>> &edges) {
  std::set<std::string> result = roots;
  std::vector<std::string> pending(roots.begin(), roots.end());
  while (!pending.empty()) {
    const std::string current = std::move(pending.back());
    pending.pop_back();
    if (const auto found = edges.find(current); found != edges.end())
      for (const auto &next : found->second)
        if (result.insert(next).second)
          pending.push_back(next);
  }
  return result;
}

std::set<std::string> DependencyGraph::dependenciesReachable(
    const std::set<std::string> &roots) const {
  return reachable(roots, dependencies_);
}

std::set<std::string> DependencyGraph::dependentsReachable(
    const std::set<std::string> &roots) const {
  return reachable(roots, dependents_);
}

} // namespace demi::graph
