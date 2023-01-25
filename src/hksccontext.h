/*
** hksccontext.h
** Defines the structure of shared data between the injector and the victim
*/

#ifndef hksccontext_h
#define hksccontext_h

#define CTX_TAG_PROGRAM_NAME   0  /* the frontend program name */
#define CTX_TAG_CWD           1  /* the frontend process cwd */
#define CTX_TAG_FILEPREFIXMAP 2  /* file prefix map strings */
#define CTX_TAG_OUTNAME_C     3  /* the main output file name */
#define CTX_TAG_OUTNAME_P     4  /* profile info file name if needed */
#define CTX_TAG_OUTNAME_D     5  /* debug info file name if needed  */
#define CTX_TAG_INFILE        6  /* an input file name */
#define CTX_TAG_DLL_NAME      7  /* the name of the injected DLL file */
#define CTX_TAG_EOS           8

typedef unsigned char ctxtag_t;

static const char *const tag_names[CTX_TAG_EOS+1] =
{
  "PROGRAM_NAME",
  "CWD",
  "FILEPREFIXMAP",
  "OUTNAME_C",
  "OUTNAME_P",
  "OUTNAME_D",
  "INFILE",
  "DLL_NAME",
  "EOS"
};

#define gettagname(t) ((unsigned)(t) <= CTX_TAG_EOS ? tag_names[t] : "INVALID")

typedef struct {
  DWORD sizedata;
  int expect_error;
  int strip;
  int status;
  int enable_int_literals;
  DWORD frontendpid;  /* process id of the injector program */
} hksc_Context;

struct ctx_data_table {
  const char *progname;
  const char *cwd;
  const char *outname_c;
  const char *outname_p;
  const char *outname_d;
  const char *prefix_maps;
  const char *infiles;
  const char *dll_name;
};

#define aligned2type(x,t)  (((uintptr_t)(x) + (sizeof(t)-1)) & ~(sizeof(t)-1))
#define aligned2dword(x)  aligned2type(x,DWORD)

#define getdata(ctx)  ((const char *)((ctx) + 1))

#endif /* hksccontext_h */
