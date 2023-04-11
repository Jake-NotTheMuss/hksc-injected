/*
** compiler.c
** Call of Duty T6 Lua compiler interface
*/


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#if !defined(LUA_CODT7) && !defined(LUA_CODT6)
#error You must define either LUA_CODT6 or LUA_CODT7
#endif

#include "hks.h"

#include "hksccontext.h"

#include "symbols.h"

#define lua_throw  hks_error
#define lua_tostring(s,idx)  lua_tolstring(s,idx,NULL)
#define lua_pushfstring hksi_lua_pushfstring

typedef struct {
  HANDLE ctx_handle; /* handle to shared context */
  hksc_Context *ctx; /* shared context */
  struct ctx_data_table data_table; /* pointers to shared data items */
  lua_State *luaVM; /* the Lua compiler state */
  char *prevcwd; /* the previous  directory of the victim process */
} MidEndState;


static void die(MidEndState *me);


static void emiterrorv(MidEndState *me, const char *fmt, va_list argp)
{
  if (me->data_table.progname)
    fprintf(stderr, "%s: ", me->data_table.progname);
  vfprintf(stderr, fmt, argp);
  fputc('\n', stderr);
}


static void emiterror(MidEndState *me, const char *fmt, ...)
{
  va_list argp;
  va_start(argp, fmt);
  emiterrorv(me, fmt, argp);
  va_end(argp);
}


static void fatal(MidEndState *me, const char *fmt, ...)
{
  va_list argp;
  va_start(argp, fmt);
  emiterrorv(me, fmt, argp);
  va_end(argp);
  me->ctx->status = LUA_ERRRUN;
  die(me);
}


/* call this when a windows API call fails */
static void fatalsys(MidEndState *me, const char *fmt, ...)
{
  va_list argp;
  DWORD error = GetLastError();
  LPSTR msg = NULL;
  if (me->data_table.progname)
    fprintf(stderr, "%s: ", me->data_table.progname);
  va_start(argp, fmt);
  vfprintf(stderr, fmt, argp);
  va_end(argp);
  fputs(": ", stderr);
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                 FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0,
                 NULL);
  fprintf(stderr, "%s\n", msg);
  LocalFree(msg);
  me->ctx->status = LUA_ERRRUN;
  die(me);
}


/* Initialize symbols in Call of Duty */
static void init_symbols(void) {
  INIT_SYMBOL(hksi_hks_newstate);
  INIT_SYMBOL(lua_close);
  INIT_SYMBOL(hks_stateSettings);
  INIT_SYMBOL(hksi_hks_dump);
  INIT_SYMBOL(hks_defaultAllocator);
  INIT_SYMBOL(hks_load);
  INIT_SYMBOL(lua_pushstring);
  INIT_SYMBOL(lua_pushlstring);
  INIT_SYMBOL(hksi_lua_pushfstring);
  INIT_SYMBOL(runProtectedAndFixStack);
  INIT_SYMBOL(hks_error);
  INIT_SYMBOL(lua_tolstring);
}


static lua_State *create_compiler_state(hksc_Context *ctx)
{
  lua_State *s;
  HksStateSettings settings;
  hks_stateSettings(&settings);
  settings.m_name = "Hksc Compiler State";
  settings.m_bytecodeSharingMode = HKS_BYTECODE_SHARING_OFF;
  settings.m_bytecodeDumpEndianness = HKS_BYTECODE_DEFAULT_ENDIAN;
  settings.m_compilerSettings.m_bytecodeSharingFormat = BYTECODE_INPLACE;
  settings.m_compilerSettings.m_enableIntLiterals = ctx->enable_int_literals;
  s = hksi_hks_newstate(&settings);
  return s;
}


static void close_compiler_state(lua_State *s)
{
  if (s != NULL)
    lua_close(s);
}


static void cleanFileName(const char *instr, char *outstr) {
  int numdots = 0;
  int length = 0;
  while (*instr != '\0' && length < (MAX_PATH-1)) {
    if (*instr == '.')
      numdots++;
    else if (numdots == 1 && (*instr == '/' || *instr == '\\')) /* omit `./' */
      numdots = 0;
    else {
      for (; numdots > 0; numdots--)
        *outstr++ = '.';
      *outstr++ = *instr;
    }
    instr++;
    length++;
  }
  *outstr = '\0';
}


/*
** {======================================================
** Load functions
** =======================================================
*/

typedef struct LoadF {
  int extraline;
  FILE *f;
  char buff[LUAL_BUFFERSIZE];
} LoadF;


static const char *getF (lua_State *s, void *ud, size_t *size) {
  LoadF *lf = (LoadF *)ud;
  (void)s;
  if (lf->extraline) {
    lf->extraline = 0;
    *size = 1;
    strcpy(lf->buff, "\n");
    return lf->buff;
  }
  if (feof(lf->f)) return NULL;
  *size = fread(lf->buff, 1, LUAL_BUFFERSIZE, lf->f);
  /* This needs to be done to ensure predictability as the Havok Script code
     may still read bytes past the end of *size. This is a problem when testing
     the BOM files, because uninitialized data after position *size in the
     buffer will still be dereferenced as if it were a part of the source file.
     This is a Havok Script bug */
  if (*size < LUAL_BUFFERSIZE)
    memset(lf->buff + *size, -1, LUAL_BUFFERSIZE - *size);
  return (*size > 0) ? lf->buff : NULL;
}


static ctxtag_t gettag(const char **p)
{
  const char *s = *p;
  ctxtag_t tag = *(ctxtag_t *)s;
  s += sizeof(ctxtag_t);
  *p = s;
  return tag;
}


static const char *gettaggedstring(const char **p, size_t *psize)
{
  const char *str;
  const char *s = *p;
  size_t size;
  s = (const char *)aligned2dword(s);
  size = *((DWORD *)s);
  s += sizeof(DWORD);
  str = s;
  s += size;
  if (psize) *psize = size;
  *p = s;
  return str;
}


#define generatechunkname(me,s,filename)  remapfilenameex(me,s,filename,"@")
#define remapfilename(me,s,filename)  remapfilenameex(me,s,filename,"")

static const char *remapfilenameex(MidEndState *me, lua_State *s,
                                 const char *filename, const char *addprefix)
{
  const char *name; /* the part to keep in filename */
  const char *prefix_maps = me->data_table.prefix_maps;
  if (addprefix == NULL) addprefix = "";
  if (prefix_maps) {
    const char *limit = getdata(me->ctx) + me->ctx->sizedata;
    const char *prefix_map;
    while (prefix_maps < limit) {
      const char *p;
      size_t old_len;
      size_t size;
      ctxtag_t tag = gettag(&prefix_maps);
      if (tag != CTX_TAG_FILEPREFIXMAP)
        break;
      prefix_map = gettaggedstring(&prefix_maps, &size);
      p = strrchr(prefix_map, '=');
      if (!p)
        continue;
      old_len = p - prefix_map;
      p++;
      if (strncmp(filename, prefix_map, old_len) == 0) {
        name = filename + old_len;
        return hksi_lua_pushfstring(s, "%s%s%s", addprefix, p, name);
      }
    }
  }
  return hksi_lua_pushfstring(s, "%s%s", addprefix, filename);
}


static int hksL_loadfile (MidEndState *me, HksCompilerSettings *options,
                          const char *filename) {
  lua_State *s = me->luaVM;
  LoadF lf;
  int status;
  int c;
  const char *chunkname;
  char cleanedfilename[MAX_PATH];
  lf.extraline = 0;
  lf.f = fopen(filename, "r");
  if (lf.f == NULL) {
    const char *serr = strerror(errno);
    lua_pushfstring(s, "cannot open %s: %s", filename, serr);
    return LUA_ERRFILE;
  }
  cleanFileName(filename, cleanedfilename);
  chunkname = generatechunkname(me, s, cleanedfilename);
  c = getc(lf.f);
  if (c == '#') {  /* Unix exec. file? */
    lf.extraline = 1;
    while ((c = getc(lf.f)) != EOF && c != '\n') ;  /* skip first line */
    if (c == '\n') c = getc(lf.f);
  }
  ungetc(c, lf.f);
  status = hks_load(s, options, getF, &lf, NULL, NULL, chunkname);
  fclose(lf.f);  /* close file (even in case of errors) */
  return status;
}


static int hks_identity_map (const char *filename, int lua_line)
{
  (void)filename;
  return lua_line;
}

static int luaL_loadfile(MidEndState *me, hksc_Context *ctx,
                         const char *filename)
{
  HksCompilerSettings settings;
  settings.m_emitStructCode = 1;
  settings.m_stripNames = NULL;
  settings.m_bytecodeSharingFormat = BYTECODE_INPLACE;
  settings.m_enableIntLiterals = ctx->enable_int_literals;
  settings.m_debugMap = hks_identity_map;
  return hksL_loadfile(me, &settings, filename);
}


/* }====================================================== */


/******************************************************************************/
/* Lua parsing and dumping */
/******************************************************************************/

/* lua_Writer for dumping precompiled chunks to a file */
static int writer(lua_State *s, const void *p, size_t size, void *ud) {
  (void)s;
  return (fwrite(p,size,1,(FILE *)ud)!=1) && (size!=0);
}


static void logtopmsg(MidEndState *me, lua_State *s, int errcode, FILE *f)
{
  const char *msg = lua_tostring(s, -1);
  if (errcode == 0 || msg == NULL || f == NULL)
    return;
  if (errcode == LUA_ERRSYNTAX)
    fprintf(f, "%s\n", msg);
  else {
    if (me->data_table.progname)
      fprintf(f, "%s: ", me->data_table.progname);
    fprintf(f, "%s\n", msg);
  }
}

/* also removes the extension from filename */
static const char *getbasename(lua_State *s, const char *filename)
{
  const char *base;
  const char *dot = strrchr(filename, '.');
  if (dot) {
    size_t basename_len = (size_t)(dot-filename);
    if (basename_len != 0) {
      lua_pushlstring(s, filename, basename_len);
      filename = lua_tostring(s, -1);
    }
  }
  if (isalpha(filename[0]) && filename[1] == ':')
    filename += 2;
  for (base = filename; *filename; filename++) {
    if (*filename == '/' || *filename == '\\')
      base = filename + 1;
  }
  return base;
}


static int precompile_lua(MidEndState *me, const char *filename)
{
  hksc_Context *ctx = me->ctx;
  lua_State *s = me->luaVM;
  struct {
    const char *name;
    int strip;
  } dumpdata[3];
  int i;
  int numoutputs=ctx->strip ? 1 : 3;
  int status;
  const char *basename = getbasename(s, filename);
  const char *outname_c = me->data_table.outname_c;
  const char *outname_p = me->data_table.outname_p;
  const char *outname_d = me->data_table.outname_d;
  if (outname_c && *outname_c != '\0')
    basename = getbasename(s, outname_c);
  else
    outname_c = hksi_lua_pushfstring(s, "%s.luac", basename);
  if (ctx->strip)
    goto compile;
  if (outname_p == NULL || *outname_p == '\0')
    outname_p = hksi_lua_pushfstring(s, "%s.luacallstackdb", basename);
  if (outname_d == NULL || *outname_d == '\0')
    outname_d = hksi_lua_pushfstring(s, "%s.luadebug", basename);
  dumpdata[1].name = outname_p;
  dumpdata[1].strip = BYTECODE_STRIPPING_CALLSTACK_RECONSTRUCTION;
  dumpdata[2].name = outname_d;
  dumpdata[2].strip = BYTECODE_STRIPPING_DEBUG_ONLY;
  compile:
  dumpdata[0].name = outname_c;
  dumpdata[0].strip = BYTECODE_STRIPPING_ALL;
  status = luaL_loadfile(me, ctx, filename);
  if (status) {
    logtopmsg(me, s, status, stderr);
    lua_throw(s, status); /* return the error */
  }
  for (i = 0; i < numoutputs; i++) {
    FILE *f;
    f = fopen(dumpdata[i].name, "wb");
    if (f == NULL) {
      const char *serr = strerror(errno);
      emiterror(me, "cannot open '%s': %s", dumpdata[i].name, serr);
      lua_throw(s, LUA_ERRFILE);
    }
    status = hksi_hks_dump(s, writer, f, dumpdata[i].strip);
    fclose(f);
    if (status) {
      logtopmsg(me, s, status, stderr);
      lua_throw(s, status); /* return the error */
    }
  }
  return status;
}

/*
** parse a file, expecting a syntax error and log the error to a file
*/
static int compile_lua_expect_error(MidEndState *me, const char *filename)
{
  hksc_Context *ctx = me->ctx;
  lua_State *s = me->luaVM;
  FILE *expectfile;
  int status;
  const char *expectname = me->data_table.outname_c;
  if (expectname == NULL || *expectname == '\0')
    expectname = hksi_lua_pushfstring(s, "%s.expect", getbasename(s, filename));
  status = luaL_loadfile(me, ctx, filename);
  if (status && status != LUA_ERRSYNTAX) {
    logtopmsg(me, s, status, stderr);
    lua_throw(s, status); /* return the error */
  }
  else if (status == LUA_ERRSYNTAX) { /* syntax error as expected */
    expectfile = fopen(expectname, "wb");
    if (expectfile == NULL) {
      const char *serr = strerror(errno);
      emiterror(me, "cannot open '%s': %s", expectname, serr);
      lua_throw(s, LUA_ERRFILE);
    }
    logtopmsg(me, s, status, expectfile);
    fclose(expectfile);
    status = 0;
  }
  else {
    fprintf(stderr, "%s: error expected but source is valid\n",
            remapfilename(me, s, filename));
    status = LUA_ERRSYNTAX;
  }
  return status;
}


/******************************************************************************/
/* Lua compilation test execution */
/******************************************************************************/

/* execute a compilation test */
static void compilefile(MidEndState *me, const char *name) {
  precompile_lua(me, name); /* compile and dump */
}

static void expecterror(MidEndState *me, const char *name) {
  compile_lua_expect_error(me, name);
}


/* action to perform with a single file */
typedef void (*filefunc)(MidEndState *me, const char *name);

/* perform an action on a file inside a protected call */
struct FileAction {
  filefunc action;
  MidEndState *me;
  const char *name;
};

static void f_doaction(lua_State *s, void *ud, int nresults, Instruction *instr)
{
  struct FileAction *fa;
  (void)s; (void)nresults; (void)instr;
  fa = (struct FileAction *)ud;
  (*fa->action)(fa->me, fa->name);
}


/* parse shared context data and process input files */
static int forfiles(MidEndState *me, filefunc action) {
  hksc_Context *ctx = me->ctx;
  struct ctx_data_table *data_table = &me->data_table;
  lua_State *s = me->luaVM;
  struct FileAction fa;
  int error = 0;
  int status;
  fa.me = me;
  fa.action = action;
  const char *limit = getdata(ctx) + ctx->sizedata;
  const char *infiles = data_table->infiles;
  while (infiles < limit) {
    const char *filename;
    size_t size;
    ctxtag_t tag = gettag(&infiles);
    if (tag != CTX_TAG_INFILE) /* end of input files */
      break;
    filename = gettaggedstring(&infiles, &size);
    fa.name = filename;
    status = runProtectedAndFixStack(s, f_doaction, &fa, 0);
    if (status && status != LUA_ERRSYNTAX)
      return status;
    else if (status == LUA_ERRSYNTAX && !error)
      error = status;
  }
  return error;
}


static int parse_ctx(MidEndState *me)
{
  hksc_Context *ctx = me->ctx;
  struct ctx_data_table *data_table = &me->data_table;
  const char *data = getdata(ctx);
  const char *limit = data + ctx->sizedata;
  int i = 0;
  memset(data_table, 0, sizeof(struct ctx_data_table));
  while (data < limit) {
    const char *str;
    size_t size;
    const ctxtag_t *tag = (ctxtag_t *)data;
    if (*tag == CTX_TAG_EOS)
      break;
    data += sizeof(ctxtag_t);
    str = gettaggedstring(&data, &size);
    switch (*tag) {
      case CTX_TAG_PROGRAM_NAME:
        data_table->progname = str;
        break;
      case CTX_TAG_CWD:
        data_table->cwd = str;
        break;
      case CTX_TAG_FILEPREFIXMAP:
        if (data_table->prefix_maps == NULL)
          data_table->prefix_maps = (const char *)tag;
        break;
      case CTX_TAG_OUTNAME_C:
        data_table->outname_c = str;
        break;
      case CTX_TAG_OUTNAME_P:
        data_table->outname_p = str;
        break;
      case CTX_TAG_OUTNAME_D:
        data_table->outname_d = str;
        break;
      case CTX_TAG_INFILE:
        if (data_table->infiles == NULL)
          data_table->infiles = (const char *)tag;
        break;
      case CTX_TAG_DLL_NAME:
        data_table->dll_name = str;
        break;
      default: {
        return 1;
      }
    }
    i++;
  }
  return 0;
}


/******************************************************************************/
/* utilities for switching between the <game> and <test> directories */
/******************************************************************************/


static void cd2luadir(MidEndState *me)
{
  DWORD sizeneeded = GetCurrentDirectoryA(0, NULL);
  if (sizeneeded == 0)
    goto fail;
  char *prevcwd = malloc(sizeneeded);
  if (prevcwd == NULL) {
    const char *serr = strerror(errno);
    fatal(me, "cannot change working directory: %s", serr);
  }
  me->prevcwd = prevcwd;
  if (!GetCurrentDirectoryA(sizeneeded, prevcwd)) {
    fail:
    fatalsys(me, "cannot get current working directory");
  }
  if (!SetCurrentDirectoryA(me->data_table.cwd))
    fatalsys(me, "cannot change working directory to '%s'", me->data_table.cwd);
}


static void cd2prevdir(MidEndState *me)
{
  if (me->prevcwd) {
    SetCurrentDirectoryA(me->prevcwd);  /* reset current dircetory */
    free(me->prevcwd);
    me->prevcwd = NULL;
  }
}


static hksc_Context *get_context(HANDLE ctx_handle)
{
  hksc_Context *ctx;
  ctx = (hksc_Context *)MapViewOfFile(ctx_handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (ctx == NULL) {
    /* you can't tell the user yet because no console is attached yet and the
       PID that is read from the file mapping is needed to attach */
    /*fatalsys("cannot create map shared context into backend address space");*/
    return NULL;
  }
  return ctx;
}


static void close_context(hksc_Context *ctx)
{
  if (ctx)
    UnmapViewOfFile(ctx);
}

/* do not call fatal or fatalsys from here */
static void die(MidEndState *me)
{
  HMODULE dll;
  int status;
  hksc_Context *ctx = me->ctx;
  cd2prevdir(me);
  dll = GetModuleHandleA(me->data_table.dll_name);
  if (dll == NULL) {
    emiterror(me, "internal error: DLL module name '%s' is invalid: "
              "terminating target process");
    ExitProcess(1);
  }
  FreeConsole();
  close_compiler_state(me->luaVM);
  me->luaVM = NULL;
  close_context(me->ctx);
  me->ctx = NULL;
  CloseHandle(me->ctx_handle);
  me->ctx_handle = NULL;
  status = me->ctx ? me->ctx->status : LUA_ERRRUN;
  FreeLibraryAndExitThread(dll, status);
}


/* wait for CRT initialization */
static void wait_for_target(void) {
#if defined(LUA_CODT7)
  while (*(int *)OFFS(0x1a8a6400) == 0) ;
#elif defined(LUA_CODT6)
  while (*(int *)OFFS(0x3d83bc0) == 0) ;
#endif /* LUA_CODT7 */
}


#ifdef __cplusplus
extern "C" {
#endif

/*
** the middle-end main function, interfaces between the injector and victim
*/
void __declspec(dllexport) middlelev (HANDLE h)
{
  MidEndState me;
  lua_State *s;  /* the Lua compiler state to create */
  hksc_Context *ctx;  /* context shared with the frontend program */
  me.ctx_handle = h;
  me.luaVM = NULL;
  me.ctx = NULL;
  me.prevcwd = NULL;
  wait_for_target();
  init_symbols();  /* set pointers */
  ctx = get_context(h);
  if (ctx == NULL)
    die(&me);
  me.ctx = ctx;
  FreeConsole();  /* need to attach to the frontend console */
  if (!AttachConsole(ctx->frontendpid))
    die(&me);
  freopen("CONIN$", "r", stdin);
  freopen("CONOUT$", "w", stdout);
  freopen("CONOUT$", "w", stderr);
  if (parse_ctx(&me) != 0)
    fatal(&me, "internal error: bad context data");
  s = create_compiler_state(ctx);  /* create a Lua state */
  if (s == NULL)
    fatal(&me, "cannot create state: not enough memory");
  me.luaVM = s;
  cd2luadir(&me);  /* change directory */
  /* process all input files */
  ctx->status = forfiles(&me, ctx->expect_error ? expecterror : compilefile);
  die(&me);
}

#ifdef __cplusplus
}
#endif
