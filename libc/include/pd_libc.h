/*
 * PortaDoom freestanding libc.
 *
 * The engine is compiled against these headers only (no system headers),
 * so it needs nothing from the OS. Every symbol is renamed to pd_* with a
 * macro, so the engine can be linked into a host program that has its own
 * libc without clashing.
 *
 * Files go through a virtual filesystem (pd_vfs_*): WADs are served straight
 * from host memory, saves and config are kept in memory and handed to the
 * host through callbacks.
 */
#ifndef PD_LIBC_H
#define PD_LIBC_H

/* ---- stddef / stdint / stdarg / stdbool / limits ---------------------- */

typedef __SIZE_TYPE__    size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef long             ssize_t;
#ifndef __cplusplus
typedef __WCHAR_TYPE__   wchar_t;
#endif

#define NULL ((void *)0)
#define offsetof(t, m) __builtin_offsetof(t, m)

typedef __INT8_TYPE__    int8_t;
typedef __INT16_TYPE__   int16_t;
typedef __INT32_TYPE__   int32_t;
typedef __INT64_TYPE__   int64_t;
typedef __UINT8_TYPE__   uint8_t;
typedef __UINT16_TYPE__  uint16_t;
typedef __UINT32_TYPE__  uint32_t;
typedef __UINT64_TYPE__  uint64_t;
typedef __INTPTR_TYPE__  intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTMAX_TYPE__  intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295U
#define INT64_MAX  __INT64_MAX__
#define INT64_MIN  (-__INT64_MAX__ - 1)
#define UINT64_MAX __UINT64_MAX__
#define SIZE_MAX   __SIZE_MAX__

#define CHAR_BIT   8
#define SCHAR_MIN  (-128)
#define SCHAR_MAX  127
#define UCHAR_MAX  255
#define CHAR_MIN   SCHAR_MIN
#define CHAR_MAX   SCHAR_MAX
#define SHRT_MIN   (-32768)
#define SHRT_MAX   32767
#define USHRT_MAX  65535
#define INT_MIN    (-__INT_MAX__ - 1)
#define INT_MAX    __INT_MAX__
#define UINT_MAX   (__INT_MAX__ * 2U + 1U)
#define LONG_MIN   (-__LONG_MAX__ - 1L)
#define LONG_MAX   __LONG_MAX__
#define ULONG_MAX  (__LONG_MAX__ * 2UL + 1UL)
#define PATH_MAX   1024

#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_copy(d, s)      __builtin_va_copy(d, s)

#ifndef __cplusplus
#define bool  _Bool
#define true  1
#define false 0
#define __bool_true_false_are_defined 1
#endif

/* ---- assert / errno ---------------------------------------------------- */

void pd_assert_fail(const char *expr, const char *file, int line);
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : pd_assert_fail(#e, __FILE__, __LINE__))
#endif

extern int pd_errno;
#define errno  pd_errno
#define ENOENT 2
#define EEXIST 17
#define EISDIR 21

/* ---- string / strings ------------------------------------------------- */

#define memcpy      pd_memcpy
#define memmove     pd_memmove
#define memset      pd_memset
#define memcmp      pd_memcmp
#define memchr      pd_memchr
#define strlen      pd_strlen
#define strcpy      pd_strcpy
#define strncpy     pd_strncpy
#define strcat      pd_strcat
#define strncat     pd_strncat
#define strcmp      pd_strcmp
#define strncmp     pd_strncmp
#define strcasecmp  pd_strcasecmp
#define strncasecmp pd_strncasecmp
#define strchr      pd_strchr
#define strrchr     pd_strrchr
#define strstr      pd_strstr
#define strdup      pd_strdup
#define strerror    pd_strerror

void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strcat(char *d, const char *s);
char  *strncat(char *d, const char *s, size_t n);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcasecmp(const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);
char  *strdup(const char *s);
char  *strerror(int e);

/* ---- ctype ------------------------------------------------------------ */

#define isspace  pd_isspace
#define isdigit  pd_isdigit
#define isxdigit pd_isxdigit
#define isalpha  pd_isalpha
#define isalnum  pd_isalnum
#define isprint  pd_isprint
#define isupper  pd_isupper
#define islower  pd_islower
#define toupper  pd_toupper
#define tolower  pd_tolower

int isspace(int c);
int isdigit(int c);
int isxdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isprint(int c);
int isupper(int c);
int islower(int c);
int toupper(int c);
int tolower(int c);

/* ---- stdlib ----------------------------------------------------------- */

#define malloc  pd_malloc
#define calloc  pd_calloc
#define realloc pd_realloc
#define free    pd_free
#define atoi    pd_atoi
#define atof    pd_atof
#define strtol  pd_strtol
#define strtoul pd_strtoul
#define abs     pd_abs
#define labs    pd_labs
#define qsort   pd_qsort
#define exit    pd_exit
#define abort   pd_abort
#define getenv  pd_getenv
#define system  pd_system

void         *malloc(size_t n);
void         *calloc(size_t n, size_t sz);
void         *realloc(void *p, size_t n);
void          free(void *p);
int           atoi(const char *s);
double        atof(const char *s);
long          strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int           abs(int x);
long          labs(long x);
void          qsort(void *base, size_t n, size_t sz,
                    int (*cmp)(const void *, const void *));
void          exit(int code) __attribute__((noreturn));
void          abort(void) __attribute__((noreturn));
char         *getenv(const char *name);
int           system(const char *cmd);

/* ---- math ------------------------------------------------------------- */

#define sin   pd_sin
#define cos   pd_cos
#define tan   pd_tan
#define atan  pd_atan
#define sqrt  pd_sqrt
#define fabs  pd_fabs
#define floor pd_floor
#define ceil  pd_ceil
#define round pd_round
#define pow   pd_pow

double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double sqrt(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double pow(double x, double y);

/* ---- stdio (virtual filesystem) --------------------------------------- */

typedef struct pd_file FILE;

#define EOF      (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define stdin     pd_stdin
#define stdout    pd_stdout
#define stderr    pd_stderr
#define fopen     pd_fopen
#define fclose    pd_fclose
#define fread     pd_fread
#define fwrite    pd_fwrite
#define fseek     pd_fseek
#define ftell     pd_ftell
#define feof      pd_feof
#define ferror    pd_ferror
#define fflush    pd_fflush
#define fgetc     pd_fgetc
#define getc      pd_fgetc
#define fgets     pd_fgets
#define fputc     pd_fputc
#define putc      pd_fputc
#define fputs     pd_fputs
#define puts      pd_puts
#define putchar   pd_putchar
#define fscanf    pd_fscanf
#define remove    pd_remove
#define rename    pd_rename
#define printf    pd_printf
#define fprintf   pd_fprintf
#define vprintf   pd_vprintf
#define vfprintf  pd_vfprintf
#define sprintf   pd_sprintf
#define snprintf  pd_snprintf
#define vsnprintf pd_vsnprintf
#define sscanf    pd_sscanf
#define vsscanf   pd_vsscanf
#define setbuf    pd_setbuf

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE  *fopen(const char *name, const char *mode);
int    fclose(FILE *f);
size_t fread(void *buf, size_t sz, size_t n, FILE *f);
size_t fwrite(const void *buf, size_t sz, size_t n, FILE *f);
int    fseek(FILE *f, long off, int whence);
long   ftell(FILE *f);
int    feof(FILE *f);
int    ferror(FILE *f);
int    fflush(FILE *f);
int    fgetc(FILE *f);
char  *fgets(char *s, int n, FILE *f);
int    fputc(int c, FILE *f);
int    fputs(const char *s, FILE *f);
int    puts(const char *s);
int    putchar(int c);
int    fscanf(FILE *f, const char *fmt, ...);
int    remove(const char *name);
int    rename(const char *from, const char *to);
int    printf(const char *fmt, ...);
int    fprintf(FILE *f, const char *fmt, ...);
int    vprintf(const char *fmt, va_list ap);
int    vfprintf(FILE *f, const char *fmt, va_list ap);
int    sprintf(char *s, const char *fmt, ...);
int    snprintf(char *s, size_t n, const char *fmt, ...);
int    vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int    sscanf(const char *s, const char *fmt, ...);
int    vsscanf(const char *s, const char *fmt, va_list ap);
void   setbuf(FILE *f, char *buf);

/* ---- PortaDoom runtime hooks (set up by src/pd_api.c) ------------------ */

/* Give the allocator its memory. Must be called before anything else. */
void pd_libc_heap(void *mem, size_t size);

/* Bytes of heap currently in use / total. */
size_t pd_libc_heap_used(void);
size_t pd_libc_heap_size(void);

/* Register a read-only file served from memory (no copy is made). */
int pd_vfs_add(const char *name, const void *data, size_t len);

/* Look up a file's memory without opening it. Returns 0 if found. */
int pd_vfs_get(const char *name, const void **data, size_t *len);

/* Host callbacks used by the libc. Any may be NULL. */
typedef struct
{
    void *user;
    void (*log)(void *user, const char *text);
    void (*fatal)(void *user, const char *text);
    int  (*file_read)(void *user, const char *name, void **data, size_t *len);
    int  (*file_write)(void *user, const char *name, const void *data, size_t len);
    int  (*file_remove)(void *user, const char *name);
} pd_libc_hooks_t;

void pd_libc_set_hooks(const pd_libc_hooks_t *hooks);

/* Report a fatal error to the host. Never returns. */
void pd_fatal(const char *text) __attribute__((noreturn));

#endif /* PD_LIBC_H */
