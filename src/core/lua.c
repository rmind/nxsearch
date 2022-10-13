/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdlib.h>
#include <inttypes.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "nxs.h"
#include "utils.h"

#define	NXS_PARAMS_METATABLE	"nxs-params-obj"
#define	NXS_IDX_METATABLE	"nxs-index-obj"
#define	NXS_RESP_METATABLE	"nxs-resp-obj"

static nxs_t *	nxs = NULL;

int		luaopen_nxsearch(lua_State *);
static int	luaclose_nxsearch(lua_State *L);

///////////////////////////////////////////////////////////////////////////////

static int
lua_nxs_push_error(lua_State *L)
{
	const char *msg;
	nxs_err_t code;

	code = nxs_get_error(nxs, &msg);

	lua_createtable(L, 0, 2);
	lua_pushinteger(L, code);
	lua_setfield(L, -2, "code");
	lua_pushstring(L, msg);
	lua_setfield(L, -2, "msg");
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

static int
lua_nxs_params_create(lua_State *L)
{
	nxs_params_t **p, *params;

	if ((params = nxs_params_create()) == NULL) {
		return luaL_error(L, "OOM");
	}
	if ((p = lua_newuserdata(L, sizeof(nxs_params_t *))) == NULL) {
		nxs_params_release(params);
		return luaL_error(L, "OOM");
	}
	*p = params;
	luaL_getmetatable(L, NXS_PARAMS_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

static nxs_params_t *
lua_nxs_params_getctx(lua_State *L, int idx)
{
	void *ud = luaL_checkudata(L, idx, NXS_PARAMS_METATABLE);
	return *(nxs_params_t **)ud;
}

static int
lua_nxs_params_fromjson(lua_State *L)
{
	nxs_params_t *params, **pp;
	const char *json;
	size_t json_len;
	void *ud;

	ud = luaL_checkudata(L, 1, NXS_PARAMS_METATABLE);
	json = lua_tolstring(L, 2, &json_len);
	luaL_argcheck(L, json && json_len, 2, "non-empty `string' expected");

	params = nxs_params_fromjson(nxs, json, json_len);
	if (params == NULL) {
		lua_pushnil(L);
		lua_nxs_push_error(L);
		return 2;
	}
	pp = (nxs_params_t **)ud;
	nxs_params_release(*pp);
	*pp = params;

	lua_pushvalue(L, -2);  // re-push the params object
	return 1;
}

static int
lua_nxs_params_gc(lua_State *L)
{
	nxs_params_t *params = lua_nxs_params_getctx(L, 1);
	nxs_params_release(params);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

static int
lua_nxs_index_newctx(lua_State *L, nxs_index_t *idx)
{
	nxs_index_t **p;

	if ((p = lua_newuserdata(L, sizeof(nxs_index_t *))) == NULL) {
		return -1;
	}
	*p = idx;
	luaL_getmetatable(L, NXS_IDX_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

static nxs_index_t *
lua_nxs_index_getctx(lua_State *L)
{
	void *ud = luaL_checkudata(L, 1, NXS_IDX_METATABLE);
	return *(nxs_index_t **)ud;
}

static int
lua_nxs_index_gc(lua_State *L)
{
	nxs_index_t *idx = lua_nxs_index_getctx(L);
	nxs_index_close(idx);
	return 0;
}

static int
lua_nxs_index_open(lua_State *L)
{
	const char *name;
	nxs_index_t *idx;
	int ret;

	name = lua_tostring(L, 1);
	luaL_argcheck(L, name, 1, "non-empty `string' expected");

	if ((idx = nxs_index_open(nxs, name)) == NULL) {
		lua_pushnil(L);
		lua_nxs_push_error(L);
		return 2;
	}
	if ((ret = lua_nxs_index_newctx(L, idx)) == -1) {
		nxs_index_close(idx);
		return luaL_error(L, "OOM");
	}
	lua_pushnil(L);
	return 2;
}

///////////////////////////////////////////////////////////////////////////////

static int
lua_nxs_index_create(lua_State *L)
{
	const char *name;
	nxs_index_t *idx;
	nxs_params_t *params;
	int ret;

	name = lua_tostring(L, 1);
	luaL_argcheck(L, name, 1, "non-empty `string' expected");
	params = lua_isnoneornil(L, 2) ? NULL : lua_nxs_params_getctx(L, 2);

	if ((idx = nxs_index_create(nxs, name, params)) == NULL) {
		lua_pushnil(L);
		lua_nxs_push_error(L);
		return 2;
	}
	if ((ret = lua_nxs_index_newctx(L, idx)) == -1) {
		nxs_index_close(idx);
		return luaL_error(L, "OOM");
	}
	lua_pushnil(L);
	return 2;
}

static int
lua_nxs_index_destroy(lua_State *L)
{
	const char *name;

	name = lua_tostring(L, 1);
	luaL_argcheck(L, name, 1, "non-empty `string' expected");

	if (nxs_index_destroy(nxs, name) == -1) {
		lua_pushboolean(L, false);
		lua_nxs_push_error(L);
		return 2;
	}
	lua_pushboolean(L, true);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

static int
lua_nxs_index_add(lua_State *L)
{
	nxs_index_t *idx = lua_nxs_index_getctx(L);
	uint64_t doc_id;
	const char *text;
	size_t len;

	doc_id = lua_tointeger(L, 2);
	luaL_argcheck(L, doc_id, 2, "document ID must be non-zero");

	text = lua_tolstring(L, 3, &len);
	luaL_argcheck(L, text && len, 3, "non-empty `string' expected");

	if (nxs_index_add(idx, doc_id, text, len) == -1) {
		lua_pushnil(L);
		lua_nxs_push_error(L);
		return 2;
	}
	lua_pushinteger(L, doc_id);
	lua_pushnil(L);
	return 2;
}

static int
lua_nxs_index_remove(lua_State *L)
{
	nxs_index_t *idx = lua_nxs_index_getctx(L);
	uint64_t doc_id;

	doc_id = lua_tointeger(L, 2);
	luaL_argcheck(L, doc_id, 2, "document ID must be non-zero");

	if (nxs_index_remove(idx, doc_id) == -1) {
		lua_pushnil(L);
		lua_nxs_push_error(L);
		return 2;
	}
	lua_pushinteger(L, doc_id);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

static int
lua_nxs_resp_acquire(lua_State *L, nxs_resp_t *resp)
{
	nxs_resp_t **p;

	if ((p = lua_newuserdata(L, sizeof(nxs_params_t *))) == NULL) {
		return -1;
	}
	*p = resp;
	luaL_getmetatable(L, NXS_RESP_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

static nxs_resp_t *
lua_nxs_resp_getctx(lua_State *L, int idx)
{
	void *ud = luaL_checkudata(L, idx, NXS_RESP_METATABLE);
	return *(nxs_resp_t **)ud;
}

static int
lua_nxs_resp_repr(lua_State *L)
{
	nxs_resp_t *resp = lua_nxs_resp_getctx(L, 1);
	const size_t count = nxs_resp_resultcount(resp);

	lua_createtable(L, 0, count);
	if (count) {
		nxs_doc_id_t doc_id;
		float score;

		nxs_resp_iter_reset(resp);
		while (nxs_resp_iter_result(resp, &doc_id, &score)) {
			lua_pushinteger(L, doc_id);
			lua_pushnumber(L, score);
			lua_settable(L, -3);
		}
	}
	return 1;
}

static int
lua_nxs_resp_tojson(lua_State *L)
{
	nxs_resp_t *resp = lua_nxs_resp_getctx(L, 1);
	char *json = nxs_resp_tojson(resp, NULL);
	lua_pushstring(L, json);
	free(json);
	return 1;
}

static int
lua_nxs_resp_gc(lua_State *L)
{
	nxs_resp_t *resp = lua_nxs_resp_getctx(L, 1);
	nxs_resp_release(resp);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

static int
lua_nxs_index_search(lua_State *L)
{
	nxs_index_t *idx = lua_nxs_index_getctx(L);
	nxs_params_t *params;
	nxs_resp_t *resp;
	const char *text;
	size_t len;
	int ret;

	text = lua_tolstring(L, 2, &len);
	luaL_argcheck(L, text && len, 2, "non-empty `string' expected");
	params = lua_isnoneornil(L, 3) ? NULL : lua_nxs_params_getctx(L, 3);

	resp = nxs_index_search(idx, params, text, len);
	if (resp == NULL) {
		lua_pushnil(L);
		lua_nxs_push_error(L);
		return 2;
	}
	if ((ret = lua_nxs_resp_acquire(L, resp)) == -1) {
		return luaL_error(L, "OOM");
	}
	lua_pushnil(L);
	return 2;
}

///////////////////////////////////////////////////////////////////////////////

static void
lua_nxs_setup_constants(lua_State *L)
{
	static const struct {
		const char *	name;
		int		value;
	} nxs_errors[] = {
		{ "ERR_SUCCEED",	NXS_ERR_SUCCESS	},
		{ "ERR_FATAL",		NXS_ERR_FATAL	},
		{ "ERR_SYSTEM",		NXS_ERR_SYSTEM	},
		{ "ERR_INVALID",	NXS_ERR_INVALID	},
		{ "ERR_EXISTS",		NXS_ERR_EXISTS	},
		{ "ERR_MISSING",	NXS_ERR_MISSING	},
		{ "ERR_LIMIT",		NXS_ERR_LIMIT	},
	};
	for (unsigned i = 0; i < __arraycount(nxs_errors); i++) {
		lua_pushinteger(L, nxs_errors[i].value);
		lua_setfield(L, -2, nxs_errors[i].name);
	}
}

static void
lua_push_class(lua_State *L, const char *name, const struct luaL_Reg *methods)
{
	if (luaL_newmetatable(L, name)) {
#if LUA_VERSION_NUM >= 502
		luaL_setfuncs(L, methods, 0);
#else
		luaL_register(L, NULL, methods);
#endif
		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);
}

__dso_public int
luaopen_nxsearch(lua_State *L)
{
	static const struct luaL_Reg nxs_lib_methods[] = {
		{ "open",	lua_nxs_index_open	},
		{ "create",	lua_nxs_index_create	},
		{ "destroy",	lua_nxs_index_destroy	},
		{ "newparams",	lua_nxs_params_create	},
		{ NULL,		NULL			},
	};
	static const struct luaL_Reg nxs_param_methods[] = {
		{ "fromjson",	lua_nxs_params_fromjson	},
		{ "__gc",	lua_nxs_params_gc	},
		{ NULL,		NULL			},
	};
	static const struct luaL_Reg nxs_index_methods[] = {
		{ "add",	lua_nxs_index_add	},
		{ "remove",	lua_nxs_index_remove	},
		{ "search",	lua_nxs_index_search	},
		{ "__gc",	lua_nxs_index_gc	},
		{ NULL,		NULL			},
	};
	static const struct luaL_Reg nxs_resp_methods[] = {
		{ "repr",	lua_nxs_resp_repr	},
		{ "tojson",	lua_nxs_resp_tojson	},
		{ "__gc",	lua_nxs_resp_gc		},
		{ NULL,		NULL			},
	};

	lua_push_class(L, NXS_PARAMS_METATABLE, nxs_param_methods);
	lua_push_class(L, NXS_IDX_METATABLE, nxs_index_methods);
	lua_push_class(L, NXS_RESP_METATABLE, nxs_resp_methods);

	if (!nxs) {
		static int lua_regkey_dtor;

		if ((nxs = nxs_open(NULL)) == NULL) {
			return luaL_error(L, "nxs_open() failed");
		}

		/*
		 * Lua doesn't provide a native luaclose_mylib support.
		 * Use a common workaround: create a dummy table which has
		 * __gc method set to a closure invoking our close function
		 * with a custom argument.  Push it into the registry so it
		 * gets invoked on Lua state destruction.
		 */
		lua_pushlightuserdata(L, &lua_regkey_dtor);
		lua_newuserdata(L, 0);
		lua_newtable(L);
		lua_pushliteral(L, "__gc");
		lua_pushcclosure(L, luaclose_nxsearch, 0);
		lua_rawset(L, -3);
		lua_setmetatable(L, -2);
		lua_settable(L, LUA_REGISTRYINDEX);
	}

#if LUA_VERSION_NUM >= 502
	luaL_newlib(L, nxs_lib_methods);
#else
	luaL_register(L, "nxsearch", nxs_lib_methods);
#endif
	lua_nxs_setup_constants(L);
	return 1;
}

static int
luaclose_nxsearch(lua_State *L)
{
	if (nxs) {
		nxs_close(nxs);
		nxs = NULL;
	}
	(void)L;
	return 0;
}
