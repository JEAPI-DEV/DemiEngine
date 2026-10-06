#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace demi {
struct AssetRegistry;
}

namespace demi::assets {

// What the inspected terrain assets may be used for. Deliberately NOT a
// download or fetch mechanism: this classifies what is already in the project,
// so a non-redistributable asset is a finding to fix by choosing different
// developer-selected assets, never something this validator resolves for the
// author.
enum class TerrainAssetUse { DevelopmentOnly, Shippable };

enum class TerrainAssetFindingKind {
  MissingRole,       // a role the palette needs is not bound at all
  UnresolvedAsset,   // bound, but the asset:// reference does not resolve
  WrongAssetType,    // resolves, but is not the kind of asset the role implies
  MissingMap,        // a material role whose asset has no map the shading needs
  IncompatibleScale, // tiling or scale out of a sane range
  LicenceRestricted, // usable in development, not in a shipped build
  MissingDependency, // referenced but absent from the manifest dependencies
  // Nothing in the manifest states redistribution terms. It is NOT the same as
  // LicenceRestricted: the advice is "declare a licence", not "replace the
  // asset", and collapsing the two would tell an author their art is
  // restricted when in fact nobody ever said.
  UndeclaredLicence,
};

[[nodiscard]] std::string_view
terrainAssetFindingKindName(TerrainAssetFindingKind kind);
[[nodiscard]] std::string_view terrainAssetUseName(TerrainAssetUse use);

struct TerrainAssetFinding {
  TerrainAssetFindingKind kind = TerrainAssetFindingKind::UnresolvedAsset;
  std::string role;    // the palette or material-set role, empty when not role-scoped
  std::string assetId; // empty when the finding is about an absent binding
  std::string message; // one actionable line naming what to do
};

struct TerrainAssetValidationRequest {
  const AssetRegistry *registry = nullptr; // required
  TerrainAssetUse use = TerrainAssetUse::Shippable;
  // A palette and/or a material set. Either may be null; at least one is needed.
  std::string paletteId;
  std::string materialSetId;
  // Whether to require every map slot the renderer will need. Off by default
  // because the example project legitimately ships colour-only materials.
  bool requireFullMaterialMaps = false;
};

struct TerrainAssetValidationReport {
  std::vector<TerrainAssetFinding> findings;
  // Everything the validator inspected: every asset:// reference it followed,
  // plus the palette and material-set documents themselves, so a caller can
  // report coverage and not just failure.
  std::size_t checked = 0;
  // The requested use and map strictness, kept so blocking() and shippable()
  // answer for the request that produced this report.
  TerrainAssetUse use = TerrainAssetUse::Shippable;
  bool requireFullMaterialMaps = false;

  // Whether this finding stops the requested use. Shared with the CLI and the
  // editor panel so a development-only project is not blocked by a shipping
  // restriction it will never hit.
  [[nodiscard]] bool blocking(const TerrainAssetFinding &finding) const noexcept;
  [[nodiscard]] std::size_t blockingCount() const noexcept;
  [[nodiscard]] std::size_t advisoryCount() const noexcept;
  [[nodiscard]] bool shippable() const noexcept;
  // One line per finding, and a single summary line. Used by the CLI and the
  // editor panel so both report identically.
  [[nodiscard]] std::string summary() const;
};

// Answers, before any terrain is generated, whether the roles a palette and a
// material set bind are actually usable: present, of a usable kind, with the
// maps the shading needs, with a sane scale, declared in their manifest
// dependencies, and licensed for the requested use.
//
// Never fetches, downloads or installs anything: it inspects what is already in
// the registry. Every problem is collected as its own finding rather than
// thrown, because an author needs the whole list at once. Document shape and
// role vocabulary stay with the palette/material-set loaders and demi validate;
// this validator reports asset usability, and says so rather than repeating a
// rejection it did not cause.
//
// Throws std::invalid_argument only for a request it cannot act on: a null
// registry, or neither a palette nor a material set.
[[nodiscard]] TerrainAssetValidationReport
validateTerrainAssets(const TerrainAssetValidationRequest &request);

} // namespace demi::assets
