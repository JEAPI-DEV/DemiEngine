local Path = {}
Path.__index = Path

local function finite(value)
  return type(value) == "number" and value == value
    and value > -math.huge and value < math.huge
end

local function copy_vector(value, field)
  assert(type(value) == "table" and #value == 3, field .. " must have three coordinates")
  local result = {}
  for axis = 1, 3 do
    assert(finite(value[axis]), field .. " coordinates must be finite")
    result[axis] = value[axis]
  end
  return result
end

local function spline(previous, start, finish, following, fraction)
  local squared = fraction * fraction
  local cubed = squared * fraction
  local result = {}
  for axis = 1, 3 do
    result[axis] = 0.5 * (
      2 * start[axis]
      + (finish[axis] - previous[axis]) * fraction
      + (2 * previous[axis] - 5 * start[axis] + 4 * finish[axis] - following[axis]) * squared
      + (3 * start[axis] - previous[axis] - 3 * finish[axis] + following[axis]) * cubed
    )
  end
  return result
end

function Path.new(keyframes, options)
  options = options or {}
  assert(type(keyframes) == "table" and #keyframes >= 2, "A camera path needs at least two keyframes")
  assert(finite(options.duration) and options.duration > 0, "Path duration must be positive and finite")
  assert(options.loop == nil or type(options.loop) == "boolean", "Path loop must be a boolean")
  local frames = {}
  for index, keyframe in ipairs(keyframes) do
    frames[index] = {
      position = copy_vector(keyframe.position, "Keyframe position"),
      target = copy_vector(keyframe.target, "Keyframe target"),
    }
  end
  return setmetatable({
    keyframes = frames,
    duration = options.duration,
    loop = options.loop ~= false,
    elapsed = 0,
    playing = true,
  }, Path)
end

function Path:normalize(seconds)
  assert(finite(seconds), "Path time must be finite")
  if self.loop then
    return seconds % self.duration
  end
  return math.max(0, math.min(seconds, self.duration))
end

function Path:frame(index)
  if self.loop then
    return self.keyframes[((index - 1) % #self.keyframes) + 1]
  end
  return self.keyframes[math.max(1, math.min(index, #self.keyframes))]
end

-- Position and aim point share a Catmull-Rom curve. Looping paths include the
-- final-to-first leg; nonlooping paths clamp end tangents to their endpoints.
function Path:sample(seconds)
  local elapsed = self:normalize(seconds)
  local segments = self.loop and #self.keyframes or #self.keyframes - 1
  local phase = elapsed / self.duration * segments
  local segment = math.min(math.floor(phase) + 1, segments)
  local fraction = phase - (segment - 1)
  local previous = self:frame(segment - 1)
  local start = self:frame(segment)
  local finish = self:frame(segment + 1)
  local following = self:frame(segment + 2)
  return {
    position = spline(previous.position, start.position, finish.position, following.position, fraction),
    target = spline(previous.target, start.target, finish.target, following.target, fraction),
    segment = segment,
    progress = elapsed / self.duration,
    elapsed = elapsed,
  }
end

function Path:seek(seconds)
  self.elapsed = self:normalize(seconds)
  return self:sample(self.elapsed)
end

function Path:pause()
  self.playing = false
end

function Path:play()
  self.playing = true
end

function Path:restart()
  self.elapsed = 0
  self.playing = true
  return self:sample(0)
end

function Path:update(dt)
  assert(finite(dt) and dt >= 0, "Path delta time must be finite and nonnegative")
  if self.playing then
    self.elapsed = self:normalize(self.elapsed + dt)
    if not self.loop and self.elapsed == self.duration then
      self.playing = false
    end
  end
  return self:sample(self.elapsed)
end

return Path
