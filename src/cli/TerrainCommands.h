#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace demi::cli {

// `demi terrain inspect <recipe.json> [--format text|json]`: reports the
// authored recipe - size, resolution, seed, biome table, layer tree, rules in
// authored order, stroke counts and derived sub-seeds - without generating a
// field, so tooling can read a large recipe cheaply.
//
// `demi terrain explain <recipe.json> --at <x>,<z> [--format text|json]`
// generates the field and reports which biome a world position received, which
// rule assigned it (or the default biome, or a painted region override), and
// whether every condition of that rule contained the sample. A position no
// rule matched is reported explicitly with each rule's conditions, because
// "my terrain is all one biome" cannot be answered by a rule listing alone.
//
// `demi terrain seeds <seed> [--format text|json]`: prints the derived sub-seed
// of every seed channel for a world seed.
[[nodiscard]] int runTerrainCommand(const std::vector<std::string> &args,
                                   std::ostream &out, std::ostream &error);

} // namespace demi::cli
