/*
 * PortaDoom freestanding libc implementation.
 * See include/pd_libc.h for the overview.
 */
#include "pd_libc.h"

static pd_libc_hooks_t hooks;

void pd_libc_set_hooks(const pd_libc_hooks_t *h)
{
    if (h)
        hooks = *h;
    else
        memset(&hooks, 0, sizeof(hooks));
}

int pd_errno;

/* ======================================================================
 * Logging and fatal errors
 * ==================================================================== */

static char logline[1024];
static size_t loglen;

static void log_flush(void)
{
    if (loglen == 0)
        return;
    logline[loglen] = '\0';
    if (hooks.log)
        hooks.log(hooks.user, logline);
    loglen = 0;
}

static void log_putc(char c)
{
    if (c == '\n')
    {
        log_flush();
        return;
    }
    if (loglen >= sizeof(logline) - 1)
        log_flush();
    logline[loglen++] = c;
}

void pd_fatal(const char *text)
{
    log_flush();
    if (hooks.fatal)
        hooks.fatal(hooks.user, text);
    /* The host's fatal hook is expected not to return (longjmp, throw,
     * exit). If it does, or there is none, stop hard. */
    __builtin_trap();
}

void pd_assert_fail(const char *expr, const char *file, int line)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "assert failed: %s (%s:%d)", expr, file, line);
    pd_fatal(buf);
}

void exit(int code)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "engine called exit(%d)", code);
    pd_fatal(buf);
}

void abort(void)
{
    pd_fatal("engine called abort()");
}

char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

int system(const char *cmd)
{
    (void)cmd;
    return -1;
}

/* ======================================================================
 * Memory
 * ==================================================================== */

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    while (n--)
        *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp < sp)
    {
        while (n--)
            *dp++ = *sp++;
    }
    else if (dp > sp)
    {
        dp += n;
        sp += n;
        while (n--)
            *--dp = *--sp;
    }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *dp = d;
    while (n--)
        *dp++ = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *ap = a, *bp = b;
    for (; n; n--, ap++, bp++)
        if (*ap != *bp)
            return *ap - *bp;
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n; n--, p++)
        if (*p == (unsigned char)c)
            return (void *)p;
    return NULL;
}

/*
 * Heap: first-fit allocator over one block the host gives us. The free
 * list is kept in address order so neighbours can be merged on free.
 */

#define ALIGN      16
#define USED_MAGIC ((struct blk *)(uintptr_t)0x9D0011EDu)

struct blk
{
    size_t size;        /* whole block including this header */
    struct blk *next;   /* next free block, or USED_MAGIC */
    size_t pad[2];      /* keep header 16-byte aligned on 32-bit too */
};

#define HDR ((sizeof(struct blk) + ALIGN - 1) & ~(size_t)(ALIGN - 1))

static struct blk *freelist;
static size_t heap_size, heap_used;

void pd_libc_heap(void *mem, size_t size)
{
    uintptr_t start = ((uintptr_t)mem + ALIGN - 1) & ~(uintptr_t)(ALIGN - 1);
    size -= start - (uintptr_t)mem;
    size &= ~(size_t)(ALIGN - 1);
    freelist = (struct blk *)start;
    freelist->size = size;
    freelist->next = NULL;
    heap_size = size;
    heap_used = 0;
}

size_t pd_libc_heap_used(void) { return heap_used; }
size_t pd_libc_heap_size(void) { return heap_size; }

void *malloc(size_t n)
{
    struct blk **pp, *b;
    size_t need;

    if (n == 0)
        n = 1;
    need = (n + HDR + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    for (pp = &freelist; (b = *pp) != NULL; pp = &b->next)
    {
        if (b->size < need)
            continue;
        if (b->size - need >= HDR + ALIGN)
        {
            struct blk *rest = (struct blk *)((char *)b + need);
            rest->size = b->size - need;
            rest->next = b->next;
            *pp = rest;
            b->size = need;
        }
        else
        {
            *pp = b->next;
        }
        b->next = USED_MAGIC;
        heap_used += b->size;
        return (char *)b + HDR;
    }

    pd_errno = 12; /* ENOMEM */
    return NULL;
}

void free(void *p)
{
    struct blk *b, *prev, *cur;

    if (!p)
        return;
    b = (struct blk *)((char *)p - HDR);
    if (b->next != USED_MAGIC)
        pd_fatal("free(): bad pointer or double free");
    heap_used -= b->size;

    prev = NULL;
    for (cur = freelist; cur && cur < b; cur = cur->next)
        prev = cur;

    b->next = cur;
    if (prev)
        prev->next = b;
    else
        freelist = b;

    /* merge with the following block */
    if (cur && (char *)b + b->size == (char *)cur)
    {
        b->size += cur->size;
        b->next = cur->next;
    }
    /* merge with the preceding block */
    if (prev && (char *)prev + prev->size == (char *)b)
    {
        prev->size += b->size;
        prev->next = b->next;
    }
}

void *calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void *p;
    if (sz && total / sz != n)
        return NULL;
    p = malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void *realloc(void *p, size_t n)
{
    struct blk *b;
    size_t have;
    void *np;

    if (!p)
        return malloc(n);
    if (n == 0)
    {
        free(p);
        return NULL;
    }
    b = (struct blk *)((char *)p - HDR);
    have = b->size - HDR;
    if (have >= n)
        return p;
    np = malloc(n);
    if (!np)
        return NULL;
    memcpy(np, p, have);
    free(p);
    return np;
}

/* ======================================================================
 * Strings and ctype
 * ==================================================================== */

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    for (; n && *s; n--)
        *d++ = *s++;
    for (; n; n--)
        *d++ = '\0';
    return r;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *e = d + strlen(d);
    for (; n && *s; n--)
        *e++ = *s++;
    *e = '\0';
    return d;
}

int strcmp(const char *a, const char *b)
{
    for (; *a && *a == *b; a++, b++)
        ;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++)
    {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            return 0;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    int ca, cb;
    do
    {
        ca = tolower((unsigned char)*a++);
        cb = tolower((unsigned char)*b++);
    } while (ca && ca == cb);
    return ca - cb;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    int ca, cb;
    for (; n; n--)
    {
        ca = tolower((unsigned char)*a++);
        cb = tolower((unsigned char)*b++);
        if (ca != cb)
            return ca - cb;
        if (!ca)
            return 0;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++)
    {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++)
    {
        if (*s == (char)c)
            r = s;
        if (!*s)
            return (char *)r;
    }
}

char *strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl)
        return (char *)h;
    for (; *h; h++)
        if (*h == *n && !strncmp(h, n, nl))
            return (char *)h;
    return NULL;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

char *strerror(int e)
{
    switch (e)
    {
        case ENOENT: return "No such file";
        case EEXIST: return "File exists";
        default:     return "Error";
    }
}

int isspace(int c)  { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isdigit(int c)  { return c >= '0' && c <= '9'; }
int isxdigit(int c) { return isdigit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f'); }
int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
int islower(int c)  { return c >= 'a' && c <= 'z'; }
int isalpha(int c)  { return isupper(c) || islower(c); }
int isalnum(int c)  { return isalpha(c) || isdigit(c); }
int isprint(int c)  { return c >= 0x20 && c < 0x7f; }
int toupper(int c)  { return islower(c) ? c - 32 : c; }
int tolower(int c)  { return isupper(c) ? c + 32 : c; }

int abs(int x)    { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

static int digitval(int c)
{
    if (isdigit(c)) return c - '0';
    if (islower(c)) return c - 'a' + 10;
    if (isupper(c)) return c - 'A' + 10;
    return 99;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    const char *p = s;
    unsigned long v = 0;
    int neg = 0, any = 0, d;

    while (isspace((unsigned char)*p))
        p++;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] | 32) == 'x'
        && isxdigit((unsigned char)p[2]))
    {
        p += 2;
        base = 16;
    }
    else if (base == 0)
    {
        base = *p == '0' ? 8 : 10;
    }
    while ((d = digitval((unsigned char)*p)) < base)
    {
        v = v * base + d;
        p++;
        any = 1;
    }
    if (end)
        *end = (char *)(any ? p : s);
    return neg ? -v : v;
}

long strtol(const char *s, char **end, int base)
{
    return (long)strtoul(s, end, base);
}

int atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

double atof(const char *s)
{
    double v = 0, scale = 1;
    int neg = 0;

    while (isspace((unsigned char)*s))
        s++;
    if (*s == '+' || *s == '-')
        neg = *s++ == '-';
    while (isdigit((unsigned char)*s))
        v = v * 10 + (*s++ - '0');
    if (*s == '.')
    {
        s++;
        while (isdigit((unsigned char)*s))
        {
            scale /= 10;
            v += (*s++ - '0') * scale;
        }
    }
    if ((*s | 32) == 'e')
    {
        int e = atoi(s + 1);
        while (e > 0) { v *= 10; e--; }
        while (e < 0) { v /= 10; e++; }
    }
    return neg ? -v : v;
}

void qsort(void *base, size_t n, size_t sz,
           int (*cmp)(const void *, const void *))
{
    /* insertion sort: tiny inputs only in this engine */
    char *b = base, tmp[256];
    size_t i, j;

    if (sz > sizeof(tmp))
        pd_fatal("qsort: element too large");
    for (i = 1; i < n; i++)
    {
        memcpy(tmp, b + i * sz, sz);
        for (j = i; j > 0 && cmp(b + (j - 1) * sz, tmp) > 0; j--)
            memcpy(b + j * sz, b + (j - 1) * sz, sz);
        memcpy(b + j * sz, tmp, sz);
    }
}

/* ======================================================================
 * Math. Only used for one-off table setup and config values, never in the
 * per-tic game simulation (which is all fixed point), so plain series are
 * accurate enough and keep results identical on every platform.
 * ==================================================================== */

#define PI 3.14159265358979323846

double fabs(double x)  { return x < 0 ? -x : x; }

double floor(double x)
{
    double t;
    if (x >= 9007199254740992.0 || x <= -9007199254740992.0)
        return x;
    t = (double)(long long)x;
    return t > x ? t - 1 : t;
}

double ceil(double x)  { double f = floor(x); return f < x ? f + 1 : f; }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double sqrt(double x)
{
    double r;
    int i;
    if (x <= 0)
        return 0;
    r = x > 1 ? x : 1;
    for (i = 0; i < 64; i++)
        r = 0.5 * (r + x / r);
    return r;
}

double sin(double x)
{
    double term, sum;
    int i;

    x -= 2 * PI * floor(x / (2 * PI));   /* 0 .. 2pi */
    if (x > PI)
        return -sin(x - PI);
    if (x > PI / 2)
        x = PI - x;
    term = sum = x;
    for (i = 1; i < 12; i++)
    {
        term *= -x * x / ((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

double cos(double x) { return sin(x + PI / 2); }

double tan(double x)
{
    double c = cos(x);
    return c == 0 ? 1e300 : sin(x) / c;
}

double atan(double x)
{
    double sum, term, x2;
    int i, neg = x < 0, inv = 0, half = 0;

    if (neg)
        x = -x;
    if (x > 1)
    {
        x = 1 / x;
        inv = 1;
    }
    /* reduce further with atan(x) = 2 atan(x / (1 + sqrt(1 + x^2))) */
    if (x > 0.3)
    {
        x = x / (1 + sqrt(1 + x * x));
        half = 1;
    }
    x2 = x * x;
    term = sum = x;
    for (i = 1; i < 30; i++)
    {
        term *= -x2;
        sum += term / (2 * i + 1);
    }
    if (half)
        sum *= 2;
    if (inv)
        sum = PI / 2 - sum;
    return neg ? -sum : sum;
}

double pow(double x, double y)
{
    /* integer exponents only (all the engine needs) */
    long long n = (long long)y;
    double r = 1;
    int neg = n < 0;
    if (neg)
        n = -n;
    while (n)
    {
        if (n & 1)
            r *= x;
        x *= x;
        n >>= 1;
    }
    return neg ? 1 / r : r;
}

/* ======================================================================
 * printf family
 * ==================================================================== */

typedef struct
{
    char *buf;          /* string target, or NULL */
    size_t cap;
    size_t len;         /* characters produced (may exceed cap) */
    FILE *file;         /* file target, or NULL */
} sink_t;

static void sink_putc(sink_t *s, char c);

static void sink_puts(sink_t *s, const char *str, size_t n)
{
    while (n--)
        sink_putc(s, *str++);
}

static void sink_pad(sink_t *s, char c, int n)
{
    while (n-- > 0)
        sink_putc(s, c);
}

static int format(sink_t *out, const char *fmt, va_list ap)
{
    char tmp[80];

    for (; *fmt; fmt++)
    {
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0;
        int width = 0, prec = -1, lng = 0, len, neg;
        unsigned long long uv;
        const char *str;
        char *p;

        if (*fmt != '%')
        {
            sink_putc(out, *fmt);
            continue;
        }
        fmt++;

        for (;; fmt++)
        {
            if (*fmt == '-') left = 1;
            else if (*fmt == '+') plus = 1;
            else if (*fmt == ' ') space = 1;
            else if (*fmt == '#') alt = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        if (*fmt == '*')
        {
            width = va_arg(ap, int);
            if (width < 0) { left = 1; width = -width; }
            fmt++;
        }
        else
        {
            while (isdigit((unsigned char)*fmt))
                width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.')
        {
            fmt++;
            prec = 0;
            if (*fmt == '*')
            {
                prec = va_arg(ap, int);
                fmt++;
            }
            else
            {
                while (isdigit((unsigned char)*fmt))
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }
        for (;; fmt++)
        {
            if (*fmt == 'l') lng++;
            else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't')
                lng = sizeof(size_t) > sizeof(int) ? 2 : 0;
            else if (*fmt == 'h') ;
            else break;
        }

        p = tmp + sizeof(tmp);
        neg = 0;

        switch (*fmt)
        {
            case 'd':
            case 'i':
            {
                long long v = lng >= 2 ? va_arg(ap, long long)
                            : lng == 1 ? va_arg(ap, long)
                            : va_arg(ap, int);
                neg = v < 0;
                uv = neg ? -(unsigned long long)v : (unsigned long long)v;
                goto print_unsigned_dec;
            }
            case 'u':
                uv = lng >= 2 ? va_arg(ap, unsigned long long)
                   : lng == 1 ? va_arg(ap, unsigned long)
                   : va_arg(ap, unsigned int);
            print_unsigned_dec:
                do { *--p = '0' + uv % 10; uv /= 10; } while (uv);
                goto emit_number;
            case 'x':
            case 'X':
            case 'o':
            case 'p':
            {
                const char *digits = *fmt == 'X' ? "0123456789ABCDEF"
                                                 : "0123456789abcdef";
                unsigned base = *fmt == 'o' ? 8 : 16;
                if (*fmt == 'p')
                {
                    uv = (uintptr_t)va_arg(ap, void *);
                    alt = 1;
                }
                else
                {
                    uv = lng >= 2 ? va_arg(ap, unsigned long long)
                       : lng == 1 ? va_arg(ap, unsigned long)
                       : va_arg(ap, unsigned int);
                }
                do { *--p = digits[uv % base]; uv /= base; } while (uv);
                if (alt && base == 8)
                    *--p = '0';
                if (alt && base == 16)
                {
                    *--p = *fmt == 'X' ? 'X' : 'x';
                    *--p = '0';
                }
            }
            emit_number:
            {
                int digits = (int)(tmp + sizeof(tmp) - p);
                int zeros = prec > digits ? prec - digits : 0;
                int sign = neg || plus || space;
                int pad = width - digits - zeros - sign;
                if (zero && !left && prec < 0)
                {
                    zeros += pad > 0 ? pad : 0;
                    pad = 0;
                }
                if (!left) sink_pad(out, ' ', pad);
                if (neg) sink_putc(out, '-');
                else if (plus) sink_putc(out, '+');
                else if (space) sink_putc(out, ' ');
                sink_pad(out, '0', zeros);
                sink_puts(out, p, digits);
                if (left) sink_pad(out, ' ', pad);
                break;
            }
            case 'f':
            case 'F':
            case 'g':
            case 'G':
            case 'e':
            {
                double v = va_arg(ap, double);
                unsigned long long ip, fp, scale = 1;
                int i, digits;
                if (prec < 0) prec = 6;
                if (prec > 9) prec = 9;
                neg = v < 0;
                if (neg) v = -v;
                for (i = 0; i < prec; i++) scale *= 10;
                ip = (unsigned long long)v;
                fp = (unsigned long long)((v - (double)ip) * scale + 0.5);
                if (fp >= scale) { ip++; fp -= scale; }
                if (prec > 0)
                {
                    for (i = 0; i < prec; i++) { *--p = '0' + fp % 10; fp /= 10; }
                    *--p = '.';
                }
                do { *--p = '0' + ip % 10; ip /= 10; } while (ip);
                digits = (int)(tmp + sizeof(tmp) - p);
                prec = -1;
                (void)digits;
                goto emit_number;
            }
            case 'c':
                tmp[0] = (char)va_arg(ap, int);
                str = tmp;
                len = 1;
                goto emit_string;
            case 's':
                str = va_arg(ap, const char *);
                if (!str)
                    str = "(null)";
                len = 0;
                while (str[len] && (prec < 0 || len < prec))
                    len++;
            emit_string:
                if (!left) sink_pad(out, ' ', width - len);
                sink_puts(out, str, len);
                if (left) sink_pad(out, ' ', width - len);
                break;
            case 'n':
                *va_arg(ap, int *) = (int)out->len;
                break;
            case '%':
                sink_putc(out, '%');
                break;
            case '\0':
                fmt--;
                break;
            default:
                sink_putc(out, '%');
                sink_putc(out, *fmt);
                break;
        }
    }
    return (int)out->len;
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    sink_t out = { s, n, 0, NULL };
    int r = format(&out, fmt, ap);
    if (n)
        s[out.len < n ? out.len : n - 1] = '\0';
    return r;
}

int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(s, (size_t)-1 / 2, fmt, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    sink_t out = { NULL, 0, 0, f };
    return format(&out, fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

/* ======================================================================
 * Virtual filesystem
 *
 * Every file is a named block of memory. Read-only files point straight
 * at host memory (WADs). Files the engine writes (saves, config) are owned
 * by the VFS and passed to hooks.file_write when closed.
 * ==================================================================== */

#define VFS_MAX 64

typedef struct
{
    char name[128];
    unsigned char *data;
    size_t size, cap;
    int owned;          /* data was malloc'd by us */
    int used;
} vfs_entry_t;

struct pd_file
{
    vfs_entry_t *e;     /* NULL for the console streams */
    size_t pos;
    int write, dirty, eof, err;
    int console;
};

static vfs_entry_t vfs[VFS_MAX];
static FILE console_out = { NULL, 0, 1, 0, 0, 0, 1 };
static FILE console_in = { NULL, 0, 0, 0, 1, 0, 1 };

FILE *stdin = &console_in;
FILE *stdout = &console_out;
FILE *stderr = &console_out;

static void sink_putc(sink_t *s, char c)
{
    if (s->file)
        fputc(c, s->file);
    else if (s->len + 1 < s->cap)
        s->buf[s->len] = c;
    s->len++;
}

/* "./foo" and "foo" are the same file */
static const char *vfs_norm(const char *name)
{
    while (name[0] == '.' && name[1] == '/')
        name += 2;
    return name;
}

static vfs_entry_t *vfs_find(const char *name)
{
    int i;
    name = vfs_norm(name);
    for (i = 0; i < VFS_MAX; i++)
        if (vfs[i].used && !strcmp(vfs[i].name, name))
            return &vfs[i];
    return NULL;
}

static vfs_entry_t *vfs_new(const char *name)
{
    int i;
    name = vfs_norm(name);
    if (strlen(name) >= sizeof(vfs[0].name))
        return NULL;
    for (i = 0; i < VFS_MAX; i++)
    {
        if (!vfs[i].used)
        {
            memset(&vfs[i], 0, sizeof(vfs[i]));
            strcpy(vfs[i].name, name);
            vfs[i].used = 1;
            return &vfs[i];
        }
    }
    return NULL;
}

static void vfs_drop(vfs_entry_t *e)
{
    if (e->owned)
        free(e->data);
    memset(e, 0, sizeof(*e));
}

int pd_vfs_add(const char *name, const void *data, size_t len)
{
    vfs_entry_t *e = vfs_find(name);
    if (e)
        vfs_drop(e);
    e = vfs_new(name);
    if (!e)
        return -1;
    e->data = (unsigned char *)data;
    e->size = e->cap = len;
    return 0;
}

int pd_vfs_get(const char *name, const void **data, size_t *len)
{
    vfs_entry_t *e = vfs_find(name);
    if (!e)
        return -1;
    *data = e->data;
    *len = e->size;
    return 0;
}

FILE *fopen(const char *name, const char *mode)
{
    vfs_entry_t *e = vfs_find(name);
    int write = strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL;
    FILE *f;

    if (!write && !e && hooks.file_read)
    {
        /* ask the host for it (e.g. a savegame on disk) */
        void *data;
        size_t len;
        if (hooks.file_read(hooks.user, vfs_norm(name), &data, &len) == 0)
        {
            e = vfs_new(name);
            if (!e)
                return NULL;
            e->data = malloc(len ? len : 1);
            if (!e->data)
            {
                vfs_drop(e);
                return NULL;
            }
            memcpy(e->data, data, len);
            e->size = e->cap = len;
            e->owned = 1;
        }
    }

    if (write)
    {
        if (!e)
            e = vfs_new(name);
        if (!e)
            return NULL;
        if (!e->owned)
        {
            /* copy-on-write: never write into host memory */
            unsigned char *copy = malloc(e->size ? e->size : 1);
            if (!copy)
                return NULL;
            memcpy(copy, e->data, e->size);
            e->data = copy;
            e->cap = e->size;
            e->owned = 1;
        }
        if (strchr(mode, 'w'))
            e->size = 0;
    }

    if (!e)
    {
        pd_errno = ENOENT;
        return NULL;
    }

    f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->e = e;
    f->write = write;
    f->pos = strchr(mode, 'a') ? e->size : 0;
    return f;
}

int fflush(FILE *f)
{
    if (f && f->console)
    {
        log_flush();
        return 0;
    }
    if (f && f->dirty && hooks.file_write)
    {
        f->dirty = 0;
        return hooks.file_write(hooks.user, f->e->name, f->e->data, f->e->size);
    }
    return 0;
}

int fclose(FILE *f)
{
    int r;
    if (!f || f->console)
        return 0;
    r = fflush(f);
    free(f);
    return r;
}

size_t fread(void *buf, size_t sz, size_t n, FILE *f)
{
    size_t want = sz * n, avail;
    if (!f->e || sz == 0)
        return 0;
    avail = f->pos < f->e->size ? f->e->size - f->pos : 0;
    if (want > avail)
    {
        want = avail - avail % sz;
        f->eof = 1;
    }
    memcpy(buf, f->e->data + f->pos, want);
    f->pos += want;
    return want / sz;
}

static int vfs_reserve(vfs_entry_t *e, size_t need)
{
    unsigned char *nd;
    size_t cap;
    if (need <= e->cap)
        return 0;
    cap = e->cap ? e->cap : 4096;
    while (cap < need)
        cap *= 2;
    nd = realloc(e->data, cap);
    if (!nd)
        return -1;
    e->data = nd;
    e->cap = cap;
    return 0;
}

size_t fwrite(const void *buf, size_t sz, size_t n, FILE *f)
{
    size_t len = sz * n;
    if (f->console)
    {
        const char *p = buf;
        size_t i;
        for (i = 0; i < len; i++)
            log_putc(p[i]);
        return n;
    }
    if (!f->write || vfs_reserve(f->e, f->pos + len))
    {
        f->err = 1;
        return 0;
    }
    if (f->pos > f->e->size)
        memset(f->e->data + f->e->size, 0, f->pos - f->e->size);
    memcpy(f->e->data + f->pos, buf, len);
    f->pos += len;
    if (f->pos > f->e->size)
        f->e->size = f->pos;
    f->dirty = 1;
    return n;
}

int fseek(FILE *f, long off, int whence)
{
    long base;
    if (!f->e)
        return -1;
    base = whence == SEEK_SET ? 0
         : whence == SEEK_CUR ? (long)f->pos
         : (long)f->e->size;
    if (base + off < 0)
        return -1;
    f->pos = (size_t)(base + off);
    f->eof = 0;
    return 0;
}

long ftell(FILE *f)    { return f->e ? (long)f->pos : -1; }
int feof(FILE *f)      { return f->eof || (f->e && f->pos >= f->e->size); }
int ferror(FILE *f)    { return f->err; }
void setbuf(FILE *f, char *b) { (void)f; (void)b; }

int fgetc(FILE *f)
{
    if (!f->e || f->pos >= f->e->size)
    {
        f->eof = 1;
        return EOF;
    }
    return f->e->data[f->pos++];
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0, c = 0;
    while (i < n - 1 && (c = fgetc(f)) != EOF)
    {
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0 && c == EOF)
        return NULL;
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *f)
{
    unsigned char ch = (unsigned char)c;
    return fwrite(&ch, 1, 1, f) == 1 ? ch : EOF;
}

int fputs(const char *s, FILE *f)
{
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int puts(const char *s)
{
    fputs(s, stdout);
    return fputc('\n', stdout);
}

int putchar(int c) { return fputc(c, stdout); }

int remove(const char *name)
{
    vfs_entry_t *e = vfs_find(name);
    int r = -1;
    if (e)
    {
        vfs_drop(e);
        r = 0;
    }
    if (hooks.file_remove && hooks.file_remove(hooks.user, vfs_norm(name)) == 0)
        r = 0;
    return r;
}

int rename(const char *from, const char *to)
{
    FILE *src = fopen(from, "rb");
    FILE *dst;
    if (!src)
        return -1;
    dst = fopen(to, "wb");
    if (!dst)
    {
        fclose(src);
        return -1;
    }
    fwrite(src->e->data, 1, src->e->size, dst);
    fclose(src);
    fclose(dst);
    return remove(from);
}

/* ======================================================================
 * scanf family (the subset the engine uses: %d %i %u %x %o %s %c %[ %n)
 * ==================================================================== */

typedef struct
{
    const char *s;      /* string source */
    FILE *f;            /* or file source */
    int count;          /* characters consumed */
} src_t;

static int src_get(src_t *in)
{
    int c;
    if (in->f)
        c = fgetc(in->f);
    else
        c = *in->s ? (unsigned char)*in->s++ : EOF;
    if (c != EOF)
        in->count++;
    return c;
}

static void src_unget(src_t *in, int c)
{
    if (c == EOF)
        return;
    in->count--;
    if (in->f)
        in->f->pos--;
    else
        in->s--;
}

static int scan(src_t *in, const char *fmt, va_list ap)
{
    int assigned = 0, c;

    for (; *fmt; fmt++)
    {
        int suppress = 0, width = 0, lng = 0;

        if (isspace((unsigned char)*fmt))
        {
            while (isspace(c = src_get(in)))
                ;
            src_unget(in, c);
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%')
        {
            if (*fmt == '%')
                fmt++;
            c = src_get(in);
            if (c != (unsigned char)*fmt)
            {
                src_unget(in, c);
                goto done;
            }
            continue;
        }

        fmt++;
        if (*fmt == '*') { suppress = 1; fmt++; }
        while (isdigit((unsigned char)*fmt))
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'h')
            if (*fmt++ == 'l')
                lng = 1;

        switch (*fmt)
        {
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
            {
                char num[64];
                int n = 0, base;
                unsigned long v;
                char *end;

                while (isspace(c = src_get(in)))
                    ;
                if (!width || width > (int)sizeof(num) - 1)
                    width = sizeof(num) - 1;
                base = *fmt == 'i' ? 0 : (*fmt == 'x' || *fmt == 'X') ? 16
                     : *fmt == 'o' ? 8 : 10;
                while (c != EOF && n < width
                       && (isxdigit(c) || c == '-' || c == '+'
                           || ((c | 32) == 'x' && base != 10 && base != 8)))
                {
                    if (base == 10 && !isdigit(c) && c != '-' && c != '+')
                        break;
                    if (base == 8 && (c == '8' || c == '9' || isalpha(c)))
                        break;
                    num[n++] = (char)c;
                    c = src_get(in);
                }
                src_unget(in, c);
                num[n] = '\0';
                v = strtoul(num, &end, base);
                if (n == 0 || end == num)
                    goto done;
                /* give back anything strtoul did not use */
                while (*end)
                {
                    src_unget(in, (unsigned char)*end);
                    end++;
                }
                if (!suppress)
                {
                    if (lng)
                        *va_arg(ap, long *) = (long)v;
                    else
                        *va_arg(ap, int *) = (int)v;
                    assigned++;
                }
                break;
            }
            case 's':
            {
                char *out = suppress ? NULL : va_arg(ap, char *);
                int n = 0;
                while (isspace(c = src_get(in)))
                    ;
                if (c == EOF)
                    goto done;
                while (c != EOF && !isspace(c) && (!width || n < width))
                {
                    if (out) out[n] = (char)c;
                    n++;
                    c = src_get(in);
                }
                src_unget(in, c);
                if (out)
                {
                    out[n] = '\0';
                    assigned++;
                }
                break;
            }
            case 'c':
            {
                char *out = suppress ? NULL : va_arg(ap, char *);
                int n;
                if (!width)
                    width = 1;
                for (n = 0; n < width; n++)
                {
                    if ((c = src_get(in)) == EOF)
                        goto done;
                    if (out) out[n] = (char)c;
                }
                if (out)
                    assigned++;
                break;
            }
            case '[':
            {
                char *out = suppress ? NULL : va_arg(ap, char *);
                const char *set;
                int invert = 0, n = 0;

                fmt++;
                if (*fmt == '^') { invert = 1; fmt++; }
                set = fmt;
                if (*fmt == ']')
                    fmt++;
                while (*fmt && *fmt != ']')
                    fmt++;
                for (;;)
                {
                    const char *p;
                    int in_set = 0;
                    if (width && n >= width)
                        break;
                    c = src_get(in);
                    if (c == EOF)
                        break;
                    for (p = set; p < fmt; p++)
                        if (*p == c)
                            in_set = 1;
                    if (in_set == invert)
                    {
                        src_unget(in, c);
                        break;
                    }
                    if (out) out[n] = (char)c;
                    n++;
                }
                if (n == 0)
                    goto done;
                if (out)
                {
                    out[n] = '\0';
                    assigned++;
                }
                break;
            }
            case 'n':
                if (!suppress)
                    *va_arg(ap, int *) = in->count;
                break;
            default:
                goto done;
        }
    }
done:
    if (assigned == 0 && in->f && feof(in->f))
        return EOF;
    return assigned;
}

int vsscanf(const char *s, const char *fmt, va_list ap)
{
    src_t in = { s, NULL, 0 };
    return scan(&in, fmt, ap);
}

int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

int fscanf(FILE *f, const char *fmt, ...)
{
    src_t in = { NULL, f, 0 };
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = scan(&in, fmt, ap);
    va_end(ap);
    return r;
}
