/*
** hks.h
** Partial Havok Script API reconstruction
*/


#ifndef hks_h
#define hks_h

#include <limits.h>

#define LUAL_BUFFERSIZE BUFSIZ

#define LUA_REGISTRYINDEX (-10000)
#define LUA_ENVIRONINDEX  (-10001)
#define LUA_GLOBALSINDEX  (-10002)
#define lua_upvalueindex(i) (LUA_GLOBALSINDEX-(i))

#define LUA_ERRERR (-300)
#define LUA_ERRMEM (-200)
#define LUA_ERRRUN (-100)
#define LUA_ERRFILE (-5)
#define LUA_ERRSYNTAX (-4)

/* bytecode sharing formats */
#define BYTECODE_DEFAULT 0
#define BYTECODE_INPLACE 1
#define BYTECODE_REFERENCED 2

/* bytecode sharing modes */
#define HKS_BYTECODE_SHARING_OFF 0
#define HKS_BYTECODE_SHARING_ON 1
#define HKS_BYTECODE_SHARING_SECURE 2

/* bytecode stripping levels */
#define BYTECODE_STRIPPING_NONE 0
#define BYTECODE_STRIPPING_PROFILING 1
#define BYTECODE_STRIPPING_ALL 2
#define BYTECODE_STRIPPING_DEBUG_ONLY 3
#define BYTECODE_STRIPPING_CALLSTACK_RECONSTRUCTION 4

/* int literal options */
#define INT_LITERALS_NONE 0
#define INT_LITERALS_LUD 1
#define INT_LITERALS_UI64 2
#define INT_LITERALS_ALL 3

/* bytecode endianness */
#define HKS_BYTECODE_DEFAULT_ENDIAN 0
#define HKS_BYTECODE_BIG_ENDIAN 1
#define HKS_BYTECODE_LITTLE_ENDIAN 2

/*
** basic types
*/
#define LUA_TANY    (-2)
#define LUA_TNONE   (-1)

#define LUA_TNIL    0
#define LUA_TBOOLEAN    1
#define LUA_TLIGHTUSERDATA  2
#define LUA_TNUMBER   3
#define LUA_TSTRING   4
#define LUA_TTABLE    5
#define LUA_TFUNCTION   6
#define LUA_TUSERDATA   7
#define LUA_TTHREAD   8
#define LUA_TIFUNCTION  9
#define LUA_TCFUNCTION  10
#define LUA_TUI64       11
#define LUA_TSTRUCT     12

#define LUA_NUM_TYPE_OBJECTS 14

typedef struct lua_State lua_State;

typedef int (*lua_CFunction) (lua_State *s);

#if INT_MAX-20 < 32760
#define LUAI_BITS_INT 16
#elif INT_MAX > 2147483640L
/* int has at least 32 bits */
#define LUAI_BITS_INT 32
#else
#error "you must define LUA_BITSINT with number of bits in an integer"
#endif

#if LUAI_BITS_INT >= 32
typedef unsigned int lu_int32;
typedef int l_int32;
#else
typedef unsigned long lu_int32;
typedef long l_int32;
#endif

typedef unsigned char lu_byte;

typedef lu_int32 Instruction;

/*
** functions that read/write blocks when loading/dumping Lua chunks
*/
typedef const char * (*lua_Reader) (lua_State *s, void *ud, size_t *sz);

typedef int (*lua_Writer) (lua_State *s, const void* p, size_t sz, void* ud);


/*
** prototype for memory-allocation functions
*/
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);

typedef void (*lua_Caller) (lua_State *s, void *ud, int nresults,
                            Instruction *instr);

typedef struct HksCompilerSettings HksCompilerSettings;

typedef int (*hks_debug_map)(const char *, int);

struct HksCompilerSettings {
  lu_byte m_emitStructCode;
  const char **m_stripNames;
  int m_bytecodeSharingFormat;
  int m_enableIntLiterals;
  hks_debug_map m_debugMap;
};


typedef struct HksStateSettings HksStateSettings;
struct HksStateSettings {
  l_int32 m_gcPause;
  l_int32 m_gcStepMul;
  size_t m_gcEmergencyMemorySize;
  void *m_emergencyGCFailFunction;
  lua_Alloc m_allocator;
  void *m_allocatorData;
  lua_CFunction m_panicFunction;
  void *m_logFunction;
  const char *m_name;
  lu_int32 m_initialRegistrySize;
  lu_int32 m_initialRegistryArraySize;
  lu_int32 m_initialGlobalSize;
  lu_int32 m_initialStringTableSize;
  HksCompilerSettings m_compilerSettings;
  void *m_debugObject;
  int m_heapAssertionFrequency;
  lua_CFunction m_gcPolicy;
  int m_bytecodeSharingMode;
  int m_bytecodeDumpEndianness;
  lu_int32 m_gcWeakStackSize;
};


#endif /* hks_h */
