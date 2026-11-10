-- SPDX-License-Identifier: GPL-3.0-or-later
-- Incremental framing for a JSON array of objects; jsonc validates each object.
-- Keep one bounded record, never the full command response or decoded array.
local json = require "luci.jsonc"
local logic = require "luci.model.ocserv_easy.logic"
local M = {}

function M.new(visit, max_rows)
    local state, depth, count = "start", 0, 0
    local quoted, escaped, failed = false, false, false
    local parts, size = {}, 0
    local function append(chunk, first, last)
        size = size + last - first + 1
        if size > 65536 then return false end
        parts[#parts + 1] = chunk:sub(first, last)
        return true
    end
    local function write(chunk)
        local offset, first = 1, state == "row" and 1 or nil
        while offset <= #chunk do
            if state == "row" then
                if escaped then offset = offset + 1; escaped = false end
                local at = chunk:find(quoted and '[\\"]' or '[%[%]{}"]', offset)
                if not at then break end
                local byte = chunk:sub(at, at)
                offset = at + 1
                if byte == '"' then quoted = not quoted
                elseif quoted then
                    if offset > #chunk then escaped = true else offset = offset + 1 end
                elseif byte == "{" or byte == "[" then
                    depth = depth + 1
                    if depth > 32 then return false end
                else
                    depth = depth - 1
                    if depth == 0 then
                        if byte ~= "}" or not append(chunk, first, at) then return false end
                        local raw = table.concat(parts)
                        parts, size, first = {}, 0, nil
                        if logic.json_has_null(raw) then return false end
                        local ok, row = pcall(json.parse, raw)
                        if not ok or type(row) ~= "table" or visit(row) == false then return false end
                        state = "comma"
                    end
                end
            else
                local at = chunk:find("[^ \t\r\n]", offset)
                if not at then break end
                local byte = chunk:sub(at, at)
                offset = at + 1
                if state == "start" and byte == "[" then state = "first"
                elseif (state == "first" or state == "comma") and byte == "]" then state = "done"
                elseif state == "comma" and byte == "," then state = "next"
                elseif (state == "first" or state == "next") and byte == "{" then
                    count = count + 1
                    if count > max_rows then return false end
                    state, depth, first = "row", 1, at
                else return false end
            end
        end
        return not first or append(chunk, first, #chunk)
    end
    return {
        write = function(chunk)
            if failed then return false end
            failed = not write(chunk)
            if failed then parts = {} end
            return not failed
        end,
        finish = function() return not failed and state == "done" end
    }
end

return M
