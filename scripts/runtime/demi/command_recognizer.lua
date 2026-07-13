local Recognizer = {}

-- Matches newest-to-oldest while allowing unrelated buffered inputs between
-- command steps. The caller owns input names and decides whether to consume.
function Recognizer.match(entries, sequence, max_gap)
  local wanted = #sequence
  local previous_time = nil
  for index = #entries, 1, -1 do
    local entry = entries[index]
    if entry.action == sequence[wanted] then
      if previous_time and previous_time - entry.time > max_gap then
        return false
      end
      previous_time = entry.time
      wanted = wanted - 1
      if wanted == 0 then return true end
    end
  end
  return false
end

return Recognizer
