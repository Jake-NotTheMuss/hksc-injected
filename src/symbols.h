/*
** symbols.h
** Call of Duty API declarations (to be constructed from image offsets)
*/

#ifndef hks_symbols_h
#define hks_symbols_h

#if defined(LUA_CODT6)
/* offsets defined for singleplayer executable `t6sp.exe' */
#define DEFAULT_CALL __cdecl
#define OFFS_hksi_hks_newstate     0x8a940
#define OFFS_lua_close             0x82630
#define OFFS_hks_stateSettings     0x1eefc0
#define OFFS_hksi_hks_dump         0x15fde0
#define OFFS_hks_defaultAllocator  0x18cc80
#define OFFS_hks_load              0x185600
#define OFFS_lua_pushstring        0x286580
#define OFFS_lua_pushlstring       0xe1eb0
#define OFFS_hksi_lua_pushfstring  0x1ea80
#define OFFS_runProtectedAndFixStack  0x370b0
#define OFFS_hks_error             0xc4fe0
#define OFFS_lua_tolstring         0x14eaa0

/*
`t6zm.exe' offsets for reference
INIT_SYMBOL(hksi_hks_newstate,      0x197410);
INIT_SYMBOL(lua_close,              0x1fbc70);
INIT_SYMBOL(hks_stateSettings,      0x1f73a0);
INIT_SYMBOL(hksi_hks_dump,          0x15bf00);
INIT_SYMBOL(hks_defaultAllocator,   0xa58e0);
INIT_SYMBOL(hks_load,               0x1ff420);
INIT_SYMBOL(lua_pushstring,         0x213720);
INIT_SYMBOL(lua_pushlstring,        0xd8cc0);
INIT_SYMBOL(hksi_lua_pushfstring,   0x1739c0);
INIT_SYMBOL(lua_pcall,              0x11360);
*/
#elif defined(LUA_CODT7)
#define DEFAULT_CALL __fastcall
#define OFFS_hksi_hks_newstate     0x1d4c250
#define OFFS_lua_close             0x1d53600
#define OFFS_hks_stateSettings     0x1d38f60
#define OFFS_hksi_hks_dump         0x1d4be40
#define OFFS_hks_defaultAllocator  0x1d49d40
#define OFFS_hks_load              0x1d3afb0
#define OFFS_lua_pushstring        0xa186b0
#define OFFS_lua_pushlstring       0xa18430
#define OFFS_hksi_lua_pushfstring  0x1d4e570
#define OFFS_runProtectedAndFixStack  0x1d6a290
#define OFFS_hks_error             0x1d4c060
#define OFFS_lua_remove            0x1d53df0
#define OFFS_lua_tolstring         0x1d4eed0
#else
#error Unsupported title
#endif

#define DECL_API1(call, name, ret, par) \
  typedef ret (call *t_ ## name) par; \
  static t_ ## name p_ ## name = NULL

#define DECL_API(name, ret, par) DECL_API1(DEFAULT_CALL, name, ret, par)


/*
** luaL_error
*/
DECL_API(luaL_error, int, (lua_State *s, const char *fmt, ...));
#define lua_error  (*p_luaL_error)


/*
** hksi_hks_newstate - create a new Lua state
*/
DECL_API(hksi_hks_newstate, lua_State *, (HksStateSettings *settings));
#define hksi_hks_newstate  (*p_hksi_hks_newstate)


/*
** lua_close - close a Lua state
*/
DECL_API(lua_close, void, (lua_State *s));
#define lua_close  (*p_lua_close)


/*
** hks_stateSettings - set default Lua state settings
*/
DECL_API1(__fastcall, hks_stateSettings, HksStateSettings *,
         (HksStateSettings *settings));
#define hks_stateSettings  (*p_hks_stateSettings)


/*
** hksi_hks_dump - dump a Lua function as a precompiled chunk
*/
DECL_API(hksi_hks_dump, int, (lua_State *s, lua_Writer writer, void *data,
                              int strippingLevel));
#define hksi_hks_dump  (*p_hksi_hks_dump)


/*
** hks_defaultAllocator
*/
DECL_API(hks_defaultAllocator, void *,
         (void *userData, void *oldMemory, size_t oldSize, size_t newSize));
#define hks_defaultAllocator  (*p_hks_defaultAllocator)


/*
** hks_load
*/
DECL_API(hks_load, int,
    (lua_State *s, HksCompilerSettings *options, lua_Reader reader,
     void *readerData, lua_Reader debugReader, void *debugReaderData,
     const char *chunkName));
#define hks_load  (*p_hks_load)


/*
** lua_pushstring
*/
DECL_API(lua_pushstring, void, (lua_State *s, const char *str));
#define lua_pushstring  (*p_lua_pushstring)


/*
** lua_pushlstring
*/
DECL_API(lua_pushlstring, void, (lua_State *s, const char *str, size_t l));
#define lua_pushlstring  (*p_lua_pushlstring)


/*
** hksi_lua_pushfstring
*/
DECL_API(hksi_lua_pushfstring, const char *,
         (lua_State *s, const char *fmt, ...));
#define hksi_lua_pushfstring  (*p_hksi_lua_pushfstring)


/*
** lua_pcall
*/
DECL_API(runProtectedAndFixStack, int, (lua_State *s, lua_Caller f, void *arg,
                                        int numResults));
#define runProtectedAndFixStack  (*p_runProtectedAndFixStack)


/*
** hks_error
*/
DECL_API(hks_error, int, (lua_State *s, int errcode));
#define hks_error  (*p_hks_error)


/*
** lua_tolstring
*/
DECL_API(lua_tolstring, const char *, (lua_State *s, int index, size_t *len));
#define lua_tolstring  (*p_lua_tolstring)


/*
** lua_remove
*/
DECL_API(lua_remove, void, (lua_State *s, int index));
#define lua_remove  (*p_lua_remove)


#undef DECL_API
#undef DECL_API1

#define OFFS(x) ((uintptr_t)GetModuleHandle(NULL) + x)
#define INIT_DATA(name,t) name = (t)OFFS(OFFS_##name)
#define INIT_SYMBOL(name) p_##name = (t_##name)OFFS(OFFS_##name)

#endif /* hks_symbols_h */
