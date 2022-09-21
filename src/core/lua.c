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

#define	NXS_IDX_METATABLE	"nxs-idx-methods"

typedef struct {
	nxs_index_t *	index;
} nxs_lua_t;

static nxs_t *	nxs = NULL;

int		luaopen_nxsearch(lua_State *);
static int	luaclose_nxsearch(lua_State *L);

///////////////////////////////////////////////////////////////////////////////

static int
lua_nxs_newctx(lua_State *L, nxs_index_t *idx)
{
	nxs_lua_t *lctx;

	lctx = (nxs_lua_t *)lua_newuserdata(L, sizeof(nxs_lua_t));
	if (lctx == NULL) {
		return -1;
	}
	lctx->index = idx;
	luaL_getmetatable(L, NXS_IDX_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

static nxs_lua_t *
lua_nxs_getctx(lua_State *L)
{
	void *ud = luaL_checkudata(L, 1, NXS_IDX_METATABLE);
	luaL_argcheck(L, ud != NULL, 1, "`" NXS_IDX_METATABLE"' expected");
	return (nxs_lua_t*)ud;
}

static int
lua_nxs_index_gc(lua_State *L)
{
	nxs_lua_t *lctx = lua_nxs_getctx(L);
	nxs_index_close(nxs, lctx->index);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

static int
lua_nxs_open(lua_State *L)
{
	const char *name;
	nxs_index_t *idx;
	int ret;

	name = lua_tostring(L, 1);
	luaL_argcheck(L, name, 1, "non-empty `string' expected");

	if ((idx = nxs_index_open(nxs, name)) == NULL) {
		return luaL_error(L, "nxs_index_open failed");  // XXX
	}
	if ((ret = lua_nxs_newctx(L, idx)) == -1) {
		nxs_index_close(nxs, idx);
		return luaL_error(L, "OOM");
	}
	return ret;
}

static int
lua_nxs_create(lua_State *L)
{
	const char *name;
	nxs_index_t *idx;
	int ret;

	name = lua_tostring(L, 1);
	luaL_argcheck(L, name, 1, "non-empty `string' expected");

	if ((idx = nxs_index_create(nxs, name)) == NULL) {
		return luaL_error(L, "nxs_index_create failed");  // XXX
	}
	if ((ret = lua_nxs_newctx(L, idx)) == -1) {
		nxs_index_close(nxs, idx);
		return luaL_error(L, "OOM");
	}
	return ret;
}

static int
lua_nxs_destroy(lua_State *L)
{
	return luaL_error(L, "not yet implemented");
}

static int
lua_nxs_index_add(lua_State *L)
{
	nxs_lua_t *lctx = lua_nxs_getctx(L);
	uint64_t doc_id;
	const char *text;
	size_t len;

	doc_id = lua_tointeger(L, 2);
	text = lua_tolstring(L, 3, &len);
	luaL_argcheck(L, text && len, 3, "non-empty `string' expected");

	if (nxs_index_add(lctx->index, doc_id, text, len) == -1) {
		return luaL_error(L, "nxsearch internal error");
	}
	lua_pushinteger(L, doc_id);
	return 1;
}

static int
lua_nxs_index_search(lua_State *L)
{
	nxs_lua_t *lctx = lua_nxs_getctx(L);
	nxs_resp_t *resp;
	const char *text;
	size_t len, count;

	text = lua_tolstring(L, 2, &len);
	luaL_argcheck(L, text && len, 2, "non-empty `string' expected");

	resp = nxs_index_search(lctx->index, text, len);
	if (resp == NULL) {
		return luaL_error(L, "nxsearch internal error");
	}

	count = nxs_resp_resultcount(resp);
	lua_createtable(L, 0, count);
	if (count) {
		nxs_doc_id_t doc_id;
		float score;

		while (nxs_resp_iter_result(resp, &doc_id, &score)) {
			lua_pushinteger(L, doc_id);
			lua_pushnumber(L, score);
			lua_settable(L, -3);
		}
	}
	nxs_resp_release(resp);
	return 1;
}

static int
luaclose_nxsearch(lua_State *L)
{
	if (nxs) {
		nxs_destroy(nxs);
		nxs = NULL;
	}
	(void)L;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

__dso_public int
luaopen_nxsearch(lua_State *L)
{
	static const struct luaL_Reg nxs_lib_methods[] = {
		{ "open",	lua_nxs_open	},
		{ "create",	lua_nxs_create	},
		{ "destroy",	lua_nxs_destroy	},
		{ NULL,		NULL		},
	};
	static const struct luaL_Reg nxs_index_methods[] = {
		{ "add",	lua_nxs_index_add	},
		{ "search",	lua_nxs_index_search	},
		//{ "remove",	lua_nxs_index_remove	},
		{ "__gc",	lua_nxs_index_gc	},
		{ NULL,		NULL			},
	};

	if (luaL_newmetatable(L, NXS_IDX_METATABLE)) {
#if LUA_VERSION_NUM >= 502
		luaL_setfuncs(L, nxs_index_methods, 0);
#else
		luaL_register(L, NULL, nxs_index_methods);
#endif
		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);

	if (!nxs) {
		static int lua_regkey_dtor;

		if ((nxs = nxs_create(NULL)) == NULL) {
			return luaL_error(L, "nxs_create failed");
		}

		/*
		 * Lua doesn't provide a native luaclose_mylib support.
		 * Use a common workaround: create a dummy table which has
		 * __gc method set to a closure invoking our close function
		 * with a custom argument.  Push it into the registry so it
		 * gets invoked on Lua state destruction.
		 */
		lua_newuserdata(L, 0);
		lua_newtable(L);
		lua_pushcclosure(L, luaclose_nxsearch, 0);
		lua_setfield(L, -2, "__gc");
		lua_setmetatable(L, -2);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &lua_regkey_dtor);
	}

#if LUA_VERSION_NUM >= 502
	luaL_newlib(L, nxs_lib_methods);
#else
	luaL_register(L, "nxsearch", nxs_lib_methods);
#endif
	return 1;
}
