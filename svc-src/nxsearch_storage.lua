--
-- Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
-- All rights reserved.
--
-- Use is subject to license terms, as specified in the LICENSE file.
--

local lua_path = require "path"

local NXS_BASEDIR = os.getenv("NXS_BASEDIR")

-------------------------------------------------------------------------

local function get_dirlevels(doc_id)
  local l1 = tonumber(doc_id) % 16
  local l2 = math.floor(tonumber(doc_id) / 16) % 256
  return string.format("%x/%02x", l1, l2)
end

local function get_doc_dirpath(index_name, doc_id)
  return string.format("%s/data/%s/docs/%s",
    lua_path.fullpath(NXS_BASEDIR), index_name, get_dirlevels(doc_id))
end

local function get_doc_path(index_name, doc_id)
  return string.format("%s/%s", get_doc_dirpath(index_name, doc_id), doc_id)
end

local function prepare_doc_location(index_name, doc_id)
  local doc_dir_path = get_doc_dirpath(index_name, doc_id)

  if not lua_path.isdir(doc_dir_path) then
    lua_path.mkdir(doc_dir_path)
  end

  return string.format("%s/%u", doc_dir_path, doc_id)
end

-------------------------------------------------------------------------

local _M = {}

function _M.store_file(index_name, doc_id, content)
  local doc_path = prepare_doc_location(index_name, doc_id)

  local file, err = io.open(doc_path, "w")
  if not file then
    return false, err
  end

  file:write(content)
  file:close()

  return true
end

function _M.fetch_file(index_name, doc_id)
  local doc_path = prepare_doc_location(index_name, doc_id)
  local content

  local file, err = io.open(doc_path, "r")
  if not file then
    return nil, err
  end

  content = file:read("*all")
  file:close()

  return content
end

-------------------------------------------------------------------------

return _M
