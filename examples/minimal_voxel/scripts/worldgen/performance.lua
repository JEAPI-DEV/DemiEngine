local config = require("worldgen.config")

local Performance = {}

function Performance.create()
  return {
    frames = {},
    next_index = 1,
    count = 0,
    total = 0.0,
    display_timer = 0.0,
    label = "fps: -- avg: -- 1%: -- 0.1%: --",
  }
end

local function percentile_low_fps(frames, count, fraction)
  if count == 0 then
    return 0.0
  end

  local sorted = {}
  for index = 1, count do
    sorted[index] = frames[index]
  end
  table.sort(sorted, function(a, b)
    return a > b
  end)

  local sample_count = math.max(1, math.floor((count * fraction) + 0.5))
  local total = 0.0
  for index = 1, sample_count do
    total = total + sorted[index]
  end
  return sample_count / total
end

function Performance.update(stats, dt)
  if dt == nil or dt <= 0.0 then
    return stats.label
  end

  local max_samples = config.performance.sample_frames
  if stats.count < max_samples then
    stats.count = stats.count + 1
  else
    stats.total = stats.total - stats.frames[stats.next_index]
  end

  stats.frames[stats.next_index] = dt
  stats.total = stats.total + dt
  stats.next_index = stats.next_index + 1
  if stats.next_index > max_samples then
    stats.next_index = 1
  end

  stats.display_timer = stats.display_timer + dt
  if stats.display_timer < config.performance.update_interval then
    return stats.label
  end
  stats.display_timer = 0.0

  local current_fps = 1.0 / dt
  local average_fps = stats.count / stats.total
  local one_percent_low = percentile_low_fps(stats.frames, stats.count, 0.01)
  local point_one_percent_low = percentile_low_fps(stats.frames, stats.count, 0.001)
  stats.label = string.format(
    "fps: %.0f avg: %.0f 1%%: %.0f 0.1%%: %.0f",
    current_fps,
    average_fps,
    one_percent_low,
    point_one_percent_low
  )
  return stats.label
end

return Performance
