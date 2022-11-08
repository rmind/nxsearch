# nxsearch Lua filters

## General

nxsearch provides an interface to implement filters using Lua.  For automatic
loading (registering) of the filters, the source files shall be placed in the
`$NXS_BASEDIR/filters/` directory and must use the `.lua` extension.  The base
name of the file represents the filter name, e.g. `lowercase.lua` will
register a filter called `lowercase`.

Alternatively, nxsearch service has `/filters/{name}/lua` endpoint where the
filter can be registered by HTTP POST-ing the source code.  The `store` flag
may be used to save the filter persistently (but it will require the service
restart so that the filter would be picked up by all workers).

## API

The Lua filter module (or posted code block) should return a table which
may have the following members:

* `ctx, err = create(json_params)`
  * A handler called upon the filter pipeline creation, when opening a
  particular index. It may be used to obtain index parameters, such as the
  language.  Arbitrary index parameters may also be passed on index creation
  and retrieved by the filters.  The `json_params` is a JSON string which
  has to be decoded.
  * This method may return an arbitrary value/context which will be passed
  to the `filter()` and `destroy()` methods.  It may also return `nil`.

* `destroy(ctx)`
  * This method is invoked on filter pipeline destruction. It may be
  used to release the resources associated with an arbitrary context
  passed by `create()`.

* `result, err = filter(ctx, value)`
  * The main filter handler: takes the arbitrary context, the token string
  and returns the transformed string.
  * A tuple of `nil` values may be returned to indicate that the token must
  be discarded.
  * A tuple of `nil` and error string may be used to indicate a failure.

* `cleanup()`
  * A module-level cleanup handler which can be used to release any resources
  created during the initialization of the Lua module.  Typically invoked when
  the application exits.

The `filter()` method is mandatory, while other methods are optional.

## Example

This is an example module which converts the tokens to lowercase:

```lua
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
}
```

To register the filter:
```shell
curl -d @lowercase.lua http://127.0.0.1:8000/filters/lowercase/lua?store
# restart the service
```
