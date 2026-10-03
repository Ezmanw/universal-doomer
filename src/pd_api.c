/*
 * PortaDoom public API: start-up, ticking, input, framebuffer and the
 * in-memory WAD file class.
 */
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_loop.h"
#include "d_player.h"
#include "d_ticcmd.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_fixed.h"
#include "tables.h"
#include "w_file.h"
#include "w_wad.h"
#include "z_zone.h"

#include "pd_internal.h"
#include "portadoom.h"

int pd_clock_tics;
int pd_quit_requested;

static boolean started;
static boolean render_enabled = true;

/* ======================================================================
 * WAD files straight from host memory (no copy, no file I/O)
 * ==================================================================== */

static wad_file_class_t memory_wad_file;

wad_file_t *W_OpenFile(char *path)
{
    const void *data;
    size_t len;
    wad_file_t *f;

    if (pd_vfs_get(path, &data, &len) != 0)
        return NULL;

    f = Z_Malloc(sizeof(*f), PU_STATIC, 0);
    f->file_class = &memory_wad_file;
    f->mapped = (byte *)data;
    f->length = (unsigned int)len;
    return f;
}

void W_CloseFile(wad_file_t *wad)
{
    Z_Free(wad);
}

size_t W_Read(wad_file_t *wad, unsigned int offset, void *buffer,
              size_t buffer_len)
{
    if (offset >= wad->length)
        return 0;
    if (buffer_len > wad->length - offset)
        buffer_len = wad->length - offset;
    memcpy(buffer, wad->mapped + offset, buffer_len);
    return buffer_len;
}

static wad_file_class_t memory_wad_file =
{
    W_OpenFile,
    W_CloseFile,
    W_Read,
};

/* ======================================================================
 * Start-up
 * ==================================================================== */

/* Doom identifies the game from the IWAD file name. If the host's name is
 * not one Doom knows, work it out from the lumps inside. */
static const char *known_iwads[] =
{
    "doom2.wad", "plutonia.wad", "tnt.wad", "doom.wad", "doom1.wad",
    "chex.wad", "hacx.wad", "freedm.wad", "freedoom2.wad", "freedoom1.wad",
};

static int wad_has_lump(const uint8_t *wad, size_t len, const char *name)
{
    uint32_t n, dir, i;

    if (len < 12 || memcmp(wad + 1, "WAD", 3) != 0)
        return 0;
    n = wad[4] | (wad[5] << 8) | (wad[6] << 16) | ((uint32_t)wad[7] << 24);
    dir = wad[8] | (wad[9] << 8) | (wad[10] << 16) | ((uint32_t)wad[11] << 24);
    if (dir > len || n > (len - dir) / 16)
        return 0;
    for (i = 0; i < n; i++)
        if (!strncasecmp((const char *)wad + dir + i * 16 + 8, name, 8))
            return 1;
    return 0;
}

static const char *iwad_canonical_name(const char *given, const uint8_t *wad,
                                       size_t len)
{
    const char *base = given ? given : "";
    const char *slash = strrchr(base, '/');
    size_t i;

    if (slash)
        base = slash + 1;
    for (i = 0; i < arrlen(known_iwads); i++)
        if (!strcasecmp(base, known_iwads[i]))
            return known_iwads[i];

    if (wad_has_lump(wad, len, "MAP01"))
    {
        if (wad_has_lump(wad, len, "CAMO1"))
            return "plutonia.wad";
        if (wad_has_lump(wad, len, "REDTNT2"))
            return "tnt.wad";
        return "doom2.wad";
    }
    if (wad_has_lump(wad, len, "E2M1"))
        return "doom.wad";
    return "doom1.wad";
}

size_t pd_recommended_heap_size(void)
{
    /* 16 MiB zone + config, savegame buffers, strings */
    return 32u * 1024 * 1024;
}

int pd_add_file(const char *name, const void *data, size_t len)
{
    return pd_vfs_add(name, data, len);
}

#define MAX_ARGS 64

static char *argv_store[MAX_ARGS + 1];

int pd_init(const pd_config_t *config, const pd_host_t *host)
{
    pd_libc_hooks_t hooks;
    const char *iwad;
    int i, argc = 0;

    if (started || !config || !config->heap || !config->iwad_data)
        return -1;

    pd_libc_heap(config->heap, config->heap_size);

    memset(&hooks, 0, sizeof(hooks));
    if (host)
    {
        hooks.user = host->user;
        hooks.log = host->log;
        hooks.fatal = host->fatal;
        hooks.file_read = host->file_read;
        hooks.file_write = host->file_write;
        hooks.file_remove = host->file_remove;
    }
    pd_libc_set_hooks(&hooks);

    iwad = iwad_canonical_name(config->iwad_name, config->iwad_data,
                               config->iwad_size);
    if (pd_vfs_add(iwad, config->iwad_data, config->iwad_size) != 0)
        return -1;

    argv_store[argc++] = "portadoom";
    argv_store[argc++] = "-iwad";
    argv_store[argc++] = (char *)iwad;
    for (i = 0; i < config->argc && argc < MAX_ARGS; i++)
        argv_store[argc++] = (char *)config->argv[i];
    argv_store[argc] = NULL;
    myargc = argc;
    myargv = argv_store;

    render_enabled = config->render != 0;

    /* one game tic per pd_tick, never more, never waiting */
    singletics = true;

    started = true;
    D_DoomMain();
    return 0;
}

int pd_tick(void)
{
    if (!started)
        return 1;
    pd_world_begin_tick();
    pd_clock_tics++;
    D_PortaDoomTick(render_enabled);
    pd_world_end_tick();
    return pd_quit_requested;
}

int pd_gametic(void)
{
    return gametic;
}

void pd_set_render(int on)
{
    render_enabled = on != 0;
}

/* ======================================================================
 * Input
 * ==================================================================== */

void pd_key(int pressed, int key)
{
    pd_queue_key(pressed, key);
}

void pd_mouse(int buttons, int dx, int dy)
{
    event_t ev;
    ev.type = ev_mouse;
    ev.data1 = buttons;
    ev.data2 = dx;
    ev.data3 = dy;
    ev.data4 = 0;
    D_PostEvent(&ev);
}

static pd_input_t host_input;
static boolean host_input_set;
static float host_turn;             /* degrees still to apply */
static boolean host_angle_set;
static float host_angle;

void pd_set_input(const pd_input_t *in)
{
    host_input = *in;
    host_input_set = true;
    host_turn += in->turn;
}

void pd_set_player_angle(float degrees)
{
    host_angle = degrees;
    host_angle_set = true;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Called at the end of G_BuildTiccmd, before demo/pause specials. */
void pd_hook_ticcmd(ticcmd_t *cmd)
{
    int turn = 0;

    if (host_angle_set)
    {
        player_t *p = &players[consoleplayer];
        if (p->mo)
        {
            /* angle_t: 2^32 = 360 degrees; angleturn is the top 16 bits */
            int desired = (int)(host_angle * 65536.0f / 360.0f) & 0xffff;
            int current = (int)(p->mo->angle >> 16);
            turn = (int16_t)(desired - current);
        }
        host_angle_set = false;
    }

    if (host_turn != 0)
    {
        turn += (int)(host_turn * 65536.0f / 360.0f);
        host_turn = 0;
    }
    cmd->angleturn = (short)(cmd->angleturn + turn);

    if (!host_input_set)
        return;

    cmd->forwardmove = (signed char)clampi(cmd->forwardmove
                                           + host_input.forward, -50, 50);
    cmd->sidemove = (signed char)clampi(cmd->sidemove
                                        + host_input.side, -40, 40);
    if (host_input.buttons & PD_BTN_FIRE)
        cmd->buttons |= BT_ATTACK;
    if (host_input.buttons & PD_BTN_USE)
        cmd->buttons |= BT_USE;
    if (host_input.weapon >= 1 && host_input.weapon <= 7)
    {
        cmd->buttons &= ~BT_WEAPONMASK;
        cmd->buttons |= BT_CHANGE
                     | ((host_input.weapon - 1) << BT_WEAPONSHIFT);
        host_input.weapon = 0;      /* weapon change is a one-shot */
    }
}

/* ======================================================================
 * Picture and sound
 * ==================================================================== */

const uint8_t *pd_framebuffer(void)
{
    return I_VideoBuffer;
}

const uint8_t *pd_palette(void)
{
    return pd_cur_palette;
}

void pd_framebuffer_rgba(uint32_t *out)
{
    int i;
    if (!I_VideoBuffer)
        return;
    for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
    {
        const byte *c = &pd_cur_palette[I_VideoBuffer[i] * 3];
        out[i] = 0xff000000u | (c[0] << 16) | (c[1] << 8) | c[2];
    }
}

void pd_audio_set_rate(int sample_rate)
{
    pd_mixer_set_rate(sample_rate);
}

void pd_audio_render(int16_t *out, int frames)
{
    pd_mixer_render(out, frames);
}
