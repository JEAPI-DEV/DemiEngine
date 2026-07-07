# Minimal Voxel Example

used assets:
- [`https://resourcepack.net/ocd-resource-pack/`](https://resourcepack.net/ocd-resource-pack/)

Import the terrain atlas from the checked-in pack files with:

```sh
lua5.4 examples/minimal_voxel/tools/import_pack.lua examples/minimal_voxel
```

This generates ignored runtime files under `examples/minimal_voxel/assets/generated/` and
`examples/minimal_voxel/generated/`. If the generated files are missing, the example still
runs with an untextured terrain mesh.
