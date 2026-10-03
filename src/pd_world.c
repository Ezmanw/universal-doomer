/*
 * PortaDoom world API: exposes the live game state (map, things, player,
 * events) and WAD resources so a host engine can draw Doom natively.
 */
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_player.h"
#include "info.h"
#include "m_fixed.h"
#include "m_misc.h"
#include "memio.h"
#include "mus2mid.h"
#include "p_local.h"
#include "p_mobj.h"
#include "p_pspr.h"
#include "r_data.h"
#include "r_defs.h"
#include "r_main.h"
#include "r_sky.h"
#include "r_state.h"
#include "sounds.h"
#include "tables.h"
#include "w_wad.h"
#include "z_zone.h"

#include "pd_internal.h"
#include "portadoom.h"

extern const char *pd_mobjtype_names[NUMMOBJTYPES];
const char *R_PD_TextureName(int texnum);
extern boolean menuactive;
extern int numflats;

#define FIX2F(x) ((float)(x) * (1.0f / 65536.0f))
#define ANG2DEG(a) ((float)((double)(uint32_t)(a) * (360.0 / 4294967296.0)))

static void copy8(char *dst, const char *src)
{
    int i;
    for (i = 0; i < 8 && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ======================================================================
 * Events
 * ==================================================================== */

#define MAX_EVENTS 4096

static pd_event_t events[MAX_EVENTS];
static int num_events;
static int map_serial;
static boolean level_loaded;

static pd_event_t *new_event(int type)
{
    pd_event_t *ev;
    if (num_events >= MAX_EVENTS)
        return NULL;
    ev = &events[num_events++];
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    return ev;
}

const pd_event_t *pd_world_events(int *count)
{
    *count = num_events;
    return events;
}

/* ======================================================================
 * Change tracking for sectors and sides
 * ==================================================================== */

typedef struct
{
    fixed_t floor, ceil;
    short floorpic, ceilingpic, light, special;
} sector_snap_t;

typedef struct
{
    fixed_t xoff, yoff;
    short top, bottom, mid;
} side_snap_t;

static sector_snap_t *sector_snaps;
static side_snap_t *side_snaps;
static int snap_sectors, snap_sides;

static void snap_sector(int i, sector_snap_t *s)
{
    sector_t *sec = &sectors[i];
    s->floor = sec->floorheight;
    s->ceil = sec->ceilingheight;
    s->floorpic = sec->floorpic;
    s->ceilingpic = sec->ceilingpic;
    s->light = sec->lightlevel;
    s->special = sec->special;
}

static void snap_side(int i, side_snap_t *s)
{
    side_t *sd = &sides[i];
    s->xoff = sd->textureoffset;
    s->yoff = sd->rowoffset;
    s->top = sd->toptexture;
    s->bottom = sd->bottomtexture;
    s->mid = sd->midtexture;
}

static void build_snapshots(void)
{
    int i;
    free(sector_snaps);
    free(side_snaps);
    snap_sectors = numsectors;
    snap_sides = numsides;
    sector_snaps = malloc(sizeof(*sector_snaps) * (numsectors ? numsectors : 1));
    side_snaps = malloc(sizeof(*side_snaps) * (numsides ? numsides : 1));
    for (i = 0; i < numsectors; i++)
        snap_sector(i, &sector_snaps[i]);
    for (i = 0; i < numsides; i++)
        snap_side(i, &side_snaps[i]);
}

static void diff_snapshots(void)
{
    int i;
    for (i = 0; i < snap_sectors && i < numsectors; i++)
    {
        sector_snap_t now;
        snap_sector(i, &now);
        if (memcmp(&now, &sector_snaps[i], sizeof(now)) != 0)
        {
            pd_event_t *ev = new_event(PD_EV_SECTOR);
            if (ev)
                ev->index = i;
            sector_snaps[i] = now;
        }
    }
    for (i = 0; i < snap_sides && i < numsides; i++)
    {
        side_snap_t now;
        snap_side(i, &now);
        if (memcmp(&now, &side_snaps[i], sizeof(now)) != 0)
        {
            pd_event_t *ev = new_event(PD_EV_SIDE);
            if (ev)
                ev->index = i;
            side_snaps[i] = now;
        }
    }
}

/* ======================================================================
 * Subsector polygons, built by clipping a big box down the BSP tree
 * ==================================================================== */

typedef struct
{
    double x, y;
} dpoint_t;

#define MAX_POLY 128

static pd_vertex_t *poly_points;    /* all polygons, back to back */
static int *poly_start;             /* per subsector: first point */
static int *poly_len;               /* per subsector: point count */
static int poly_count, poly_cap;

/* Keep the part of the polygon on the right (front) side of the directed
 * line (px,py)+(dx,dy). Doom's convention: front = right side. */
static int clip_poly(dpoint_t *in, int n, dpoint_t *out,
                     double px, double py, double dx, double dy)
{
    int i, m = 0;
    for (i = 0; i < n && m < MAX_POLY - 1; i++)
    {
        dpoint_t a = in[i], b = in[(i + 1) % n];
        /* > 0 means left side (back) */
        double sa = dx * (a.y - py) - dy * (a.x - px);
        double sb = dx * (b.y - py) - dy * (b.x - px);
        const double eps = 1e-6 * (dx * dx + dy * dy + 1);
        int ina = sa <= eps, inb = sb <= eps;

        if (ina)
            out[m++] = a;
        if (ina != inb && m < MAX_POLY)
        {
            double t = sa / (sa - sb);
            out[m].x = a.x + (b.x - a.x) * t;
            out[m].y = a.y + (b.y - a.y) * t;
            m++;
        }
    }
    return m;
}

static void poly_add_subsector(int ss, dpoint_t *poly, int n)
{
    static dpoint_t tmp[MAX_POLY];
    subsector_t *sub = &subsectors[ss];
    int i;

    /* finish the shape using the subsector's own segs */
    for (i = 0; i < sub->numlines && n > 2; i++)
    {
        seg_t *seg = &segs[sub->firstline + i];
        double x1 = FIX2F(seg->v1->x), y1 = FIX2F(seg->v1->y);
        double x2 = FIX2F(seg->v2->x), y2 = FIX2F(seg->v2->y);
        n = clip_poly(poly, n, tmp, x1, y1, x2 - x1, y2 - y1);
        memcpy(poly, tmp, sizeof(dpoint_t) * n);
    }

    if (poly_count + n > poly_cap)
    {
        poly_cap = (poly_count + n) * 2;
        poly_points = realloc(poly_points, sizeof(pd_vertex_t) * poly_cap);
    }
    for (i = 0; i < n; i++)
    {
        poly_points[poly_count + i].x = (float)poly[i].x;
        poly_points[poly_count + i].y = (float)poly[i].y;
    }
    poly_start[ss] = poly_count;
    poly_len[ss] = n;
    poly_count += n;
}

static void poly_walk(int nodenum, dpoint_t *poly, int n)
{
    dpoint_t *front, *back;
    node_t *node;
    int nf, nb;
    double px, py, dx, dy;

    if (nodenum & NF_SUBSECTOR)
    {
        int ss = numnodes == 0 ? 0 : nodenum & ~NF_SUBSECTOR;
        if (ss < numsubsectors)
            poly_add_subsector(ss, poly, n);
        return;
    }
    if (nodenum >= numnodes)
        return;

    /* heap, not stack: BSP trees can be deep and WASM stacks are small */
    front = malloc(sizeof(dpoint_t) * MAX_POLY * 2);
    back = front + MAX_POLY;

    node = &nodes[nodenum];
    px = FIX2F(node->x);
    py = FIX2F(node->y);
    dx = FIX2F(node->dx);
    dy = FIX2F(node->dy);

    nf = clip_poly(poly, n, front, px, py, dx, dy);
    nb = clip_poly(poly, n, back, px, py, -dx, -dy);   /* left side */

    poly_walk(node->children[0], front, nf);
    poly_walk(node->children[1], back, nb);
    free(front);
}

static void build_polys(void)
{
    static dpoint_t box[MAX_POLY];
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    int i;

    free(poly_start);
    free(poly_len);
    poly_start = malloc(sizeof(int) * (numsubsectors + 1));
    poly_len = malloc(sizeof(int) * (numsubsectors + 1));
    for (i = 0; i <= numsubsectors; i++)
        poly_start[i] = poly_len[i] = 0;
    poly_count = 0;

    for (i = 0; i < numvertexes; i++)
    {
        double x = FIX2F(vertexes[i].x), y = FIX2F(vertexes[i].y);
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    minx -= 64; miny -= 64; maxx += 64; maxy += 64;
    box[0].x = minx; box[0].y = miny;
    box[1].x = minx; box[1].y = maxy;
    box[2].x = maxx; box[2].y = maxy;
    box[3].x = maxx; box[3].y = miny;

    /* a map with no nodes is a single subsector */
    poly_walk(numnodes ? numnodes - 1 : NF_SUBSECTOR, box, 4);

}

/* ======================================================================
 * Things
 * ==================================================================== */

static uint32_t next_mobj_id = 1;

/* map arrays the last LEVEL_START was built from */
static sector_t *seen_sectors;
static int seen_numsectors;

static mobj_t *first_mobj(thinker_t **iter)
{
    thinker_t *th;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        if (th->function.acp1 == (actionf_p1)P_MobjThinker)
        {
            *iter = th;
            return (mobj_t *)th;
        }
    }
    return NULL;
}

static mobj_t *next_mobj(thinker_t **iter)
{
    thinker_t *th;
    for (th = (*iter)->next; th != &thinkercap; th = th->next)
    {
        if (th->function.acp1 == (actionf_p1)P_MobjThinker)
        {
            *iter = th;
            return (mobj_t *)th;
        }
    }
    return NULL;
}

static void assign_new_ids(void)
{
    thinker_t *it;
    mobj_t *mo;

    if (gamestate != GS_LEVEL && !level_loaded)
        return;
    for (mo = first_mobj(&it); mo; mo = next_mobj(&it))
    {
        if (mo->pd_id == 0)
        {
            pd_event_t *ev;
            mo->pd_id = next_mobj_id++;
            ev = new_event(PD_EV_MOBJ_SPAWN);
            if (ev)
            {
                ev->id = mo->pd_id;
                ev->index = mo->type;
                ev->x = FIX2F(mo->x);
                ev->y = FIX2F(mo->y);
                ev->z = FIX2F(mo->z);
            }
        }
    }
}

void pd_hook_mobj_remove(mobj_t *mo)
{
    pd_event_t *ev;
    if (mo->pd_id == 0)
        return;     /* born and gone within one tic; host never saw it */
    ev = new_event(PD_EV_MOBJ_REMOVE);
    if (ev)
        ev->id = mo->pd_id;
}

static void fill_mobj(mobj_t *mo, pd_mobj_t *out)
{
    int frame = mo->frame & FF_FRAMEMASK;
    int flags = 0;

    memset(out, 0, sizeof(*out));
    out->id = mo->pd_id;
    out->type = mo->type;
    out->doomednum = mo->info ? mo->info->doomednum : -1;
    out->x = FIX2F(mo->x);
    out->y = FIX2F(mo->y);
    out->z = FIX2F(mo->z);
    out->angle = ANG2DEG(mo->angle);
    out->radius = FIX2F(mo->radius);
    out->height = FIX2F(mo->height);
    out->momx = FIX2F(mo->momx);
    out->momy = FIX2F(mo->momy);
    out->momz = FIX2F(mo->momz);
    out->health = mo->health;

    if (mo->flags & MF_SOLID)       flags |= PD_MOBJ_SOLID;
    if (mo->flags & MF_SHOOTABLE)   flags |= PD_MOBJ_SHOOTABLE;
    if (mo->flags & MF_MISSILE)     flags |= PD_MOBJ_MISSILE;
    if (mo->flags & MF_CORPSE)      flags |= PD_MOBJ_CORPSE;
    if (mo->flags & MF_SPECIAL)     flags |= PD_MOBJ_PICKUP;
    if (mo->flags & MF_COUNTKILL)   flags |= PD_MOBJ_COUNTKILL;
    if (mo->flags & MF_SHADOW)      flags |= PD_MOBJ_SHADOW;
    if (mo->player)                 flags |= PD_MOBJ_PLAYER;
    if (mo->flags & MF_NOSECTOR)    flags |= PD_MOBJ_NOSECTOR;
    if (mo->flags & MF_FLOAT)       flags |= PD_MOBJ_FLOAT;
    out->flags = flags;

    if (mo->sprite >= 0 && mo->sprite < NUMSPRITES)
        copy8(out->sprite, sprnames[mo->sprite]);
    out->sprite[4] = '\0';
    out->frame = frame;
    out->fullbright = (mo->frame & FF_FULLBRIGHT) != 0;
    out->rotations = 1;
    if (sprites && mo->sprite < numsprites
        && frame < sprites[mo->sprite].numframes
        && sprites[mo->sprite].spriteframes[frame].rotate)
        out->rotations = 8;
    out->target_id = mo->target ? mo->target->pd_id : 0;
}

int pd_world_mobj_count(void)
{
    thinker_t *it;
    mobj_t *mo;
    int n = 0;
    if (!level_loaded)
        return 0;
    for (mo = first_mobj(&it); mo; mo = next_mobj(&it))
        n++;
    return n;
}

int pd_world_mobjs(pd_mobj_t *out, int max)
{
    thinker_t *it;
    mobj_t *mo;
    int n = 0;
    if (!level_loaded)
        return 0;
    for (mo = first_mobj(&it); mo && n < max; mo = next_mobj(&it))
        fill_mobj(mo, &out[n++]);
    return n;
}

int pd_world_mobj(uint32_t id, pd_mobj_t *out)
{
    thinker_t *it;
    mobj_t *mo;
    if (!level_loaded || id == 0)
        return 0;
    for (mo = first_mobj(&it); mo; mo = next_mobj(&it))
    {
        if (mo->pd_id == id)
        {
            fill_mobj(mo, out);
            return 1;
        }
    }
    return 0;
}

const char *pd_mobj_type_name(int type)
{
    if (type < 0 || type >= NUMMOBJTYPES)
        return "";
    return pd_mobjtype_names[type];
}

/* ======================================================================
 * Level lifecycle hooks
 * ==================================================================== */

void pd_hook_level_start(void)
{
    pd_event_t *ev;
    thinker_t *it;
    mobj_t *mo;

    level_loaded = true;
    map_serial++;

    /* every thing is announced again with a fresh id after LEVEL_START */
    for (mo = first_mobj(&it); mo; mo = next_mobj(&it))
        mo->pd_id = 0;

    seen_sectors = sectors;
    seen_numsectors = numsectors;
    build_snapshots();
    build_polys();

    ev = new_event(PD_EV_LEVEL_START);
    if (ev)
    {
        ev->index = map_serial;
        ev->x = (float)gameepisode;
        ev->y = (float)gamemap;
    }
}

void pd_hook_level_end(void)
{
    new_event(PD_EV_LEVEL_END);
}

void pd_world_begin_tick(void)
{
    /* events from start-up are delivered with the first tick */
    if (pd_clock_tics > 0)
        num_events = 0;
}

void pd_world_end_tick(void)
{
    /* safety net: a map loaded by some path that skipped the hook */
    if (gamestate == GS_LEVEL
        && (sectors != seen_sectors || numsectors != seen_numsectors))
        pd_hook_level_start();

    if (level_loaded && gamestate == GS_LEVEL)
    {
        assign_new_ids();
        diff_snapshots();
    }
}

/* ======================================================================
 * Sounds and music
 * ==================================================================== */

void pd_hook_sound(void *origin, int sfx_id)
{
    pd_event_t *ev;
    sfxinfo_t *sfx;

    if (sfx_id < 1 || sfx_id >= NUMSFX)
        return;
    ev = new_event(PD_EV_SOUND);
    if (!ev)
        return;
    sfx = &S_sfx[sfx_id];
    if (sfx->link)
        sfx = sfx->link;
    copy8(ev->name, sfx->name);
    ev->index = sfx_id;
    ev->volume = 127;

    if (origin)
    {
        byte *p = origin;
        if (sectors && p >= (byte *)sectors
            && p < (byte *)(sectors + numsectors))
        {
            /* a sector's sound origin (doors, lifts) */
            int s = (int)((p - (byte *)sectors) / sizeof(sector_t));
            degenmobj_t *o = &sectors[s].soundorg;
            ev->x = FIX2F(o->x);
            ev->y = FIX2F(o->y);
            ev->z = FIX2F((sectors[s].floorheight
                           + sectors[s].ceilingheight) / 2);
            ev->id = 0;
            ev->index = sfx_id;
        }
        else
        {
            mobj_t *mo = origin;
            ev->x = FIX2F(mo->x);
            ev->y = FIX2F(mo->y);
            ev->z = FIX2F(mo->z);
            ev->id = mo->pd_id;
        }
    }
}

void pd_hook_music(const char *lumpname, int lumpnum, int looping)
{
    pd_event_t *ev = new_event(PD_EV_MUSIC);
    int i;
    if (!ev)
        return;
    if (lumpnum >= 0 && lumpnum < (int)numlumps)
        copy8(ev->name, lumpinfo[lumpnum].name);
    else if (lumpname)
        copy8(ev->name, lumpname);
    for (i = 0; ev->name[i]; i++)
        ev->name[i] = (char)toupper((unsigned char)ev->name[i]);
    ev->index = looping;
}

void pd_hook_music_stop(void)
{
    new_event(PD_EV_MUSIC_STOP);
}

void pd_hook_pickup(player_t *player, mobj_t *item)
{
    pd_event_t *ev;
    if (player != &players[consoleplayer])
        return;
    ev = new_event(PD_EV_PLAYER_PICKUP);
    if (!ev)
        return;
    ev->id = item->pd_id;
    ev->index = item->type;
    if (item->sprite >= 0 && item->sprite < NUMSPRITES)
        copy8(ev->name, sprnames[item->sprite]);
}

void pd_hook_player_hurt(player_t *player, int damage)
{
    pd_event_t *ev;
    if (player != &players[consoleplayer])
        return;
    ev = new_event(PD_EV_PLAYER_HURT);
    if (ev)
        ev->index = damage;
}

/* ======================================================================
 * Game, map and player queries
 * ==================================================================== */

void pd_world_game(pd_game_t *out)
{
    memset(out, 0, sizeof(*out));
    switch (gamestate)
    {
        case GS_LEVEL:        out->state = PD_STATE_LEVEL; break;
        case GS_INTERMISSION: out->state = PD_STATE_INTERMISSION; break;
        case GS_FINALE:       out->state = PD_STATE_FINALE; break;
        default:              out->state = PD_STATE_DEMOSCREEN; break;
    }
    out->episode = gameepisode;
    out->map = gamemap;
    if (gamemode == commercial)
        M_snprintf(out->map_name, sizeof(out->map_name), "MAP%02d", gamemap);
    else
        M_snprintf(out->map_name, sizeof(out->map_name), "E%dM%d",
                   gameepisode, gamemap);
    out->skill = gameskill;
    out->menu_active = menuactive;
    out->paused = paused;
    out->automap_active = automapactive;
    out->demo_playback = demoplayback;
    out->level_time = leveltime;
    out->kills = players[consoleplayer].killcount;
    out->items = players[consoleplayer].itemcount;
    out->secrets = players[consoleplayer].secretcount;
    out->total_kills = totalkills;
    out->total_items = totalitems;
    out->total_secrets = totalsecret;
    out->map_serial = map_serial;
    {
        const char *sky = R_PD_TextureName(skytexture);
        copy8(out->sky_texture, sky);
    }
}

void pd_world_counts(pd_map_counts_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!level_loaded)
        return;
    out->num_vertices = numvertexes;
    out->num_lines = numlines;
    out->num_sides = numsides;
    out->num_sectors = numsectors;
    out->num_subsectors = numsubsectors;
}

void pd_world_vertices(pd_vertex_t *out)
{
    int i;
    for (i = 0; level_loaded && i < numvertexes; i++)
    {
        out[i].x = FIX2F(vertexes[i].x);
        out[i].y = FIX2F(vertexes[i].y);
    }
}

void pd_world_lines(pd_line_t *out)
{
    int i;
    for (i = 0; level_loaded && i < numlines; i++)
    {
        line_t *l = &lines[i];
        out[i].v1 = (int)(l->v1 - vertexes);
        out[i].v2 = (int)(l->v2 - vertexes);
        out[i].flags = (unsigned short)l->flags;
        out[i].special = l->special;
        out[i].tag = l->tag;
        out[i].front_side = l->sidenum[0];
        out[i].back_side = l->sidenum[1];
        out[i].front_sector = l->frontsector ? (int)(l->frontsector - sectors) : -1;
        out[i].back_sector = l->backsector ? (int)(l->backsector - sectors) : -1;
    }
}

static void texname(char *dst, int texnum)
{
    const char *n = R_PD_TextureName(texnum);
    if (n)
        copy8(dst, n);
    else
        strcpy(dst, "-");
}

void pd_world_side(int i, pd_side_t *out)
{
    side_t *sd;
    memset(out, 0, sizeof(*out));
    if (!level_loaded || i < 0 || i >= numsides)
        return;
    sd = &sides[i];
    out->x_offset = FIX2F(sd->textureoffset);
    out->y_offset = FIX2F(sd->rowoffset);
    texname(out->top, sd->toptexture);
    texname(out->middle, sd->midtexture);
    texname(out->bottom, sd->bottomtexture);
    out->sector = (int)(sd->sector - sectors);
}

void pd_world_sides(pd_side_t *out)
{
    int i;
    for (i = 0; level_loaded && i < numsides; i++)
        pd_world_side(i, &out[i]);
}

void pd_world_sector(int i, pd_sector_t *out)
{
    sector_t *s;
    memset(out, 0, sizeof(*out));
    if (!level_loaded || i < 0 || i >= numsectors)
        return;
    s = &sectors[i];
    out->floor_height = FIX2F(s->floorheight);
    out->ceiling_height = FIX2F(s->ceilingheight);
    copy8(out->floor_flat, lumpinfo[firstflat + s->floorpic].name);
    copy8(out->ceiling_flat, lumpinfo[firstflat + s->ceilingpic].name);
    out->light = s->lightlevel;
    out->special = s->special;
    out->tag = s->tag;
}

void pd_world_sectors(pd_sector_t *out)
{
    int i;
    for (i = 0; level_loaded && i < numsectors; i++)
        pd_world_sector(i, &out[i]);
}

int pd_world_point_sector(float x, float y)
{
    subsector_t *ss;
    if (!level_loaded)
        return -1;
    ss = R_PointInSubsector((fixed_t)(x * 65536.0f), (fixed_t)(y * 65536.0f));
    return (int)(ss->sector - sectors);
}

int pd_world_subsector_poly(int index, pd_vertex_t *out, int max, int *sector)
{
    int start, n, i;

    if (!level_loaded || index < 0 || index >= numsubsectors || !poly_start)
        return 0;
    start = poly_start[index];
    n = poly_len[index];
    if (sector)
        *sector = (int)(subsectors[index].sector - sectors);
    for (i = 0; i < n && i < max; i++)
        out[i] = poly_points[start + i];
    return n;
}

void pd_world_player(pd_player_t *out)
{
    player_t *p = &players[consoleplayer];
    int i;

    memset(out, 0, sizeof(*out));
    if (p->mo)
    {
        out->mobj_id = p->mo->pd_id;
        out->x = FIX2F(p->mo->x);
        out->y = FIX2F(p->mo->y);
        out->z = FIX2F(p->mo->z);
        out->angle = ANG2DEG(p->mo->angle);
    }
    out->view_z = FIX2F(p->viewz);
    out->health = p->health;
    out->armor = p->armorpoints;
    out->armor_type = p->armortype;
    out->ready_weapon = p->readyweapon;
    out->pending_weapon = p->pendingweapon == wp_nochange ? -1 : p->pendingweapon;
    for (i = 0; i < NUMWEAPONS && i < 9; i++)
        out->owned_weapons[i] = p->weaponowned[i];
    for (i = 0; i < NUMAMMO && i < 4; i++)
    {
        out->ammo[i] = p->ammo[i];
        out->max_ammo[i] = p->maxammo[i];
    }
    for (i = 0; i < NUMCARDS && i < 6; i++)
        out->cards[i] = p->cards[i];
    for (i = 0; i < NUMPOWERS && i < 6; i++)
        out->powers[i] = p->powers[i];
    out->damage_count = p->damagecount;
    out->bonus_count = p->bonuscount;
    out->extra_light = p->extralight;
    out->dead = p->playerstate == PST_DEAD;
    out->fixed_colormap = p->fixedcolormap;

    if (p->psprites[ps_weapon].state)
    {
        state_t *st = p->psprites[ps_weapon].state;
        copy8(out->weapon_sprite, sprnames[st->sprite]);
        out->weapon_sprite[4] = '\0';
        out->weapon_frame = st->frame & FF_FRAMEMASK;
        out->weapon_fullbright = (st->frame & FF_FULLBRIGHT) != 0;
        out->weapon_sx = FIX2F(p->psprites[ps_weapon].sx);
        out->weapon_sy = FIX2F(p->psprites[ps_weapon].sy);
    }
    if (p->psprites[ps_flash].state)
    {
        state_t *st = p->psprites[ps_flash].state;
        copy8(out->flash_sprite, sprnames[st->sprite]);
        out->flash_sprite[4] = '\0';
        out->flash_frame = st->frame & FF_FRAMEMASK;
    }
    out->message = p->message;
}

/* ======================================================================
 * Resources
 * ==================================================================== */

static int rd16(const byte *p) { return (short)(p[0] | (p[1] << 8)); }
static int rd32(const byte *p)
{
    return (int)(p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24));
}

const uint8_t *pd_palette_base(void)
{
    int lump = W_CheckNumForName("PLAYPAL");
    return lump < 0 ? NULL : W_CacheLumpNum(lump, PU_STATIC);
}

const void *pd_res_lump(const char *name, size_t *len)
{
    int lump = W_CheckNumForName((char *)name);
    if (lump < 0)
        return NULL;
    if (len)
        *len = W_LumpLength(lump);
    return W_CacheLumpNum(lump, PU_STATIC);
}

/* Draw a patch into a 16-bit image (0xFFFF = transparent). */
static void draw_patch(const byte *patch, int plen, uint16_t *img,
                       int w, int h, int ox, int oy)
{
    int pw, x;
    if (plen < 8)
        return;
    pw = rd16(patch);
    for (x = 0; x < pw; x++)
    {
        int dx = ox + x, colofs, guard = 0;
        const byte *col;
        if (dx < 0 || dx >= w)
            continue;
        colofs = rd32(patch + 8 + x * 4);
        if (colofs < 0 || colofs >= plen)
            continue;
        col = patch + colofs;
        while (*col != 0xff && col + 3 < patch + plen && guard++ < 256)
        {
            int top = col[0], len = col[1], y;
            const byte *src = col + 3;
            for (y = 0; y < len; y++)
            {
                int dy = oy + top + y;
                if (dy >= 0 && dy < h && src + y < patch + plen)
                    img[dy * w + dx] = src[y];
            }
            col += len + 4;
        }
    }
}

int pd_res_patch(const char *name, int *w, int *h, int *left_offset,
                 int *top_offset, uint16_t *out, size_t cap)
{
    int lump = W_CheckNumForName((char *)name);
    const byte *p;
    int pw, ph, len, i;

    if (lump < 0)
        return 0;
    p = W_CacheLumpNum(lump, PU_STATIC);
    len = W_LumpLength(lump);
    if (len < 8)
        return 0;
    pw = rd16(p);
    ph = rd16(p + 2);
    if (w) *w = pw;
    if (h) *h = ph;
    if (left_offset) *left_offset = rd16(p + 4);
    if (top_offset) *top_offset = rd16(p + 6);
    if (out && cap >= (size_t)(pw * ph))
    {
        for (i = 0; i < pw * ph; i++)
            out[i] = 0xffff;
        draw_patch(p, len, out, pw, ph, 0, 0);
    }
    return 1;
}

/* Find a texture definition in TEXTURE1/TEXTURE2. */
static const byte *find_texture_def(const char *name, int *deflen_out)
{
    static const char *tlumps[2] = { "TEXTURE1", "TEXTURE2" };
    int t;

    for (t = 0; t < 2; t++)
    {
        int lump = W_CheckNumForName((char *)tlumps[t]);
        const byte *data;
        int len, n, i;
        if (lump < 0)
            continue;
        data = W_CacheLumpNum(lump, PU_STATIC);
        len = W_LumpLength(lump);
        n = rd32(data);
        for (i = 0; i < n && 4 + i * 4 + 4 <= len; i++)
        {
            int ofs = rd32(data + 4 + i * 4);
            if (ofs < 0 || ofs + 22 > len)
                continue;
            if (!strncasecmp((const char *)data + ofs, name, 8))
            {
                *deflen_out = len - ofs;
                return data + ofs;
            }
        }
    }
    return NULL;
}

int pd_res_texture(const char *name, int *w, int *h, uint16_t *out, size_t cap)
{
    const byte *def, *pnames;
    int deflen, tw, th, npatches, i, pnlump, numpnames;

    def = find_texture_def(name, &deflen);
    if (!def)
        return 0;
    tw = rd16(def + 12);
    th = rd16(def + 14);
    npatches = rd16(def + 20);
    if (w) *w = tw;
    if (h) *h = th;
    if (!out || cap < (size_t)(tw * th))
        return 1;

    for (i = 0; i < tw * th; i++)
        out[i] = 0xffff;

    pnlump = W_CheckNumForName("PNAMES");
    if (pnlump < 0)
        return 1;
    pnames = W_CacheLumpNum(pnlump, PU_STATIC);
    numpnames = rd32(pnames);

    for (i = 0; i < npatches && 22 + i * 10 + 10 <= deflen; i++)
    {
        const byte *mp = def + 22 + i * 10;
        int ox = rd16(mp), oy = rd16(mp + 2), pnum = rd16(mp + 4);
        char pname[9];
        int plump;
        if (pnum < 0 || pnum >= numpnames)
            continue;
        copy8(pname, (const char *)pnames + 4 + pnum * 8);
        plump = W_CheckNumForName(pname);
        if (plump < 0)
            continue;
        draw_patch(W_CacheLumpNum(plump, PU_STATIC), W_LumpLength(plump),
                   out, tw, th, ox, oy);
    }
    return 1;
}

int pd_res_flat(const char *name, uint8_t *out)
{
    int lump = W_CheckNumForName((char *)name);
    if (lump < 0 || W_LumpLength(lump) < 4096)
        return 0;
    if (out)
        memcpy(out, W_CacheLumpNum(lump, PU_STATIC), 4096);
    return 1;
}

const char *pd_res_flat_current(const char *name)
{
    static char buf[9];
    int lump = W_CheckNumForName((char *)name);
    int flat;
    if (lump < firstflat || lump >= firstflat + numflats || !flattranslation)
        return name;
    flat = flattranslation[lump - firstflat];
    copy8(buf, lumpinfo[firstflat + flat].name);
    return buf;
}

const char *pd_res_texture_current(const char *name)
{
    static char buf[9];
    int tex = R_CheckTextureNumForName((char *)name);
    const char *n;
    if (tex <= 0 || !texturetranslation)
        return name;
    n = R_PD_TextureName(texturetranslation[tex]);
    if (!n)
        return name;
    copy8(buf, n);
    return buf;
}

int pd_res_sprite(const char *sprite, int frame, int rotation,
                  int *w, int *h, int *left_offset, int *top_offset,
                  int *flip, uint16_t *out, size_t cap)
{
    int s, lump;
    spriteframe_t *sf;
    char lumpname[9];

    for (s = 0; s < numsprites; s++)
        if (!strncasecmp(sprnames[s], sprite, 4))
            break;
    if (s >= numsprites || frame < 0 || frame >= sprites[s].numframes)
        return 0;
    sf = &sprites[s].spriteframes[frame];
    if (!sf->rotate || rotation < 1 || rotation > 8)
        rotation = 1;
    lump = sf->lump[rotation - 1];
    if (lump < 0)
        return 0;
    if (flip)
        *flip = sf->flip[rotation - 1];
    copy8(lumpname, lumpinfo[firstspritelump + lump].name);
    return pd_res_patch(lumpname, w, h, left_offset, top_offset, out, cap);
}

const uint8_t *pd_res_sound(const char *name, int *samples, int *rate)
{
    char lumpname[9];
    const byte *data;
    int lump, len;
    uint32_t n;

    M_snprintf(lumpname, sizeof(lumpname), "ds%s", name);
    lump = W_CheckNumForName(lumpname);
    if (lump < 0)
        lump = W_CheckNumForName((char *)name);
    if (lump < 0)
        return NULL;
    data = W_CacheLumpNum(lump, PU_STATIC);
    len = W_LumpLength(lump);
    if (len < 8 || data[0] != 3)
        return NULL;
    n = (uint32_t)rd32(data + 4);
    if (n > (uint32_t)len - 8 || n <= 32)
        return NULL;
    if (rate)
        *rate = data[2] | (data[3] << 8);
    if (samples)
        *samples = (int)n - 32;
    return data + 8 + 16;
}

size_t pd_res_music_midi(const char *name, uint8_t *out, size_t cap)
{
    int lump = W_CheckNumForName((char *)name);
    const byte *data;
    MEMFILE *in, *mid;
    void *buf;
    size_t len;

    if (lump < 0)
        return 0;
    data = W_CacheLumpNum(lump, PU_STATIC);
    len = W_LumpLength(lump);

    /* already MIDI (some PWADs) */
    if (len >= 4 && !memcmp(data, "MThd", 4))
    {
        if (out && cap >= len)
            memcpy(out, data, len);
        return len;
    }

    in = mem_fopen_read((void *)data, len);
    mid = mem_fopen_write();
    if (mus2mid(in, mid))
    {
        mem_fclose(in);
        mem_fclose(mid);
        return 0;
    }
    mem_get_buf(mid, &buf, &len);
    if (out && cap >= len)
        memcpy(out, buf, len);
    mem_fclose(in);
    mem_fclose(mid);
    return len;
}
