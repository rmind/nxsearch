--
-- Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
-- All rights reserved.
--
-- Use is subject to license terms, as specified in the LICENSE file.
--

local nxs = require "nxsearch"
local nxs_fs = require "nxsearch_storage"
local routes = require "resty.route".new()
local lrucache = require "resty.lrucache"
local cjson = require "cjson"

local NXS_BASEDIR = os.getenv("NXS_BASEDIR")
local NXS_ENABLE_LUA_POST = os.getenv("NXS_ENABLE_LUA_POST")

local nxs_index_ttl = 86400
local nxs_index_map = lrucache.new(32)

local PARAMS_NUMFIELDS = {"limit"}

-------------------------------------------------------------------------

local function nxs_svc_init()
  local filters = nxs_fs.get_filters()
  local i

  for i = 1, #filters do
    local name = filters[i]
    local content = nxs_fs.get_filter_code(name)
    local ok, err = nxs.load_lua(name, content)
    if not ok then error(err) end
  end
end

-------------------------------------------------------------------------

local function get_http_body(raise_err)
  ngx.req.read_body() -- fetch the body data
  local data = ngx.req.get_body_data()
  if not data and raise_err then
    ngx.status = ngx.HTTP_BAD_REQUEST
    ngx.say("no data")
    ngx.exit(ngx.HTTP_BAD_REQUEST)
  end
  return data
end

local function set_http_error(err)
  ngx.status = ngx.HTTP_BAD_REQUEST
  ngx.say(cjson.encode({["error"] = {
    ["code"] = err.code,
    ["msg"] = err.msg,
  }}))
  return ngx.exit(ngx.HTTP_BAD_REQUEST)
end

local function set_http_sys_error(msg)
  return set_http_error({["code"] = nxs.ERR_SYSTEM, ["msg"] = msg})
end

local function get_nxs_index(name)
  local index = nxs_index_map:get(name)
  local err

  if not index then
    index, err = nxs.open(name)
    if not index then
      return set_http_error(err)
    end
    nxs_index_map:set(name, index, nxs_index_ttl)
  end
  return index
end

local function query_string_to_params(args)
  local i

  -- If empty table, then return nil.
  if next(args) == nil then
    return nil
  end

  -- Adjust the type of the numeric fields.
  for i = 1, #PARAMS_NUMFIELDS do
    local field = PARAMS_NUMFIELDS[i]
    local value = args[field]
    if value ~= nil then
      args[field] = tonumber(value)
    end
  end

  local qstr_json = cjson.encode(args)
  return nxs.newparams():fromjson(qstr_json)
end

local function fetch_resp_to_json(index_name, repr)
  local results = {}
  local doc_id, score

  for doc_id, score in pairs(repr) do
    table.insert(results, {
      ["doc_id"] = doc_id,
      ["score"] = score,
      ["content"] = nxs_fs.fetch_file(index_name, doc_id),
    })
  end

  return cjson.encode({
    ["results"] = results,
    ["count"] = #results
  })
end

-------------------------------------------------------------------------

--[[
  @schema index_params
  type: object
  properties:
    "algo":
      description: Ranking algorithm.
      type: string
      enum: ["BM25", "TF-IDF"]
    "lang":
      description: >
        Language of the content in the index which should be two-letter
        ISO 639-1 code.
      type: string
      default: "en"
    "filters":
      description: >
        A list of filters (in the specified order) to apply when tokenizing.
      type: array
      uniqueItems: true
      items:
        type: string
      default: ["normalizer", "stopwords", "stemmer"]
--]]

--[[
  @schema search_response
  type: object
  properties:
    count:
      type: integer
    results:
      type: array
      items:
        type: object
        properties:
          doc_id:
            type: number
            format: int64
          score:
            type: number
            format: float
--]]

--[[
  @schema error_response
  type: object
  properties:
    error:
      type: object
      properties:
        "msg":
          type: string
        "code":
          description: |
            0 — success;
            1 — unspecified fatal error;
            2 — operating system error;
            3 — invalid parameter or value;
            4 — resource already exists;
            5 — resource is missing;
            6 — resource limit reached;
          type: integer
--]]

-------------------------------------------------------------------------

routes:post("@/filters/:string/lua", function(self, name)
  --[[
  @api [post] /filters/{name}/lua
  tags:
    - filters
  description: |
    Create a Lua-based filter.
    Only if `NXS_ENABLE_LUA_POST` environment variable is set to non-empty value.
  parameters:
    - name: "name"
      description: "Filter name (alphanumeric only)"
      in: "path"
      type: "string"
    - name: "store"
      description: >
        Store the filter persistently.
        Service restart will be needed.
      in: query
      schema:
        type: boolean
      default: false
  requestBody:
    required: true
    content:
      application/x-lua:
        schema:
          type: string
          description: Lua source code
  responses:
    201:
      description: "Created"
    400:
      content:
        application/json:
          schema:
            $ref: "#/components/schemas/error_response"
  --]]

  if not NXS_ENABLE_LUA_POST then
    return set_http_sys_error("Lua code posting is not enabled")
  end

  local payload = get_http_body(true)
  local query_string = ngx.req.get_uri_args()

  if name:match("%W") then
    return set_http_sys_error("filter name must be alphanumeric")
  end

  local ok, err = nxs.load_lua(name, payload)
  if not ok then
    return set_http_error(err)
  end

  if query_string["store"] then
    local ok, errmsg = nxs_fs.store_filter(name, payload)
    if not ok then
      return set_http_sys_error(errmsg)
    end
  end

  return ngx.exit(ngx.HTTP_CREATED)
end)

-------------------------------------------------------------------------

routes:post("@/:string", function(self, name)
  --[[
  @api [post] /{index}
  description: "Create an index."
  tags:
    - index
  parameters:
    - name: "index"
      description: "Index name (must be alphanumeric)"
      in: path
      type: string
  requestBody:
    content:
      application/json:
        schema:
          $ref: '#/components/schemas/index_params'
  responses:
    201:
      description: "Created"
    400:
      content:
        application/json:
          schema:
            $ref: "#/components/schemas/error_response"
  --]]

  local params = nil
  local payload = get_http_body(false)

  if payload then
    local params = nxs.newparams():fromjson(payload)
    if not params then
      return set_http_error("OOM")
    end
  end

  local index, err = nxs.create(name, params)
  if not index then
    return set_http_error(err)
  end
  nxs_index_map:set(name, index, nxs_index_ttl)

  return ngx.exit(ngx.HTTP_CREATED)
end)

routes:delete("@/:string", function(self, name)
  --[[
  @api [delete] /{index}
  description: "Delete an index."
  tags:
    - index
  parameters:
    - name: "index"
      description: "Index name"
      in: "path"
      type: "string"
  responses:
    200:
      description: "OK"
    400:
      content:
        application/json:
          schema:
            $ref: "#/components/schemas/error_response"
  --]]

  -- Destroy the reference first.
  nxs_index_map:delete(name)
  collectgarbage() -- ensure index gets closed
  nxs_fs.destroy_index(name)

  local ok, err = nxs.destroy(name)
  if not ok then
    return set_http_error(err)
  end

  return ngx.exit(ngx.HTTP_OK)
end)

-------------------------------------------------------------------------

routes:post("@/:string/add/:number", function(self, name, doc_id)
  --[[
  @api [post] /{index}/add/{doc_id}
  description: "Add a document."
  tags:
    - documents
  parameters:
    - name: "index"
      description: "Index name"
      in: "path"
      type: "string"
    - name: "doc_id"
      description: "A unique document identifier"
      in: "path"
      type: "integer"
      format: "int64"
  requestBody:
    required: true
    content:
      text/plain:
        schema:
          type: string
        example: "The quick brown fox jumped over the lazy dog."
  responses:
    201:
      description: "Created"
    400:
      content:
        application/json:
          schema:
            $ref: "#/components/schemas/error_response"
  --]]

  local index = get_nxs_index(name)
  local query_string = ngx.req.get_uri_args()
  local params = query_string_to_params(query_string)
  local payload = get_http_body(true)

  if query_string["store"] then
    doc_id = tonumber(doc_id)
    local ok, err = nxs_fs.store_file(name, doc_id, payload)
    if not ok then
      return set_http_sys_error(err)
    end
  end

  local id, err = index:add(doc_id, payload)
  if not id then
    return set_http_error(err)
  end

  return ngx.exit(ngx.HTTP_CREATED)
end)

routes:delete("@/:string/remove/:number", function(self, name, doc_id)
  --[[
  @api [delete] /{index}/remove/{doc_id}
  description: "Delete a document."
  tags:
    - documents
  parameters:
    - name: "index"
      description: "Index name"
      in: "path"
      type: "string"
    - name: "doc_id"
      description: "Target document identifier"
      in: "path"
      type: "integer"
      format: "int64"
  responses:
    200:
      description: "OK"
    400:
      content:
        application/json:
          schema:
            $ref: "#/components/schemas/error_response"
  --]]

  local index = get_nxs_index(name)
  local id, err = index:remove(doc_id, get_http_body(true))
  if not id then
    return set_http_error(err)
  end
  return ngx.exit(ngx.HTTP_OK)
end)

-------------------------------------------------------------------------

routes:post("@/:string/search", function(self, name)
  --[[
  @api [post] /{index}/search
  tags:
    - search
  description: "Search the index for documents."
  parameters:
    - name: "index"
      description: "Index name"
      in: "path"
      type: "string"
    - name: "algo"
      description: "Override the ranking algorithm (see index creation)"
      in: query
      schema:
        type: string
    - name: "limit"
      description: "The cap for the results"
      in: query
      schema:
        type: integer
      default: 1000
    - name: "fuzzymatch"
      description: "Fuzzy-match the terms"
      in: query
      schema:
        type: boolean
      default: true
  responses:
    200:
      content:
        application/json:
          schema:
            $ref: "#/components/schemas/search_response"
    400:
      content:
        application/json:
          schema:
            $ref: "#/components/schemas/error_response"
  --]]

  local index = get_nxs_index(name)
  local query_string = ngx.req.get_uri_args()
  local params = query_string_to_params(query_string)

  local resp, err = index:search(get_http_body(true), params)
  if not resp then
    return set_http_error(err)
  end

  if query_string["fetch"] then
    ngx.say(fetch_resp_to_json(name, resp:repr()))
  else
    ngx.say(resp:tojson())
  end

  return ngx.exit(ngx.HTTP_OK)
end)

-------------------------------------------------------------------------

nxs_svc_init() -- initialize nxsearch service

return routes
