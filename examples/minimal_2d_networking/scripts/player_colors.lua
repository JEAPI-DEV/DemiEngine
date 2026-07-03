local PlayerColors = {}

local HOST = { 0.30, 0.62, 1.0, 1.0 }

local PEERS = {
  { 1.0, 0.46, 0.34, 1.0 },
  { 0.42, 0.92, 0.52, 1.0 },
  { 1.0, 0.78, 0.28, 1.0 },
  { 0.86, 0.48, 1.0, 1.0 },
  { 0.30, 0.92, 0.92, 1.0 },
  { 1.0, 0.56, 0.76, 1.0 },
}

local function peer_index(sender_id)
  local number = string.match(sender_id or "", "^peer(%d+)$")
  if number == nil then
    return 1
  end
  return ((tonumber(number) or 1) - 1) % #PEERS + 1
end

function PlayerColors.for_sender(sender_id)
  if sender_id == "host" then
    return HOST
  end
  return PEERS[peer_index(sender_id)]
end

return PlayerColors
