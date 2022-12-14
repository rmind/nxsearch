--
-- This code is in the public domain.
--

local nxs = require "nxsearch"
local cjson = require "cjson"

local params = nxs.newparams()
assert(params)

local index, err = nxs.create("__test-index-lua-1", params)
if not index then
  index, err = nxs.open("__test-index-lua-1")
  assert(index)
end

index:add(1, "The quick brown fox jumped over the lazy dog")
index:add(2, "Once upon a time there were three little foxes")
index:add(3, "Test")

local doc_id, err = index:add(3, "Test")
assert(doc_id == nil)
assert(err.code == nxs.ERR_EXISTS)
assert(err.msg == "document 3 is already indexed")

index:remove(3)

local resp, err = index:search("fox")
assert(resp)

--
-- Get the results as JSON.
--

local SCORE_DOC_1 = "0.0610"
local SCORE_DOC_2 = "0.0668"

local function round_score(x) return string.format("%.4f", x) end

local results_json = cjson.decode(resp:tojson())
assert(results_json["count"] == 2)

local doc = results_json["results"][1]
assert(doc["doc_id"] == 2)
assert(round_score(doc["score"]) == SCORE_DOC_2)

local doc = results_json["results"][2]
assert(doc["doc_id"] == 1)
assert(round_score(doc["score"]) == SCORE_DOC_1)

--
-- Get the results as a table.
--

local results_table = resp:repr()
assert(#results_table == 2)
assert(round_score(results_table[1]) == SCORE_DOC_1)
assert(round_score(results_table[2]) == SCORE_DOC_2)

local ok, err = nxs.destroy("__test-index-lua-1")
assert(ok)

print("OK")
