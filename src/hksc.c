/*
** hksc.c
** Frontend to the Call of Duty Lua compiler interface
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h> /* CreateToolhelp32Snapshot, etc. */
#include <psapi.h> /* EnumProcessModules, GetModuleFileName */
#include <intsafe.h> /* SIZETToDWord */

#define hksc_c

#ifndef _WIN64
#error This program is meant for 64-bit Windows
#endif

#include "hksccontext.h"
#include "hks.h"

#define PROGRAM_NAME "cod-hksc"
#define PROGRAM_VERSION "0.0.0"

static int strip=0;
static int literals_enabled=INT_LITERALS_NONE; /* int literal options */

#define MAX_PREFIX_MAPS 16

static int n_prefix_maps=0;
static const char *prefix_maps[MAX_PREFIX_MAPS]={NULL};
static int n_infiles=0;
static const char **infiles=NULL;
static size_t sizedata=sizeof(hksc_Context);
static int n_tags=0;
static char *progname=PROGRAM_NAME;
static int expect_error=0;
static HANDLE target;  /* handle to Black Ops 3 process */
static DWORD pid=0;  /* PID of Black Ops 3 process */
static const char *dllpath=NULL;  /* file path of the DLL to inject */
static const char *outputname=NULL;
static const char *debugname=NULL;
static const char *callstackdbname=NULL;

static const char *targetgame="t7";
static const char *targetprocess=NULL;
static int gameid=-1;

#define GAME_T6 0
#define GAME_T7 1
#define GAME_SEKIRO 2
#define NUM_GAMES 3

static size_t data_dir_offsets[NUM_GAMES] =
{
  [GAME_T6] = 120,
  [GAME_T7] = 136,
  [GAME_SEKIRO] = 136
};

static void preallocsize(size_t size)
{
  n_tags++;
  sizedata += (DWORD)size + aligned2dword(sizeof(ctxtag_t)) + sizeof(DWORD);
}


static void preallocstring(const char *data)
{
  preallocsize(data ? strlen(data)+1 : 1);
}


static void puttag(ctxtag_t **t, ctxtag_t tag)
{
  **t = tag;
  (*t)++;
}


static void puttaggedsize(char **p, size_t size)
{
  char *s = *p;
  s = (char *)aligned2dword(s);
  *(DWORD *)s = (DWORD)size;
  s+= sizeof(DWORD);
  *p = s;
}


static void puttaggedstring(char **pos, ctxtag_t tag, const char *str)
{
  char *data;
  puttag(pos, tag);
  data = *pos;
  if (str == NULL)
    puttaggedsize(&data, 1);
  if (str != NULL) {
    size_t l = strlen(str);
    puttaggedsize(&data, l+1);
    strncpy(data, str, l);
    data += l;
  }
  *data++ = '\0';
  *pos = data;
}


static void fatal(const char *fmt, ...)
{
  va_list argp;
  fprintf(stderr, "%s: ", progname);
  va_start(argp, fmt);
  vfprintf(stderr, fmt, argp);
  va_end(argp);
  fputc('\n', stderr);
  exit(EXIT_FAILURE);
}


#define error_multiple_inputs(opt) \
  usage("'" opt "' used with multiple input files")


/* call this when a windows API call fails */
static void fatalsys(const char *fmt, ...)
{
  va_list argp;
  DWORD error = GetLastError();
  LPSTR msg = NULL;
  fprintf(stderr, "%s: ", progname);
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
  exit(EXIT_FAILURE);
}


static void print_help(void)
{
  fprintf(stderr, "usage: %s [options]\n", progname);
  fputs(
  "\nOptions:\n"
  "      --help              Print this message and exit\n"
  "      --version           Show version information\n"
  "  -e, --expect-error      Expect errors and write them to the output file\n"
  "      --game=GAME         Compile Lua for GAME\n"
  "                          Supported values:\n"
  "                            t6\n"
  "                            t7\n"
  "                            sekiro\n"
  "                          The default value is 't7'\n"
  "  -L[=TYPE]               Enable int literals of the given TYPE\n"
  "  -o, --output=NAME       Output to file NAME\n"
  "      --callstackdb=FILE  Use FILE for callstack reconstruction\n"
  "      --debugfile=FILE    Use FILE for debug info\n"
  "  -s                      Do not generate debug files\n"
  "      --file-prefix-map=<OLD=NEW>\n"
  "                          Replace OLD with NEW when referencing file names "
  "in\n"
  "                          debug information and error messages\n\n"
  , stderr);
  fputs(
  "Int literal options for TYPE (to use with '-L')\n"
  "  32  Enable 32-bit int literals\n"
  "  64  Enable 64-bit int literals\n"
  "  Not providing a value for TYPE will enable all literal types\n\n",
   stderr);
  fputs(
  "About game title options (to use with '--game')\n"
  "  The frontend will look for a given process depending on this option\n"
  "    t6: will look for 't6sp.exe'\n"
  "    t7: will look for 'blackops3.exe'\n"
  "    sekiro: will look for 'sekiro.exe'\n\n", stderr);
  fputs(
  "If '-e' is used, the program succeeds and exits with code 0 only if all\n"
  "source files contain syntax errors\n"
  , stderr);
}


static void print_version(void)
{
  fputs(PROGRAM_NAME " version " PROGRAM_VERSION "\n", stderr);
}


static void usage(const char *msg)
{
  if (*msg == '-')
    fprintf(stderr,"%s: unrecognized option '%s'\n",progname,msg);
  else
    fprintf(stderr,"%s: %s\n",progname,msg);
  print_help();
  exit(EXIT_FAILURE);
}


#define IS(s) (strcmp(argv[i],s)==0)
#define HAS(s) (strncmp(argv[i],"" s,sizeof(s)-1)==0)


#define GETARGVALUE(var, str) do { \
  const char *s = argv[i] + sizeof("" str)-1; \
  if (*s == '\0') { \
    if (i+1 < argc) var = argv[++i]; \
    else goto badarg; \
  } \
  else if (*s != '=') goto badarg; \
  else var = s+1; \
} while (0)


static int doargs(int argc, const char *argv[])
{
  int i=0;
  int version=0;
  int nfiles=0;
  if (argv[0] != NULL && *argv[0] != 0)
    progname = (char *)argv[0];
  for (i=1; i<argc; i++)
  {
    if (argv[i][0]!='-')      /* input file */
    {
      const char *tmp = argv[++nfiles]; /* push names to the front */
      argv[nfiles] = argv[i];
      argv[i] = tmp;
      preallocstring(argv[nfiles]);
    }
    else if (IS("--"))      /* end of options; skip it */
    {
      ++i;
      if (version) ++version;
      break;
    }
    else if (IS("--help")) {
      print_help();
      exit(EXIT_SUCCESS);
    }
    else if (IS("--version")) {
      print_version();
      exit(EXIT_SUCCESS);
    }
    else if (IS("-e") || IS("--expect-error"))
      expect_error=1;
    else if (HAS("-L"))   /* specify int literal options */
    {
      const char *mode;
      if (argv[i][2] == 0) {
        literals_enabled=INT_LITERALS_ALL; /* default */
        continue;
      }
      mode = argv[i]+2;
      if (*mode == '=') mode++;
      if (mode[0] == '\0' || mode[1] == '\0' || mode[2] != '\0')
        goto badliteralarg;
      switch (*mode) {
        case '3':
          if (mode[1] != '2') goto badliteralarg;
          literals_enabled|=INT_LITERALS_LUD; break;
        case '6':
          if (mode[1] != '4') goto badliteralarg;
          literals_enabled|=INT_LITERALS_UI64; break;
        default:
          goto badliteralarg;
      }
      continue;
      badliteralarg:
      usage("invalid int literal type given with '-L'");
    }
    else if (HAS("-o"))
      GETARGVALUE(outputname, "-o");
    else if (HAS("--output"))
      GETARGVALUE(outputname, "--output");
    else if (HAS("--debugfile"))
      GETARGVALUE(debugname, "--debugfile");
    else if (HAS("--callstackdb"))
      GETARGVALUE(callstackdbname, "--callstackdb");
    else if (HAS("--file-prefix-map")) {
      const char *prefix_map;
      if (n_prefix_maps >= MAX_PREFIX_MAPS) {
        fatal("too many file prefix maps specified (limited to %d)",
              MAX_PREFIX_MAPS);
      }
      GETARGVALUE(prefix_map, "--file-prefix-map");
      prefix_maps[n_prefix_maps++] = prefix_map;
      if (strchr(prefix_map, '=') == NULL)
        fatal("invalid value for '--file-prefix-map' '%s'", prefix_map);
    }
    else if (HAS("--game"))
      GETARGVALUE(targetgame, "--game");
    else if (IS("-s"))
      strip=1;
    else {
      badarg:
      usage(argv[i]);
    }
  }
  return nfiles;
}


static char *xgetfulldllpath(const char *name)
{
  char exename[MAX_PATH];
  size_t exename_len;
  size_t name_len;
  char *lastsep = NULL;
  char *fulldllpath;
  unsigned int i;
  {
    DWORD result;
    result = GetModuleFileNameA(NULL, exename, sizeof(exename));
    if (result == 0)
      fatalsys("cannot get executable filename");
    else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
      fatal("cannot get executable filename: path is too long");
  }
  exename_len = strlen(exename);
  name_len = strlen(name);
  fulldllpath = malloc(exename_len + name_len + 1);
  if (!fulldllpath)
    fatal("cannot allocate memory for DLL path: %s", strerror(errno));
  for (i = 0; i < exename_len; i++) {
    if (exename[i] == '/')
      fulldllpath[i] = '\\';
    else
      fulldllpath[i] = exename[i];
    if (fulldllpath[i] == '\\')
      lastsep = &fulldllpath[i];
  }
  if (lastsep == NULL)
    fatal("invalid executable path: '%s'", exename);
  lastsep++;
  for (i = 0; i < name_len; i++) {
    if (name[i] == '/')
      lastsep[i] = '\\';
    else
      lastsep[i] = name[i];
  }
  lastsep[i] = '\0';
  return fulldllpath;
}


static int invokebackend(void);

#define STREQ(a,b) (_stricmp(a,b) == 0)
#define STREQN(a,b) (strncmp(a,b,strlen(a)) == 0)

int main(int argc, const char *argv[])
{
  int status;
  int nfiles=doargs(argc, argv); /* parse args */
  const char *dllname;
  if (nfiles == 0)
    usage("no input files given");
  else if (nfiles > 1) {
    if (outputname != NULL)
      error_multiple_inputs("--output");
    if (debugname != NULL)
      error_multiple_inputs("--debugfile");
    if (callstackdbname != NULL)
      error_multiple_inputs("--callstackdb");
  }
  if (STREQ(targetgame, "t6")) {
    dllname = "compiler_t6.dll";
    targetprocess = "t6sp.exe";
    gameid = GAME_T6;
  }
  else if (STREQ(targetgame, "t7")) {
    dllname = "compiler_t7.dll";
    targetprocess = "blackops3.exe";
    gameid = GAME_T7;
  }
  else if (STREQN(targetgame, "sekiro")) {
    dllname = "compiler_sekiro.dll";
    targetprocess = "sekiro.exe";
    gameid = GAME_SEKIRO;
  }
  else {
    usage("invalid value for '--game'");
    return EXIT_FAILURE;
  }
  dllpath = xgetfulldllpath(dllname);
  argv++;
  n_infiles = nfiles;
  infiles = argv;
  status = invokebackend();
  return status;
}


FARPROC xgetremoteprocaddr(HANDLE p, const char *module, const char *name,
                           int fullpath);


/* create a remote thread in the target process */
static HANDLE xinjectthread(FARPROC f, LPVOID ud) {
  DWORD tid;
  HANDLE t;
  t = CreateRemoteThread(target, NULL, 0, (LPTHREAD_START_ROUTINE)f, ud,
                                0, &tid);
  if (t == NULL)
    fatalsys("failed to create remote thread in (%lu)", pid);
  return t;
}


/* run a remote thread in the target process with a given start procedure */
static DWORD xremotecall(FARPROC f, LPVOID ud) {
  DWORD status;
  HANDLE t = xinjectthread(f, ud);
  WaitForSingleObject(t, INFINITE);
  if (!GetExitCodeThread(t, &status))
    fatalsys("failed to obtain the exit code of the remote thread");
  return status;
}


/* inject a DLL into the target process */
static void xinject(const char *name)
{
  size_t written;
  HANDLE t;
  DWORD status;
  LPVOID remotebuff;
  size_t remotebuffsize = strlen(name)+1;
  FARPROC loadlib;
  remotebuff = (LPVOID)VirtualAllocEx(target, NULL, remotebuffsize,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (remotebuff == NULL)
    fatalsys("cannot allocate memory in remote process (%lu)", pid);
  if (!WriteProcessMemory(target, remotebuff, name, remotebuffsize, &written))
    fatalsys("cannot write %zu bytes to remote process (%lu)", remotebuffsize,
             pid);
  if (written < remotebuffsize)
    fatal("cannot write %zu bytes to remote process (%lu) (wrote %zu/%zu)\n",
          remotebuffsize, pid, written, remotebuffsize);
  /*loadlib = GetProcAddress(GetModuleHandleA("kernel32.dll"),"LoadLibraryA");*/
  loadlib = xgetremoteprocaddr(target, "kernel32.dll", "LoadLibraryA", 0);
  t = CreateRemoteThread(target, NULL, 0, (LPTHREAD_START_ROUTINE)loadlib,
                         remotebuff, 0, NULL);
  if (t == NULL) {
    VirtualFreeEx(target, remotebuff, remotebuffsize, MEM_RELEASE);
    fatalsys("failed to create injection thread in (%lu)", pid);
  }
  WaitForSingleObject(t, INFINITE);
  VirtualFreeEx(target, remotebuff, remotebuffsize, MEM_RELEASE);
  if (!GetExitCodeThread(t, &status)) {
    fatalsys("failed to obtain exit code of the injection thread");
  }
  /* the thread exit status is the return value of LoadLibraryA */
  if (status == 0) {
    fatal("failed to inject DLL '%s'", name);
  }
}


static DWORD xgetpid(const char *name)
{
  DWORD pid = 0;
  PROCESSENTRY32 entry;
  HANDLE snap;
  snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE)
    fatalsys("cannot obtain a system snapshot of processes");
  entry.dwSize = sizeof(PROCESSENTRY32);
  if (!Process32First(snap, &entry))
    fatalsys("cannot retieve information about the first snapshot process");
  do { /* find NAME in the system snapshot */
    if (_stricmp(entry.szExeFile, name) == 0) {
      /*printf("found process '%s'\n", name);*/
      pid = entry.th32ProcessID;
      break;
    }
  } while (Process32Next(snap, &entry));
  CloseHandle(snap);
  if (pid == 0)
    fatal("cannot find process '%s'", name);
  return pid;
}


static hksc_Context *xmap_hksc_context(HANDLE *h)
{
  DWORD contextsize;
  DWORD cwdlen;
  int i;
  HANDLE ctx_handle;
  hksc_Context *ctx;
  char *data;
  cwdlen = GetCurrentDirectoryA(0, NULL);
  if (!cwdlen)
    fatalsys("cannot get current directory");
  preallocsize(cwdlen); /* allocate space for current working directory */
  preallocstring(progname);
  preallocstring(dllpath);
  preallocstring(outputname);
  preallocstring(debugname);
  preallocstring(callstackdbname);
  if (SIZETToDWord(sizedata, &contextsize) != S_OK)
    fatal("data too large to fit in DWORD");
  ctx_handle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                 contextsize, NULL);
  if (!ctx_handle)
    fatalsys("failed to create a shared file mapping (%zu) bytes", sizedata);
  ctx = (hksc_Context *)MapViewOfFile(ctx_handle, FILE_MAP_ALL_ACCESS, 0, 0,
                                      sizedata);
  if (!ctx)
    fatalsys("failed to map view of shared file");
  ctx->sizedata = contextsize;
  ctx->expect_error = expect_error;
  ctx->status = 0;
  ctx->enable_int_literals = literals_enabled;
  ctx->strip = strip;
  ctx->frontendpid = GetCurrentProcessId();
  data = (char *)(ctx + 1);
  puttaggedstring(&data, CTX_TAG_PROGRAM_NAME, progname);
  puttaggedstring(&data, CTX_TAG_DLL_NAME, dllpath);
  puttag(&data, CTX_TAG_CWD);
  puttaggedsize(&data, cwdlen);
  if (!GetCurrentDirectoryA(cwdlen, data))
    fatalsys("cannot get current directory");
  data += cwdlen;
  puttaggedstring(&data, CTX_TAG_OUTNAME_C, outputname);
  puttaggedstring(&data, CTX_TAG_OUTNAME_D, debugname);
  puttaggedstring(&data, CTX_TAG_OUTNAME_P, callstackdbname);
  for (i = 0; i < n_prefix_maps; i++)
    puttaggedstring(&data, CTX_TAG_FILEPREFIXMAP, prefix_maps[i]);
  for (i = 0; i < n_infiles; i++)
    puttaggedstring(&data, CTX_TAG_INFILE, infiles[i]);
  puttag(&data, CTX_TAG_EOS);
  if (h) *h = ctx_handle;
  return ctx;
}


static int invokebackend(void)
{
  int threadstatus, status;
  FARPROC func; /* the name of the DLL export to call */
  hksc_Context *ctx; /* context shared between the front and back end */
  HANDLE ctx_handle; /* frontend handle to shared context */
  HANDLE ctx_handle_dupe; /* backend handle to shared context */
  pid = xgetpid(targetprocess);
  ctx = xmap_hksc_context(&ctx_handle); /* allocate shared context */
  /* get a handle to the remote process */
  target = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                       PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_DUP_HANDLE |
                       PROCESS_VM_OPERATION, FALSE, pid);
  if (target == NULL)
    fatalsys("cannot open process (%lu)", pid);
  xinject(dllpath); /* inject the DLL which does the backend work */
  /* create a handle to the context for the backend */
  if (!DuplicateHandle(GetCurrentProcess(), ctx_handle, target,
                       &ctx_handle_dupe, 0, FALSE, DUPLICATE_SAME_ACCESS))
    fatalsys("failed to duplicate handle to shared memory");
  /* get remote address of the DLL export to call */
  func = xgetremoteprocaddr(target, dllpath, "middlelev", 1);
  /* start the backend worker to process all input files */
  threadstatus = xremotecall(func, (LPVOID)ctx_handle_dupe);
  status = ctx->status;
  UnmapViewOfFile(ctx);
  CloseHandle(ctx_handle);
  CloseHandle(target);
  if (threadstatus || status)
    return EXIT_FAILURE;
  else
    return EXIT_SUCCESS;
}


static void pxread(HANDLE p, const void *src, void *dst, size_t n,
                   const char *id) {
  size_t nread;
  BOOL result = ReadProcessMemory(p, src, dst, n, &nread);
  /*printf("read %zu bytes for '%s' from '%s'\n", nread, id, targetprocess);*/
  if (!result)
    fatalsys("cannot read %zu bytes from remote process (%lu)", n, pid);
  if (nread != n)
    fatal("cannot read %zu bytes from remote process (%lu), only read %zu", n,
          pid, nread);
}


#define pxreadvar(p, src, s) pxread(p, src, &(s), sizeof(s), #s)



FARPROC xgetremoteprocaddr(HANDLE p, const char *module, const char *name,
                           int fullpath) {
  HMODULE m; /* handle to MODULE */
  char *mbase;
  FARPROC procaddr = NULL; /* return value, the address of the function */
  int found = 0; /* whether the MODULE was found */
  IMAGE_DOS_HEADER dos_hdr = {0};
  IMAGE_NT_HEADERS nt_hdrs = {0};
  IMAGE_EXPORT_DIRECTORY exp_dir = {0};
  IMAGE_DATA_DIRECTORY *data_dir;
  { /* get a handle to MODULE  */
    HMODULE modules[1024];
    DWORD needed;
    unsigned int i;
    if (!EnumProcessModulesEx(p, modules, sizeof(modules), &needed,
                              LIST_MODULES_ALL))
      fatalsys("cannot enumerate process modules for '%s'", targetprocess);
    else if (needed > sizeof(modules))
      fatal("'%s' has too many modules loaded (%lu)", targetprocess, needed);
    for (i = 0; i < needed/sizeof(HMODULE); i++) {
      char buff[MAX_PATH];
      DWORD result;
      if (fullpath)
        result = GetModuleFileNameExA(p, modules[i], buff, sizeof(buff));
      else
        result = GetModuleBaseNameA(p, modules[i], buff, sizeof(buff));
      if (!result)
        fatalsys("cannot get module name");
      if (_stricmp(buff, module) == 0) {
        found = 1;
        /*printf("found module '%s'\n", module);*/
        m = modules[i];
        break;
      }
    }
  }
  if (!found) { /* module not found */
    fatal("module '%s' not found in (%lu)", module, pid);
    return NULL;
  }
  mbase = (char *)m;
  pxreadvar(p, mbase, dos_hdr);  /* read DOS header */
  if (dos_hdr.e_magic != IMAGE_DOS_SIGNATURE) {
    fatal("bad DOS header in (%lu)", pid);
    return NULL;
  }
  pxreadvar(p, mbase + dos_hdr.e_lfanew, nt_hdrs);  /* read PE header */
  if (nt_hdrs.Signature != IMAGE_NT_SIGNATURE) {
    fatal("bad NT headers in (%lu)", pid);
    return NULL;
  }
  /* read exports directory */
  data_dir = (IMAGE_DATA_DIRECTORY *)((char *)(&nt_hdrs) +
                                      data_dir_offsets[gameid]);
/*  pxreadvar(p, mbase + nt_hdrs.OptionalHeader.DataDirectory
               [IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress, exp_dir);*/
  pxreadvar(p, mbase + data_dir[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress,
            exp_dir);
  { /* search for NAME in the exports directory */
    DWORD *addrtab = (DWORD *)(mbase + exp_dir.AddressOfFunctions);
    DWORD *nametab = (DWORD *)(mbase + exp_dir.AddressOfNames);
    WORD *ordtab = (WORD *)(mbase + exp_dir.AddressOfNameOrdinals);
    unsigned int i;
    size_t namelen = strlen(name);
    size_t buffsize = namelen+2;
    char *buff = malloc(buffsize);
    if (buff == NULL)
      fatal("out of memory (failed to allocate %zu bytes)", buffsize);
    for (i = 0; i < exp_dir.NumberOfNames; i++) {
      DWORD nameoffs; /* offset of this export name */
      WORD ord; /* ordinal number of this export  */
      pxreadvar(p, &nametab[i], nameoffs); /* read name offset */
      pxread(p, mbase + nameoffs, buff, buffsize, "buff"); /* read name into BUFF */
      buff[buffsize-1] = '\0';
      if (strcmp(name, buff) == 0) {
        DWORD procoffs; /* RVA of function */
        /*printf("found export '%s'\n", name);*/
        pxreadvar(p, &ordtab[i], &ord); /* read ordinal */
        pxreadvar(p, &addrtab[ord], procoffs); /* read RVA */
        procaddr = (FARPROC)(mbase + procoffs);
        break;
      }
    }
    free(buff);
  }
  if (procaddr == NULL)
    fatal("procedure '%s' not found in module '%s' in (%lu)", name, pid);
  return procaddr;
}
