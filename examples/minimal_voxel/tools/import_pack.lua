local project_root = arg[1] or "."

local function path(...)
  local parts = { ... }
  return table.concat(parts, "/")
end

local function shell_quote(value)
  return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function write_file(filename, contents)
  local file = assert(io.open(filename, "w"))
  file:write(contents)
  file:close()
end

local function run(command)
  local ok, reason, code = os.execute(command)
  if ok == true or ok == 0 then
    return
  end
  error(string.format("command failed (%s %s): %s", tostring(reason), tostring(code), command))
end

local pack_blocks = path(project_root, "assets", "pack", "blocks")
local generated_assets = path(project_root, "assets", "generated")
local generated_scripts = path(project_root, "generated")
local atlas_path = path(generated_assets, "terrain_atlas.png")
local manifest_path = path(generated_assets, "terrain_atlas.asset.json")
local lua_data_path = path(generated_scripts, "pack_import.lua")

local tiles = {
  { name = "grass_top", file = path(pack_blocks, "grass_top.png"), tint = "#6fb34a" },
  { name = "grass_side", file = path(pack_blocks, "grass_side.png") },
  { name = "dirt", file = path(pack_blocks, "dirt.png") },
  { name = "stone", file = path(pack_blocks, "stone.png") },
  { name = "log_oak", file = path(pack_blocks, "log_oak.png") },
  { name = "log_oak_top", file = path(pack_blocks, "log_oak_top.png") },
  { name = "leaves_oak_opaque", file = path(pack_blocks, "leaves_oak_opaque.png") },
}

run("mkdir -p " .. shell_quote(generated_assets) .. " " .. shell_quote(generated_scripts))

local command = {
  "magick",
  "-size",
  "112x16",
  "xc:none",
}

for index, tile in ipairs(tiles) do
  local x = (index - 1) * 16
  command[#command + 1] = "\\("
  command[#command + 1] = shell_quote(tile.file)
  command[#command + 1] = "-resize"
  command[#command + 1] = "16x16!"
  if tile.tint then
    command[#command + 1] = "\\("
    command[#command + 1] = "+clone"
    command[#command + 1] = "-fill"
    command[#command + 1] = shell_quote(tile.tint)
    command[#command + 1] = "-colorize"
    command[#command + 1] = "100"
    command[#command + 1] = "\\)"
    command[#command + 1] = "-compose"
    command[#command + 1] = "Multiply"
    command[#command + 1] = "-composite"
  end
  command[#command + 1] = "\\)"
  command[#command + 1] = "-geometry"
  command[#command + 1] = "+" .. x .. "+0"
  command[#command + 1] = "-composite"
end
command[#command + 1] = "-depth"
command[#command + 1] = "8"
command[#command + 1] = shell_quote("PNG32:" .. atlas_path)
run(table.concat(command, " "))

write_file(manifest_path, [[{
  "format_version": 1,
  "id": "asset://textures/terrain_atlas",
  "type": "Texture2D",
  "source": "terrain_atlas.png"
}
]])

write_file(lua_data_path, [[return {
  texture = "asset://textures/terrain_atlas",
  atlas_columns = 7,
  blocks = {
    [1] = { top = 0, side = 1, bottom = 2 },
    [2] = { top = 2, side = 2, bottom = 2 },
    [3] = { top = 3, side = 3, bottom = 3 },
    [4] = { top = 5, side = 4, bottom = 5 },
    [5] = { top = 6, side = 6, bottom = 6 },
  },
}
]])

print("Imported minimal voxel pack:")
print("  " .. atlas_path)
print("  " .. manifest_path)
print("  " .. lua_data_path)
