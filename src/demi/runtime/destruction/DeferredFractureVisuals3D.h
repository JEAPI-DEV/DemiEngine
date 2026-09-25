#pragma once
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace demi::runtime {
struct World;
struct Entity;
struct Destructible3DComponent;

// Two visual levels over the support graph: intact regions and their leaves.
// Preparation changes only the candidate world; commit follows physics commit.
class DeferredFractureVisuals3D {
public:
  void configure(const World &, const Entity &,
                 const Destructible3DComponent &);
  std::string dormantOwner(const std::string &visual) const;
  std::set<std::string>
  prepare(World &, const std::map<std::string, std::string> &owners) const;
  void commit(std::set<std::string> regions);

private:
  std::string root_;
  std::shared_ptr<const nlohmann::json> templates_;
  std::map<std::string, std::string> regions_;
  std::map<std::string, std::string> leafOwners_;
  std::set<std::string> refined_;
};
} // namespace demi::runtime
