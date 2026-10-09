-- SPDX-License-Identifier: GPL-2.0-or-later
-- JSON for the bridge: objects are tables with string keys, arrays are tables made with Json.Array
-- (or any non-empty sequence). null decodes to nil.
local _, LA = ...

local Json = {}
LA.Json = Json

local ArrayMeta = {}

function Json.Array(items)
    return setmetatable(items or {}, ArrayMeta)
end

local escapes = { ['"'] = '\\"', ["\\"] = "\\\\", ["\b"] = "\\b", ["\f"] = "\\f", ["\n"] = "\\n",
    ["\r"] = "\\r", ["\t"] = "\\t", ["|"] = "\\u007c" }

local function EncodeString(text)
    return '"' .. text:gsub('[%c"\\|]', function(c)
        return escapes[c] or string.format("\\u%04x", c:byte())
    end) .. '"'
end

local function IsArray(value)
    if getmetatable(value) == ArrayMeta then
        return true
    end
    local count = 0
    for key in pairs(value) do
        if type(key) ~= "number" then
            return false
        end
        count = count + 1
    end
    return count > 0 and count == #value
end

local function Encode(value, out)
    local kind = type(value)
    if kind == "nil" then
        out[#out + 1] = "null"
    elseif kind == "boolean" then
        out[#out + 1] = value and "true" or "false"
    elseif kind == "number" then
        if value ~= value or value == math.huge or value == -math.huge then
            error("JSON cannot hold " .. tostring(value))
        end
        out[#out + 1] = value == math.floor(value) and string.format("%d", value) or string.format("%.14g", value)
    elseif kind == "string" then
        out[#out + 1] = EncodeString(value)
    elseif kind == "table" then
        if IsArray(value) then
            out[#out + 1] = "["
            for i = 1, #value do
                if i > 1 then
                    out[#out + 1] = ","
                end
                Encode(value[i], out)
            end
            out[#out + 1] = "]"
        else
            out[#out + 1] = "{"
            local first = true
            for key, item in pairs(value) do
                if type(key) ~= "string" then
                    error("JSON object keys must be strings")
                end
                if not first then
                    out[#out + 1] = ","
                end
                first = false
                out[#out + 1] = EncodeString(key)
                out[#out + 1] = ":"
                Encode(item, out)
            end
            out[#out + 1] = "}"
        end
    else
        error("JSON cannot hold a " .. kind)
    end
end

function Json.Encode(value)
    local out = {}
    Encode(value, out)
    return table.concat(out)
end

-- Decoding

local function Utf8(code)
    if code < 0x80 then
        return string.char(code)
    elseif code < 0x800 then
        return string.char(0xC0 + math.floor(code / 0x40), 0x80 + code % 0x40)
    elseif code < 0x10000 then
        return string.char(0xE0 + math.floor(code / 0x1000), 0x80 + math.floor(code / 0x40) % 0x40, 0x80 + code % 0x40)
    end
    return string.char(0xF0 + math.floor(code / 0x40000), 0x80 + math.floor(code / 0x1000) % 0x40,
        0x80 + math.floor(code / 0x40) % 0x40, 0x80 + code % 0x40)
end

local Decode

local function Skip(text, at)
    return text:find("[^ \t\r\n]", at) or #text + 1
end

local function DecodeString(text, at)
    local parts = {}
    local i = at + 1
    while true do
        local stop = text:find('["\\]', i)
        if not stop then
            error("unterminated string")
        end
        parts[#parts + 1] = text:sub(i, stop - 1)
        if text:sub(stop, stop) == '"' then
            return table.concat(parts), stop + 1
        end
        local kind = text:sub(stop + 1, stop + 1)
        if kind == "u" then
            local code = tonumber(text:sub(stop + 2, stop + 5), 16)
            if not code then
                error("bad \\u escape")
            end
            i = stop + 6
            if code >= 0xD800 and code < 0xDC00 and text:sub(i, i + 1) == "\\u" then
                local low = tonumber(text:sub(i + 2, i + 5), 16)
                if low and low >= 0xDC00 and low < 0xE000 then
                    code = 0x10000 + (code - 0xD800) * 0x400 + (low - 0xDC00)
                    i = i + 6
                end
            end
            parts[#parts + 1] = Utf8(code)
        else
            local simple = ({ b = "\b", f = "\f", n = "\n", r = "\r", t = "\t" })[kind]
            parts[#parts + 1] = simple or kind
            i = stop + 2
        end
    end
end

function Decode(text, at)
    at = Skip(text, at)
    local c = text:sub(at, at)
    if c == "{" then
        local object = {}
        at = Skip(text, at + 1)
        if text:sub(at, at) == "}" then
            return object, at + 1
        end
        while true do
            if text:sub(at, at) ~= '"' then
                error("expected a key at " .. at)
            end
            local key
            key, at = DecodeString(text, at)
            at = Skip(text, at)
            if text:sub(at, at) ~= ":" then
                error("expected : at " .. at)
            end
            object[key], at = Decode(text, at + 1)
            at = Skip(text, at)
            local next = text:sub(at, at)
            if next == "}" then
                return object, at + 1
            elseif next ~= "," then
                error("expected , or } at " .. at)
            end
            at = Skip(text, at + 1)
        end
    elseif c == "[" then
        local array = Json.Array()
        at = Skip(text, at + 1)
        if text:sub(at, at) == "]" then
            return array, at + 1
        end
        local n = 0
        while true do
            n = n + 1
            array[n], at = Decode(text, at)
            at = Skip(text, at)
            local next = text:sub(at, at)
            if next == "]" then
                return array, at + 1
            elseif next ~= "," then
                error("expected , or ] at " .. at)
            end
            at = at + 1
        end
    elseif c == '"' then
        return DecodeString(text, at)
    elseif text:sub(at, at + 3) == "true" then
        return true, at + 4
    elseif text:sub(at, at + 4) == "false" then
        return false, at + 5
    elseif text:sub(at, at + 3) == "null" then
        return nil, at + 4
    end
    local number = text:match("^-?%d+%.?%d*[eE]?[-+]?%d*", at)
    if not number or number == "" then
        error("unexpected " .. (c ~= "" and c or "end") .. " at " .. at)
    end
    return tonumber(number), at + #number
end

-- The decoded value, or nil and the reason.
function Json.Decode(text)
    local ok, value, at = pcall(Decode, text, 1)
    if not ok then
        return nil, value
    end
    if Skip(text, at) <= #text then
        return nil, "trailing text"
    end
    return value
end
