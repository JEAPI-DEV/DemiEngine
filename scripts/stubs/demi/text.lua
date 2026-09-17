---@meta
-- Native module: require("demi.text"). Annotations only.
---@class TextLayoutLine
---@field text string
---@field x number
---@field y number
---@field width number
---@class TextLayoutResult
---@field width number
---@field height number
---@field grapheme_count integer
---@field truncated boolean
---@field valid_utf8 boolean
---@field shaping_complete boolean
---@field lines TextLayoutLine[]
---@class TextService
local Text = {}
---@param value string
---@return integer
function Text.grapheme_count(value) end
---@param value string
---@param first integer One-based grapheme index.
---@param count integer
---@return string|nil
function Text.grapheme_slice(value, first, count) end
---@param value string
---@param width number
---@param font_size number
---@param max_lines? integer
---@return TextLayoutResult
function Text.layout(value, width, font_size, max_lines) end
---@class RichTextSpan
---@field begin integer
---@field length integer
---@field style string
---@field value string
---@class RichTextResult
---@field text string
---@field spans RichTextSpan[]
---@field diagnostics string[]
---@param markup string
---@param strict? boolean
---@return RichTextResult
function Text.parse_rich(markup, strict) end

return Text
