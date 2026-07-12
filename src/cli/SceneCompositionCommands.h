#pragma once
#include <iosfwd>
#include <string>
#include <vector>
namespace demi::cli {
int runPrefabCommand(const std::vector<std::string> &, std::ostream &,
                     std::ostream &);
int runSceneExpand(const std::vector<std::string> &, std::ostream &,
                   std::ostream &);
int runSceneDiff(const std::vector<std::string> &, std::ostream &,
                 std::ostream &);
} // namespace demi::cli
