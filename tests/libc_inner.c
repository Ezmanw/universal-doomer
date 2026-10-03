#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static char heap[1<<20];
int inner(char *out, int n) {
    pd_libc_heap(heap, sizeof heap);
    char a[80], b[100]; int x=0,y=0,z=0;
    int r1 = sscanf("  0x1F", " 0x%x", &x);
    int r2 = sscanf("017", " 0%o", &y);
    int r3 = sscanf("-42", " %d", &z);
    FILE *f = fopen("cfg.txt", "w");
    fprintf(f, "key_up 72\nname \"hello world\"\n");
    fclose(f);
    f = fopen("cfg.txt", "r");
    int r4 = fscanf(f, "%79s %99[^\n]\n", a, b);
    char a2[80], b2[100];
    int r5 = fscanf(f, "%79s %99[^\n]\n", a2, b2);
    int e = feof(f);
    fclose(f);
    void *p1 = malloc(100), *p2 = malloc(200); free(p1); void *p3 = realloc(p2, 5000); free(p3);
    size_t used = pd_libc_heap_used();
    return snprintf(out, n, "%d:%x %d:%o %d:%d | %d [%s][%s] %d [%s][%s] eof=%d | %5.2f|%-4s|%04d|%+d|%s|%lld|%zu | atan1=%.6f tan=%.6f sin=%.6f | used=%zu",
        r1,x,r2,y,r3,z,r4,a,b,r5,a2,b2,e, 3.14159, "ab", 42, 7, "str", -1234567890123LL, (size_t)99,
        atan(1.0), tan(0.5), sin(2.0), used);
}
