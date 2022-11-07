--
-- This code is in the public domain.
--

local cjson = require "cjson"

return {
  create = function(json_params)
    local params = cjson.decode(json_params)
    return {["lang"] = params["lang"]}
  end,

  destroy = function(ctx)
    assert(ctx.lang == "en")
  end,

  filter = function(ctx, value)
    assert(ctx.lang == "en")
    return string.lower(value)
  end,

  cleanup = function()
    -- module level cleanup
  end,
}
