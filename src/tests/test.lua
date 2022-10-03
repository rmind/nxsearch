local nxs = require("nxsearch")

local params = nxs.newparams()
assert(params)

local index, err = nxs.create("test-index", params)
if not index then
  index, err = nxs.open("test-index")
  assert(index)
end

index:add(1, "The quick brown fox jumped over the lazy dog")
index:add(2, "Once upon a time there were three little foxes")

local resp, err = index:search("fox")
assert(resp)

local results_json = resp:tojson()
assert(results_json ==
  '{"results":[{"doc_id":2,"score":0.06675427407026291},' ..
  '{"doc_id":1,"score":0.060958366841077805}],"count":2}')

results_table = resp:repr()
assert(#results_table == 2)
assert(tostring(results_table[1]) == "0.060958366841078")
assert(tostring(results_table[2]) == "0.066754274070263")
print("OK")
