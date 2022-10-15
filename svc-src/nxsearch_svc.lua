--
-- Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
-- All rights reserved.
--
-- Use is subject to license terms, as specified in the LICENSE file.
--

local nxs = require "nxsearch"
local routes = require "resty.route".new()
local lrucache = require "resty.lrucache"
local cjson = require "cjson"

local nxs_index_ttl = 86400
local nxs_index_map = lrucache.new(32)

local PARAMS_NUMFIELDS = {"limit"}

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
  ngx.say(cjson.encode({["error"] = {
    ["code"] = err.code,
    ["msg"] = err.msg,
  }}))
  return ngx.exit(ngx.HTTP_BAD_REQUEST)
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

-------------------------------------------------------------------------

routes:post("@/:string", function(self, name)
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
  local ok, err = nxs.destroy(name)
  if not ok then
    return set_http_error(err)
  end
  return ngx.exit(ngx.HTTP_OK)
end)

routes:post("@/:string/add/:number", function(self, name, doc_id)
  local index = get_nxs_index(name)
  local id, err = index:add(doc_id, get_http_body(true))
  if not id then
    return set_http_error(err)
  end
  return ngx.exit(ngx.HTTP_CREATED)
end)

routes:delete("@/:string/remove/:number", function(self, name, doc_id)
  local index = get_nxs_index(name)
  local id, err = index:remove(doc_id, get_http_body(true))
  if not id then
    return set_http_error(err)
  end
  return ngx.exit(ngx.HTTP_OK)
end)

routes:post("@/:string/search", function(self, name)
  local index = get_nxs_index(name)
  local query_string = ngx.req.get_uri_args()
  local params = query_string_to_params(query_string)

  local resp, err = index:search(get_http_body(true), params)
  if not resp then
    return set_http_error(err)
  end

  ngx.say(resp:tojson())
  return ngx.exit(ngx.HTTP_OK)
end)

return routes
