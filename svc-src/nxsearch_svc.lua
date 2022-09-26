--
-- Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
-- All rights reserved.
--
-- Use is subject to license terms, as specified in the LICENSE file.
--

local nxs = require "nxsearch"
local lrucache = require "resty.lrucache"
local cjson = require "cjson"
local routes = require "resty.route".new()

local nxs_index_ttl = 86400
local nxs_index_map = lrucache.new(32)

-------------------------------------------------------------------------

local function get_http_body()
  ngx.req.read_body() -- fetch the body data
  local data = ngx.req.get_body_data()
  if not data then
    ngx.status = ngx.HTTP_BAD_REQUEST
    ngx.say("no data")
    ngx.exit(ngx.HTTP_BAD_REQUEST)
  end
  return data
end

local function set_http_error(err)
  ngx.say(cjson.encode({["error"] = err}))
  return ngx.exit(ngx.HTTP_BAD_REQUEST)
end

local function get_nxs_index(name)
  local index = nxs_index_map:get(name)
  if not index then
    index, err = nxs.open(name)
    if not index then
      return set_http_error(err)
    end
    nxs_index_map:set(name, index, nxs_index_ttl)
  end
  return index
end

-------------------------------------------------------------------------

routes:post("@/:string", function(self, name)
  local params = nxs.newparams()
  if not params then
    return set_http_error("OOM")
  end

  local index, err = nxs.create(name, params)
  if not index then
    return set_http_error(err)
  end
  nxs_index_map:set(name, index, nxs_index_ttl)

  return ngx.exit(ngx.HTTP_CREATED)
end)

routes:delete("@/:string", function(self, name)
  return set_http_error("not implemented yet")
end)

routes:post("@/:string/add/:number", function(self, name, doc_id)
  local index = get_nxs_index(name)
  index:add(doc_id, get_http_body())
  return ngx.exit(ngx.HTTP_CREATED)
end)

routes:post("@/:string/search", function(self, name)
  local index = get_nxs_index(name)
  local resp, err = index:search(get_http_body())
  if not resp then
    return set_http_error(err)
  end

  ngx.say(resp:tojson())
  return ngx.exit(ngx.HTTP_OK)
end)

return routes