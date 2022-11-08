/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * An interface for Lua filters.
 *
 * - Each filter gets a separate Lua state.
 * - It is generally just wrappers around the filters API.
 */

#include <stdlib.h>
#include <inttypes.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#define	__NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "filters.h"
#include "utils.h"

typedef enum {
	LF_CREATE = 0,
	LF_DESTROY,
	LF_FILTER,
	LF_CLEANUP,
	LF_COUNT
} lua_filtcall_t;

typedef struct {
	lua_State *		L;
	int			handlers[LF_COUNT];
	const filter_ops_t *	ops;
	const char *		code;
} lua_filtctx_t;

typedef struct {
	lua_filtctx_t *		ctx;
	int			arg_ref;
} lua_argref_t;

static void	luafilt_sysfini(void *);

static int
lua_getfield_wrapper(lua_State *L)
{
	const char *key = lua_tostring(L, 1);
	lua_getfield(L, 2, key);
	return 1;
}

static bool
lua_getfield_instack(nxs_t *nxs, lua_State *L, const char *key)
{
	lua_pushcfunction(L, lua_getfield_wrapper);
	lua_pushstring(L, key);
	lua_pushvalue(L, -3);

	if (lua_pcall(L, 2, 1, 0) != 0) {
		const char *errmsg = lua_tostring(L, -1);
		nxs_decl_errx(nxs, NXS_ERR_INVALID,
		    "invalid Lua code: %s", errmsg);
		lua_pop(L, 1);
		return false;
	}
	return true;
}

static void *
luafilt_sysinit(nxs_t *nxs, void *arg)
{
	static const char *handler_names[] = {
		[LF_CREATE]	= "create",
		[LF_DESTROY]	= "destroy",
		[LF_FILTER]	= "filter",
		[LF_CLEANUP]	= "cleanup",
	};
	lua_filtctx_t *lctx = arg;
	lua_State *L;

	L = luaL_newstate();
	luaL_openlibs(L);
	lctx->L = L;

	for (unsigned i = LF_CREATE; i < LF_COUNT; i++) {
		lctx->handlers[i] = LUA_REFNIL;
	}

	if (luaL_loadstring(L, lctx->code) != 0 || lua_pcall(L, 0, 1, 0) != 0) {
		const char *errmsg = lua_tostring(L, -1);
		nxs_decl_errx(nxs, NXS_ERR_INVALID, "Lua error: %s", errmsg);
		goto err;
	}
	if (lua_isnoneornil(L, -1)) {
		nxs_decl_errx(nxs, NXS_ERR_INVALID,
		    "invalid Lua code: missing table with operations", NULL);
		goto err;
	}
	for (unsigned i = LF_CREATE; i < LF_COUNT; i++) {
		if (!lua_getfield_instack(nxs, L, handler_names[i])) {
			goto err;
		}
		lctx->handlers[i] = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	if (lctx->handlers[LF_FILTER] == LUA_REFNIL) {
		nxs_decl_errx(nxs, NXS_ERR_INVALID,
		    "invalid Lua code: missing `filter' handler", NULL);
		goto err;
	}
	lua_pop(L, 1);
	ASSERT(lua_gettop(L) == 0);
	return lctx;
err:
	lua_settop(L, 0);
	luafilt_sysfini(lctx);
	return NULL;
}

static void
luafilt_sysfini(void *arg)
{
	lua_filtctx_t *lctx = arg;
	lua_State *L;

	if ((L = lctx->L) == NULL) {
		goto out;
	}
	ASSERT(lua_gettop(L) == 0);

	if (lctx->handlers[LF_CLEANUP] != LUA_REFNIL) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, lctx->handlers[LF_CLEANUP]);
		if (lua_pcall(L, 0, 0, 0) != 0) {
			app_dbgx("Lua error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}

	for (unsigned i = LF_CREATE; i < LF_COUNT; i++) {
		const int func_ref = lctx->handlers[i];

		if (func_ref != LUA_REFNIL) {
			luaL_unref(L, LUA_REGISTRYINDEX, func_ref);
		}
	}
	lua_close(L);
out:
	free(lctx);
}

static void *
luafilt_create(nxs_params_t *params, void *arg)
{
	lua_filtctx_t *lctx = arg;
	lua_State *L = lctx->L;
	lua_argref_t *xref;
	char *json;

	if ((xref = calloc(1, sizeof(lua_argref_t))) == NULL) {
		app_dbgx("Lua error: OOM", NULL);
		return NULL;
	}
	xref->ctx = lctx;
	xref->arg_ref = LUA_REFNIL;

	/* If no handler registered, then nothing to do. */
	if (lctx->handlers[LF_CREATE] == LUA_REFNIL) {
		return xref;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, lctx->handlers[LF_CREATE]); // +1

	/* Convert params to JSON and pass to Lua. */
	if ((json = nxs_params_tojson(params, NULL)) != NULL) {
		lua_pushstring(L, json); // +1
		free(json);
	} else {
		lua_pushnil(L); // +1
	}

	if (lua_pcall(L, 1, 2, 0) != 0) {
		app_dbgx("Lua error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return NULL;
	}

	if (lua_isnil(L, -2)) {
		app_dbgx("Lua error: %s", lua_tostring(L, -1));
		lua_pop(L, 2);
		return NULL;
	}
	lua_pop(L, 1); // -1

	xref->arg_ref = luaL_ref(L, LUA_REGISTRYINDEX); // -1
	ASSERT(lua_gettop(L) == 0);
	return xref;
}

static void
luafilt_destroy(void *arg)
{
	lua_argref_t *xref = arg;
	lua_filtctx_t *lctx = xref->ctx;
	lua_State *L = lctx->L;

	if (lctx->handlers[LF_DESTROY] != LUA_REFNIL) {
		const int func_ref = lctx->handlers[LF_DESTROY];
		lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref); // +1
		lua_rawgeti(L, LUA_REGISTRYINDEX, xref->arg_ref); // +1

		if (lua_pcall(L, 1, 0, 0) != 0) {
			app_dbgx("Lua error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
	luaL_unref(L, LUA_REGISTRYINDEX, xref->arg_ref);
	ASSERT(lua_gettop(L) == 0);
	free(xref);
}

static filter_action_t
luafilt_filter(void *arg, strbuf_t *buf)
{
	lua_argref_t *xref = arg;
	lua_filtctx_t *lctx = xref->ctx;
	lua_State *L = lctx->L;
	const char *val, *err;
	size_t len;

	ASSERT(lua_gettop(L) == 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, lctx->handlers[LF_FILTER]); // +1
	lua_rawgeti(L, LUA_REGISTRYINDEX, xref->arg_ref); // +1
	lua_pushlstring(L, buf->value, buf->length); // +1

	if (lua_pcall(L, 2, 2, 0) != 0) {
		app_dbgx("Lua error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return FILT_ERROR;
	}
	val = lua_tolstring(L, -2, &len);
	err = lua_tostring(L, -1);
	if (val == NULL) {
		if (err) {
			// XXX: return the error message
			app_dbgx("Lua filter() error: %s", err);
			lua_pop(L, 2);
			return FILT_ERROR;
		}
		lua_pop(L, 2);
		return FILT_DISCARD;
	}
	if (strbuf_acquire(buf, val, len) == -1) {
		app_dbg("strbuf_acquire failed", NULL);
		lua_pop(L, 2);
		return FILT_ERROR;
	}
	lua_pop(L, 2);
	ASSERT(lua_gettop(L) == 0);

	return FILT_MUTATION;
}

__dso_public int
nxs_luafilter_load(nxs_t *nxs, const char *name, const char *code)
{
	static const filter_ops_t luafilt_ops = {
		.sysinit	= luafilt_sysinit,
		.sysfini	= luafilt_sysfini,
		.create		= luafilt_create,
		.destroy	= luafilt_destroy,
		.filter		= luafilt_filter,
	};
	lua_filtctx_t *lctx;

	/*
	 * Create the context structure and set the operations.
	 * The rest will be initialized by the luafilt_sysinit().
	 */
	if ((lctx = calloc(1, sizeof(lua_filtctx_t))) == NULL) {
		return -1;
	}
	lctx->code = code;  // temporary
	lctx->ops = &luafilt_ops;

	return nxs_filter_register(nxs, name, &luafilt_ops, lctx);
}
