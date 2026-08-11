local Hitscan = {}

function Hitscan.fire(events, query, values)
  assert(events and query)
  values = values or {}
  local hits = query(values.from_x or 0, values.from_y or 0,
    values.to_x or 0, values.to_y or 0, values.mask) or {}
  table.sort(hits, function(a, b)
    local left, right = a.fraction or 0, b.fraction or 0
    if left == right then return tostring(a.entity) < tostring(b.entity) end
    return left < right
  end)

  local remaining = values.pierce or 0
  local struck = {}
  for _, hit in ipairs(hits) do
    local id = hit.entity
    if id and id ~= values.owner and id ~= values.ignored and
      not hit.trigger and not struck[id] then
      struck[id] = true
      events:emit("damage_requested", {
        source = values.owner,
        target = id,
        amount = values.damage or 0,
        type = values.type,
        point = hit.point,
        normal = hit.normal,
        tags = { "hitscan" },
      })
      if remaining == 0 then return id end
      remaining = remaining - 1
    end
  end
  return nil
end

return Hitscan
