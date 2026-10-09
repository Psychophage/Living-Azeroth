-- Runs outside the game: lua5.1 addon/tests/json_test.lua
local LA = {}
assert(loadfile(arg[0]:gsub("tests/json_test.lua$", "LivingAzeroth/Json.lua")))(nil, LA)
local Json = LA.Json

local function check(value, expected)
    assert(value == expected, ("expected %s, got %s"):format(tostring(expected), tostring(value)))
end

check(Json.Encode({ op = "who", guids = Json.Array({ 12, 4096 }) }):find('"guids":%[12,4096%]') ~= nil, true)
check(Json.Encode(Json.Array()), "[]")
check(Json.Encode("a|b\n\"c\""), '"a\\u007cb\\n\\"c\\""')
check(Json.Encode(-3), "-3")
check(Json.Encode(0.5), "0.5")

local value = Json.Decode('{"re":"a1","ok":true,"bots":[{"guid":4294967295,"name":"Br\\u00e4n","order":null}],'
    .. '"x":-1.5e2,"e":[]}')
check(value.re, "a1")
check(value.ok, true)
check(value.bots[1].guid, 4294967295)
check(value.bots[1].name, "Br\195\164n")
check(value.bots[1].order, nil)
check(value.x, -150)
check(#value.e, 0)
check(Json.Encode(value.e), "[]")
check(Json.Decode('"\\ud83d\\ude00"'), "\240\159\152\128")

local nothing, problem = Json.Decode('{"a":1')
check(nothing, nil)
assert(problem)
check(select(2, Json.Decode('{"a":1} x')), "trailing text")

local round = Json.Decode(Json.Encode({ text = "line|one\ttwo", list = Json.Array({ true, false }) }))
check(round.text, "line|one\ttwo")
check(round.list[2], false)
print("json: all checks passed")
