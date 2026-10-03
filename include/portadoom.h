/*
 * PortaDoom: the full Doom engine as a portable library.
 *
 * Doom runs the whole game (rules, monsters, weapons, doors, menus, saves,
 * demos). The host engine decides how to show it:
 *
 *   - Native ("doomified"): read the world each tick with pd_world_* and
 *     draw it with the host's own renderer (blocks, brushes, meshes...).
 *   - Screen: read Doom's own 320x200 picture with pd_framebuffer().
 *
 * The library uses no OS functions. It allocates only from the heap block
 * given in pd_config_t and reads/writes files only through pd_host_t.
 *
 * One engine per process (Doom keeps global state). For several, load the
 * WASM build once per instance.
 *
 * Coordinates are Doom map units (1 unit ~ 1 inch; a player is 56 tall).
 * X is east, Y is north, Z is up. Angles are degrees, 0 = east, CCW.
 */
#ifndef PORTADOOM_H
#define PORTADOOM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define PD_VERSION_MAJOR 0
#define PD_VERSION_MINOR 1

#define PD_TICRATE    35        /* game tics per second */
#define PD_SCREEN_W   320
#define PD_SCREEN_H   200

/* ======================================================================
 * Setup
 * ==================================================================== */

typedef struct
{
    void *user;

    /* Text output from the engine, one line at a time. */
    void (*log)(void *user, const char *line);

    /* Unrecoverable error. Must not return (longjmp, throw, exit...).
     * If NULL the engine traps. */
    void (*fatal)(void *user, const char *message);

    /* Optional persistent storage for savegames and the config file.
     * file_read: on success set *data and *len (engine copies it) and return 0.
     * The data pointer only needs to stay valid until the call returns. */
    int (*file_read)(void *user, const char *name, void **data, size_t *len);
    int (*file_write)(void *user, const char *name, const void *data, size_t len);
    int (*file_remove)(void *user, const char *name);
} pd_host_t;

typedef struct
{
    /* Memory for the whole engine. pd_recommended_heap_size() is plenty
     * for any vanilla map. Must stay valid until the process ends. */
    void *heap;
    size_t heap_size;

    /* The IWAD (e.g. DOOM.WAD, DOOM2.WAD, freedoom1.wad) in memory. It is
     * used in place, never copied or modified. Keep it alive. */
    const char *iwad_name;
    const void *iwad_data;
    size_t iwad_size;

    /* Optional extra Doom command-line arguments, e.g.
     * { "-skill", "4", "-warp", "1", "1" }. */
    int argc;
    const char *const *argv;

    /* 1 = run Doom's software renderer every tick (pd_framebuffer).
     * 0 = skip it; the host draws the world itself. Gameplay is identical. */
    int render;
} pd_config_t;

size_t pd_recommended_heap_size(void);

/* Make another WAD (or any file) visible to the engine before pd_init,
 * e.g. a PWAD to load with argv { "-file", "mymap.wad" }. No copy. */
int pd_add_file(const char *name, const void *data, size_t len);

/* Start Doom. Returns 0 on success. */
int pd_init(const pd_config_t *config, const pd_host_t *host);

/* Advance the game by exactly one tic (1/35 s). Call 35 times a second.
 * Returns 1 once the player has quit from the menu, else 0. */
int pd_tick(void);

/* Game tics run since start. */
int pd_gametic(void);

/* Turn Doom's software renderer on/off at any time (see pd_config_t). */
void pd_set_render(int on);

/* ======================================================================
 * Input
 * ==================================================================== */

/* Keyboard keys use Doom's codes: printable ASCII in lowercase for
 * letters, plus these. */
enum
{
    PD_KEY_RIGHTARROW = 0xae,
    PD_KEY_LEFTARROW  = 0xac,
    PD_KEY_UPARROW    = 0xad,
    PD_KEY_DOWNARROW  = 0xaf,
    PD_KEY_ESCAPE     = 27,
    PD_KEY_ENTER      = 13,
    PD_KEY_TAB        = 9,
    PD_KEY_BACKSPACE  = 0x7f,
    PD_KEY_PAUSE      = 0xff,
    PD_KEY_F1         = 0x80 + 0x3b,
    PD_KEY_F2, PD_KEY_F3, PD_KEY_F4, PD_KEY_F5, PD_KEY_F6,
    PD_KEY_F7, PD_KEY_F8, PD_KEY_F9, PD_KEY_F10,
    PD_KEY_F11        = 0x80 + 0x57,
    PD_KEY_F12        = 0x80 + 0x58,
    PD_KEY_RSHIFT     = 0x80 + 0x36,
    PD_KEY_RCTRL      = 0x80 + 0x1d,
    PD_KEY_RALT       = 0x80 + 0x38,
    PD_KEY_USE        = 0xa2,   /* default "use" binding */
    PD_KEY_FIRE       = 0xa3,   /* default "fire" binding */
};

/* Raw key/mouse events, exactly like a keyboard and mouse plugged into
 * Doom. These drive the menus too. */
void pd_key(int pressed, int key);
void pd_mouse(int buttons, int dx, int dy);   /* buttons: bit0 L, bit1 R, bit2 M */

/* Direct control of the player for native ports. The values are held
 * (like keys) until the next call, and add to any keyboard movement.
 * turn and weapon are applied once. Menus still use pd_key. */
enum
{
    PD_BTN_FIRE = 1,
    PD_BTN_USE  = 2,
};

typedef struct
{
    int forward;        /* -50..50  (walk 25, run 50) */
    int side;           /* -40..40  (walk 24, run 40), + = right */
    float turn;         /* degrees to turn this tic, + = left (CCW) */
    int buttons;        /* PD_BTN_* */
    int weapon;         /* 0 = no change, 1..7 = weapon slot */
} pd_input_t;

void pd_set_input(const pd_input_t *in);

/* Set the player's facing directly (degrees). Applied next tic. Handy when
 * the host owns the camera (mouse look in Minecraft, Source...). */
void pd_set_player_angle(float degrees);

/* ======================================================================
 * Doom's own picture and sound
 * ==================================================================== */

/* 320x200 bytes, one palette index per pixel. Updated by pd_tick when
 * render=1. */
const uint8_t *pd_framebuffer(void);

/* 256 RGB triples for the framebuffer, including damage/pickup tint. */
const uint8_t *pd_palette(void);

/* Convenience: framebuffer as 0xAARRGGBB, 320*200 entries. */
void pd_framebuffer_rgba(uint32_t *out);

/* Mix Doom's sound effects (and music, once synthesised) into interleaved
 * stereo 16-bit at the given rate. Call with however many frames the
 * audio device wants. Hosts with their own audio can instead use
 * PD_EV_SOUND events and pd_res_sound(). */
void pd_audio_set_rate(int sample_rate);
void pd_audio_render(int16_t *out, int frames);

/* ======================================================================
 * World: everything a native renderer needs.
 * ==================================================================== */

enum
{
    PD_STATE_LEVEL = 0,
    PD_STATE_INTERMISSION,
    PD_STATE_FINALE,
    PD_STATE_DEMOSCREEN,
};

typedef struct
{
    int state;          /* PD_STATE_* */
    int episode, map;   /* map is 1-based; episode is 1 for Doom II */
    char map_name[9];   /* "E1M1" or "MAP01" */
    int skill;          /* 0..4 */
    int menu_active, paused, automap_active, demo_playback;
    int level_time;     /* tics since level start */
    int kills, total_kills, items, total_items, secrets, total_secrets;
    int map_serial;     /* changes every time a level loads */
    char sky_texture[9];/* wall texture used for the sky ("SKY1") */
} pd_game_t;

void pd_world_game(pd_game_t *out);

typedef struct
{
    float x, y;
} pd_vertex_t;

enum
{
    PD_LINE_BLOCKING      = 1,
    PD_LINE_BLOCKMONSTERS = 2,
    PD_LINE_TWOSIDED      = 4,
    PD_LINE_UPPER_UNPEGGED = 8,
    PD_LINE_LOWER_UNPEGGED = 16,
    PD_LINE_SECRET        = 32,
    PD_LINE_BLOCKSOUND    = 64,
    PD_LINE_NOT_ON_MAP    = 128,
    PD_LINE_MAPPED        = 256,   /* seen on the automap */
};

typedef struct
{
    int v1, v2;             /* vertex indices */
    int flags;              /* PD_LINE_* */
    int special, tag;
    int front_side;         /* side index, -1 if none */
    int back_side;
    int front_sector;       /* sector index, -1 if none */
    int back_sector;
} pd_line_t;

typedef struct
{
    float x_offset, y_offset;     /* texture offsets (scroll live) */
    char top[9], middle[9], bottom[9];  /* texture names, "-" = none */
    int sector;
} pd_side_t;

typedef struct
{
    float floor_height, ceiling_height;
    char floor_flat[9], ceiling_flat[9];  /* "F_SKY1" ceiling = sky */
    int light;              /* 0..255 */
    int special, tag;
} pd_sector_t;

typedef struct
{
    int num_vertices, num_lines, num_sides, num_sectors, num_subsectors;
} pd_map_counts_t;

void pd_world_counts(pd_map_counts_t *out);

/* Copy map data into arrays sized from pd_world_counts. Sector heights,
 * light, flats and side texture names/offsets change during play (doors,
 * lifts, switches), so re-read them each tick or follow PD_EV_SECTOR. */
void pd_world_vertices(pd_vertex_t *out);
void pd_world_lines(pd_line_t *out);
void pd_world_sides(pd_side_t *out);
void pd_world_sectors(pd_sector_t *out);
void pd_world_sector(int index, pd_sector_t *out);
void pd_world_side(int index, pd_side_t *out);

/* Sector index at a map point (works for any x,y; uses the BSP). Great
 * for voxel hosts: sample a grid and build columns from floor/ceiling. */
int pd_world_point_sector(float x, float y);

/* Convex floor polygons: one per subsector, built from the BSP, together
 * they tile every sector exactly. Triangulate as a fan for floors and
 * ceilings. pd_world_subsector_poly fills up to max points, returns the
 * point count; sector index goes to *sector. */
int pd_world_subsector_poly(int index, pd_vertex_t *out, int max, int *sector);

typedef struct
{
    uint32_t id;            /* unique for the life of the engine, never reused */
    int type;               /* internal mobj type (see pd_mobj_type_name) */
    int doomednum;          /* map editor number (e.g. 3001 = imp), -1 if none */
    float x, y, z;          /* feet position */
    float angle;            /* degrees */
    float radius, height;
    float momx, momy, momz; /* units per tic */
    int health;
    int flags;              /* PD_MOBJ_* */
    char sprite[5];         /* e.g. "TROO" */
    int frame;              /* 0 = 'A' */
    int fullbright;
    int rotations;          /* 1 = same from all sides, 8 = rotated */
    uint32_t target_id;     /* who it is attacking / who fired it, 0 = none */
} pd_mobj_t;

enum
{
    PD_MOBJ_SOLID      = 1,
    PD_MOBJ_SHOOTABLE  = 2,
    PD_MOBJ_MISSILE    = 4,
    PD_MOBJ_CORPSE     = 8,
    PD_MOBJ_PICKUP     = 16,   /* item the player can pick up */
    PD_MOBJ_COUNTKILL  = 32,   /* monster */
    PD_MOBJ_SHADOW     = 64,   /* spectre fuzz */
    PD_MOBJ_PLAYER     = 128,
    PD_MOBJ_NOSECTOR   = 256,  /* invisible (e.g. teleport target) */
    PD_MOBJ_FLOAT      = 512,
};

/* Number of live map objects, then copy up to max into out. */
int pd_world_mobj_count(void);
int pd_world_mobjs(pd_mobj_t *out, int max);
int pd_world_mobj(uint32_t id, pd_mobj_t *out);    /* 1 if found */
const char *pd_mobj_type_name(int type);          /* "MT_TROOP" */

typedef struct
{
    uint32_t mobj_id;
    float x, y, z;          /* feet */
    float view_z;           /* eye height incl. bob */
    float angle;            /* degrees */
    int health, armor, armor_type;
    int ready_weapon;       /* 0 fist .. 8 super shotgun */
    int pending_weapon;     /* -1 none */
    int owned_weapons[9];
    int ammo[4], max_ammo[4];  /* bullets, shells, cells, rockets */
    int cards[6];           /* blue, yellow, red card; blue, yellow, red skull */
    int powers[6];          /* tics left: invuln, berserk, invis, suit, map, goggles */
    int damage_count;       /* > 0: red flash strength */
    int bonus_count;        /* > 0: gold flash strength */
    int extra_light;        /* gun flash lighting */
    int dead;
    int fixed_colormap;     /* goggles/invuln */
    /* weapon sprite (psprite) for a first-person gun overlay */
    char weapon_sprite[5];
    int weapon_frame;
    float weapon_sx, weapon_sy;
    int weapon_fullbright;
    char flash_sprite[5];   /* muzzle flash, "" if none */
    int flash_frame;
    const char *message;    /* HUD pickup message, NULL if none */
} pd_player_t;

void pd_world_player(pd_player_t *out);

/* ---- Events: things that happened during the last pd_tick ------------ */

enum
{
    PD_EV_NONE = 0,
    PD_EV_LEVEL_START,      /* new map loaded: rebuild everything */
    PD_EV_LEVEL_END,
    PD_EV_MOBJ_SPAWN,       /* id */
    PD_EV_MOBJ_REMOVE,      /* id */
    PD_EV_SECTOR,           /* index: height, light or flat changed */
    PD_EV_SIDE,             /* index: texture changed (switches) */
    PD_EV_SOUND,            /* name, id (0 = global), x, y, volume */
    PD_EV_MUSIC,            /* name ("D_E1M1"), index = 1 if looping */
    PD_EV_MUSIC_STOP,
    PD_EV_PLAYER_HURT,      /* index = damage */
    PD_EV_PLAYER_PICKUP,
};

typedef struct
{
    int type;               /* PD_EV_* */
    uint32_t id;
    int index;
    char name[9];
    float x, y, z;
    int volume;             /* 0..127 */
} pd_event_t;

/* Events from the most recent tick. Pointer valid until the next tick. */
const pd_event_t *pd_world_events(int *count);

/* ---- Resources: graphics and sounds from the WADs --------------------- */

/* Pixel data comes as palette indices; 0xFFFF in a 16-bit image means
 * transparent. Use pd_palette_base() for colours (no damage tint). */
const uint8_t *pd_palette_base(void);

/* Wall texture by name ("STARTAN3"). Fills w/h, writes w*h indices into
 * out (row-major, top to bottom) if out != NULL and cap is enough.
 * Transparent pixels (masked mid-textures) are written as 0xFFFF.
 * Returns 1 if found. */
int pd_res_texture(const char *name, int *w, int *h, uint16_t *out, size_t cap);

/* Floor/ceiling flat ("FLOOR4_8"), always 64x64, 4096 bytes. Animated
 * flats/textures: the name in sectors/sides is the base name; ask
 * pd_res_flat_current / pd_res_texture_current for the live frame. */
int pd_res_flat(const char *name, uint8_t *out);
const char *pd_res_flat_current(const char *name);
const char *pd_res_texture_current(const char *name);

/* Sprite graphic. rotation 0 = facing the viewer for 1-rotation sprites,
 * else 1..8 like Doom (1 = front). *flip says to mirror horizontally.
 * Offsets put the image relative to the thing's feet (Doom convention). */
int pd_res_sprite(const char *sprite, int frame, int rotation,
                  int *w, int *h, int *left_offset, int *top_offset,
                  int *flip, uint16_t *out, size_t cap);

/* Any patch lump by name (status bar pieces, menu graphics...). */
int pd_res_patch(const char *lump, int *w, int *h, int *left_offset,
                 int *top_offset, uint16_t *out, size_t cap);

/* Sound effect by short name ("pistol"): unsigned 8-bit mono PCM.
 * Returns pointer into WAD memory, NULL if missing. */
const uint8_t *pd_res_sound(const char *name, int *samples, int *rate);

/* Music lump ("D_E1M1") converted to a standard MIDI file. Returns the
 * size; call with out=NULL to get the size first. */
size_t pd_res_music_midi(const char *name, uint8_t *out, size_t cap);

/* Raw lump access for anything else. */
const void *pd_res_lump(const char *name, size_t *len);

#ifdef __cplusplus
}
#endif

#endif /* PORTADOOM_H */
