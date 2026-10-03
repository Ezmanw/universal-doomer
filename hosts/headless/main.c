/*
 * PortaDoom headless host: runs the engine with no window, for tests.
 *
 *   pd_headless <iwad> [options] [-- doom args...]
 *     -tics N        run N tics (default 700 = 20 s)
 *     -shot T file   save Doom's own framebuffer at tic T as PPM
 *     -map T file    save a top-down picture built only from the world API
 *     -hash          print a hash of the framebuffer every 35 tics
 *     -events        print world events as they happen
 *     -norender      run without the software renderer
 *     -walk          hold forward + fire through pd_set_input, turning
 *                    a little each tic (tests host control)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "portadoom.h"

static jmp_buf fatal_jump;

static void host_log(void *user, const char *line)
{
    (void)user;
    fprintf(stderr, "[doom] %s\n", line);
}

static void host_fatal(void *user, const char *msg)
{
    (void)user;
    fprintf(stderr, "[doom] FATAL: %s\n", msg);
    longjmp(fatal_jump, 1);
}

static void *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    void *buf;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(*len);
    if (fread(buf, 1, *len, f) != *len)
    {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static void save_framebuffer(const char *path)
{
    const uint8_t *fb = pd_framebuffer();
    const uint8_t *pal = pd_palette();
    FILE *f = fopen(path, "wb");
    int i;
    if (!f || !fb)
        return;
    fprintf(f, "P6\n%d %d\n255\n", PD_SCREEN_W, PD_SCREEN_H);
    for (i = 0; i < PD_SCREEN_W * PD_SCREEN_H; i++)
        fwrite(&pal[fb[i] * 3], 1, 3, f);
    fclose(f);
    fprintf(stderr, "saved %s\n", path);
}

/* ---- top-down map drawn purely from pd_world_* ---------------------- */

#define MW 800
#define MH 800

static uint8_t mapimg[MH][MW][3];

static void plot(int x, int y, int r, int g, int b)
{
    if (x < 0 || y < 0 || x >= MW || y >= MH)
        return;
    mapimg[y][x][0] = (uint8_t)r;
    mapimg[y][x][1] = (uint8_t)g;
    mapimg[y][x][2] = (uint8_t)b;
}

static void line(int x0, int y0, int x1, int y1, int r, int g, int b)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;)
    {
        plot(x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* fill a convex polygon with a scanline */
static void fill_convex(const float *px, const float *py, int n,
                        int r, int g, int b)
{
    int y, i;
    float miny = 1e9f, maxy = -1e9f;
    for (i = 0; i < n; i++)
    {
        if (py[i] < miny) miny = py[i];
        if (py[i] > maxy) maxy = py[i];
    }
    for (y = (int)miny; y <= (int)maxy; y++)
    {
        float xl = 1e9f, xr = -1e9f, fy = y + 0.5f;
        for (i = 0; i < n; i++)
        {
            float x0 = px[i], y0 = py[i];
            float x1 = px[(i + 1) % n], y1 = py[(i + 1) % n];
            if ((fy >= y0 && fy < y1) || (fy >= y1 && fy < y0))
            {
                float x = x0 + (fy - y0) * (x1 - x0) / (y1 - y0);
                if (x < xl) xl = x;
                if (x > xr) xr = x;
            }
        }
        for (int x = (int)xl; x <= (int)xr; x++)
            plot(x, y, r, g, b);
    }
}

static void save_world_map(const char *path)
{
    pd_map_counts_t c;
    pd_vertex_t *v, poly[256];
    pd_line_t *lines;
    pd_sector_t *secs;
    pd_mobj_t *mobjs;
    pd_player_t pl;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f, scale;
    int i, n;
    FILE *f;

    pd_world_counts(&c);
    if (c.num_vertices == 0)
        return;
    v = malloc(sizeof(*v) * c.num_vertices);
    lines = malloc(sizeof(*lines) * c.num_lines);
    secs = malloc(sizeof(*secs) * c.num_sectors);
    pd_world_vertices(v);
    pd_world_lines(lines);
    pd_world_sectors(secs);

    for (i = 0; i < c.num_vertices; i++)
    {
        if (v[i].x < minx) minx = v[i].x;
        if (v[i].x > maxx) maxx = v[i].x;
        if (v[i].y < miny) miny = v[i].y;
        if (v[i].y > maxy) maxy = v[i].y;
    }
    scale = (MW - 20) / ((maxx - minx) > (maxy - miny) ? (maxx - minx) : (maxy - miny));
#define SX(x) (10 + ((x) - minx) * scale)
#define SY(y) (MH - 10 - ((y) - miny) * scale)

    memset(mapimg, 0, sizeof(mapimg));

    /* floors: subsector polygons shaded by floor height and light */
    for (i = 0; i < c.num_subsectors; i++)
    {
        float px[256], py[256];
        int sec, k;
        n = pd_world_subsector_poly(i, poly, 256, &sec);
        if (n < 3)
            continue;
        for (k = 0; k < n; k++)
        {
            px[k] = SX(poly[k].x);
            py[k] = SY(poly[k].y);
        }
        int h = (int)secs[sec].floor_height;
        int lt = secs[sec].light;
        int base = 40 + ((h + 64) & 0xff) / 3;
        fill_convex(px, py, n, base * lt / 255, (base + 30) * lt / 255,
                    (base + 10) * lt / 255);
    }

    /* walls */
    for (i = 0; i < c.num_lines; i++)
    {
        pd_vertex_t a = v[lines[i].v1], b = v[lines[i].v2];
        int two = lines[i].back_sector >= 0;
        int r = two ? 120 : 255, g = two ? 120 : 255, bl = two ? 120 : 255;
        if (lines[i].special)
            r = 255, g = 220, bl = 0;
        line((int)SX(a.x), (int)SY(a.y), (int)SX(b.x), (int)SY(b.y), r, g, bl);
    }

    /* things */
    n = pd_world_mobj_count();
    mobjs = malloc(sizeof(*mobjs) * (n ? n : 1));
    n = pd_world_mobjs(mobjs, n);
    for (i = 0; i < n; i++)
    {
        int x = (int)SX(mobjs[i].x), y = (int)SY(mobjs[i].y), r, g, b;
        if (mobjs[i].flags & PD_MOBJ_COUNTKILL)
            r = 255, g = 40, b = 40;
        else if (mobjs[i].flags & PD_MOBJ_PICKUP)
            r = 60, g = 160, b = 255;
        else
            r = 180, g = 180, b = 180;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                plot(x + dx, y + dy, r, g, b);
    }

    /* player and facing */
    pd_world_player(&pl);
    {
        int x = (int)SX(pl.x), y = (int)SY(pl.y);
        float a = pl.angle * 3.14159265f / 180.0f;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++)
                plot(x + dx, y + dy, 0, 255, 0);
        line(x, y, x + (int)(20 * __builtin_cosf(a)),
             y - (int)(20 * __builtin_sinf(a)), 0, 255, 0);
    }

    f = fopen(path, "wb");
    if (f)
    {
        fprintf(f, "P6\n%d %d\n255\n", MW, MH);
        fwrite(mapimg, 1, sizeof(mapimg), f);
        fclose(f);
        fprintf(stderr, "saved %s\n", path);
    }
    free(v);
    free(lines);
    free(secs);
    free(mobjs);
}

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

static const char *event_name(int t)
{
    static const char *names[] = {
        "NONE", "LEVEL_START", "LEVEL_END", "MOBJ_SPAWN", "MOBJ_REMOVE",
        "SECTOR", "SIDE", "SOUND", "MUSIC", "MUSIC_STOP", "PLAYER_HURT",
        "PLAYER_PICKUP" };
    return t >= 0 && t < (int)(sizeof(names) / sizeof(*names)) ? names[t] : "?";
}

int main(int argc, char **argv)
{
    pd_config_t cfg;
    pd_host_t host;
    size_t wadlen;
    void *wad;
    int tics = 700, i, hash = 0, show_events = 0, render = 1, walk = 0;
    int shot_tic = -1, map_tic = -1;
    const char *shot_file = NULL, *map_file = NULL;
    const char *doomargs[64];
    int ndoomargs = 0;

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <iwad> [options] [-- doom args]\n", argv[0]);
        return 1;
    }
    for (i = 2; i < argc; i++)
    {
        if (!strcmp(argv[i], "-tics") && i + 1 < argc)
            tics = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-shot") && i + 2 < argc)
        {
            shot_tic = atoi(argv[++i]);
            shot_file = argv[++i];
        }
        else if (!strcmp(argv[i], "-map") && i + 2 < argc)
        {
            map_tic = atoi(argv[++i]);
            map_file = argv[++i];
        }
        else if (!strcmp(argv[i], "-hash"))
            hash = 1;
        else if (!strcmp(argv[i], "-events"))
            show_events = 1;
        else if (!strcmp(argv[i], "-norender"))
            render = 0;
        else if (!strcmp(argv[i], "-walk"))
            walk = 1;
        else if (!strcmp(argv[i], "--"))
        {
            for (i++; i < argc && ndoomargs < 64; i++)
                doomargs[ndoomargs++] = argv[i];
        }
    }

    wad = read_file(argv[1], &wadlen);
    if (!wad)
    {
        fprintf(stderr, "can't read %s\n", argv[1]);
        return 1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.heap_size = pd_recommended_heap_size();
    cfg.heap = malloc(cfg.heap_size);
    cfg.iwad_name = argv[1];
    cfg.iwad_data = wad;
    cfg.iwad_size = wadlen;
    cfg.argc = ndoomargs;
    cfg.argv = doomargs;
    cfg.render = render;

    memset(&host, 0, sizeof(host));
    host.log = host_log;
    host.fatal = host_fatal;

    if (setjmp(fatal_jump))
        return 2;

    if (pd_init(&cfg, &host) != 0)
    {
        fprintf(stderr, "pd_init failed\n");
        return 1;
    }

    for (i = 1; i <= tics; i++)
    {
        int n, k;
        const pd_event_t *ev;

        if (walk)
        {
            pd_input_t in;
            memset(&in, 0, sizeof(in));
            in.forward = 25;
            in.turn = (i / 70) % 2 ? 2.0f : -1.0f;
            in.buttons = PD_BTN_FIRE | ((i / 20) % 2 ? PD_BTN_USE : 0);
            pd_set_input(&in);
        }

        if (pd_tick())
            break;

        ev = pd_world_events(&n);
        for (k = 0; k < n; k++)
        {
            if (!show_events && ev[k].type != PD_EV_LEVEL_START)
                continue;
            if (ev[k].type == PD_EV_SECTOR || ev[k].type == PD_EV_SIDE)
                continue;   /* too noisy (flickering lights) */
            printf("tic %5d %-13s id=%u index=%d name=%s pos=(%.0f,%.0f,%.0f)\n",
                   i, event_name(ev[k].type), ev[k].id, ev[k].index,
                   ev[k].name, ev[k].x, ev[k].y, ev[k].z);
        }

        if (hash && i % 35 == 0 && pd_framebuffer())
            printf("tic %5d fb %08x\n", i,
                   fnv1a(pd_framebuffer(), PD_SCREEN_W * PD_SCREEN_H));
        if (i == shot_tic)
            save_framebuffer(shot_file);
        if (i == map_tic)
            save_world_map(map_file);
    }

    {
        pd_game_t g;
        pd_player_t p;
        pd_world_game(&g);
        pd_world_player(&p);
        printf("end: tic %d state %d map %s skill %d kills %d/%d mobjs %d "
               "player (%.0f,%.0f,%.0f) angle %.0f health %d sky %s\n",
               pd_gametic(), g.state, g.map_name, g.skill, g.kills,
               g.total_kills, pd_world_mobj_count(), p.x, p.y, p.z, p.angle,
               p.health, g.sky_texture);
    }
    return 0;
}
