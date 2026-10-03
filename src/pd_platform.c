/*
 * PortaDoom platform layer: everything Doom's i_* interfaces expect from
 * an operating system, implemented with no OS at all.
 *
 *   system  - errors go to the host, zone memory comes from the heap
 *   timer   - a virtual clock that only moves when the host calls pd_tick
 *   video   - an in-memory 320x200 indexed framebuffer + palette
 *   input   - a key queue the host fills through pd_key
 *   sound   - a software mixer the host pulls from with pd_audio_render
 */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "doomtype.h"
#include "doomkeys.h"
#include "d_event.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "i_sound.h"
#include "i_joystick.h"
#include "i_endoom.h"
#include "i_swap.h"
#include "m_argv.h"
#include "m_misc.h"
#include "v_video.h"
#include "w_wad.h"
#include "tables.h"
#include "z_zone.h"
#include "deh_str.h"

#include "pd_internal.h"

/* ======================================================================
 * System
 * ==================================================================== */

#define ZONE_MB 16

typedef struct atexit_entry_s
{
    atexit_func_t func;
    boolean run_on_error;
    struct atexit_entry_s *next;
} atexit_entry_t;

static atexit_entry_t *exit_funcs;

void I_AtExit(atexit_func_t func, boolean run_on_error)
{
    atexit_entry_t *e = malloc(sizeof(*e));
    e->func = func;
    e->run_on_error = run_on_error;
    e->next = exit_funcs;
    exit_funcs = e;
}

void I_Tactile(int on, int off, int total)
{
}

byte *I_ZoneBase(int *size)
{
    int mb = ZONE_MB;
    int p = M_CheckParmWithArgs("-mb", 1);
    byte *mem;

    if (p > 0)
        mb = atoi(myargv[p + 1]);
    *size = mb * 1024 * 1024;
    mem = malloc(*size);
    if (!mem)
        I_Error("Not enough heap for a %d MiB zone", mb);
    return mem;
}

void I_PrintBanner(char *msg)
{
    printf("== %s ==\n", msg);
}

void I_PrintDivider(void)
{
}

void I_PrintStartupBanner(char *gamedescription)
{
    printf("PortaDoom: %s\n", gamedescription);
}

boolean I_ConsoleStdout(void)
{
    return false;
}

void I_Quit(void)
{
    atexit_entry_t *e;
    for (e = exit_funcs; e; e = e->next)
        e->func();
    exit_funcs = NULL;
    pd_quit_requested = 1;
}

void I_Error(char *error, ...)
{
    static boolean already;
    char msg[512];
    va_list ap;
    atexit_entry_t *e;

    va_start(ap, error);
    M_vsnprintf(msg, sizeof(msg), error, ap);
    va_end(ap);

    if (!already)
    {
        already = true;
        for (e = exit_funcs; e; e = e->next)
            if (e->run_on_error)
                e->func();
    }
    pd_fatal(msg);
}

/* Memory contents some vanilla overflow emulation reads. Values from a
 * DOS 6.22 machine, same as Chocolate Doom's default. */
boolean I_GetMemoryValue(unsigned int offset, void *value, int size)
{
    static const unsigned char dump[10] = {
        0x57, 0x92, 0x19, 0x00, 0xF4, 0x06, 0x70, 0x00, 0x16, 0x00 };

    if (offset + size > sizeof(dump))
        return false;
    switch (size)
    {
        case 1:
            *(unsigned char *)value = dump[offset];
            return true;
        case 2:
            *(unsigned short *)value = dump[offset] | (dump[offset + 1] << 8);
            return true;
        case 4:
            *(unsigned int *)value = dump[offset] | (dump[offset + 1] << 8)
                | (dump[offset + 2] << 16) | ((unsigned)dump[offset + 3] << 24);
            return true;
    }
    return false;
}

void I_Endoom(byte *data)
{
}

/* ======================================================================
 * Timer: time is measured in tics the host has asked for, nothing else.
 * ==================================================================== */

int I_GetTime(void)
{
    return pd_clock_tics;
}

int I_GetTimeMS(void)
{
    return pd_clock_tics * 1000 / TICRATE;
}

void I_Sleep(int ms)
{
}

void I_WaitVBL(int count)
{
}

void I_InitTimer(void)
{
}

/* ======================================================================
 * Joystick: the host maps controllers itself (pd_set_input).
 * ==================================================================== */

void I_InitJoystick(void) {}
void I_ShutdownJoystick(void) {}
void I_UpdateJoystick(void) {}
void I_BindJoystickVariables(void) {}

/* ======================================================================
 * Video
 * ==================================================================== */

byte *I_VideoBuffer = NULL;
boolean screensaver_mode = false;
boolean screenvisible = true;
float mouse_acceleration = 2.0;
int mouse_threshold = 10;
int usegamma = 0;
int usemouse = 1;

byte pd_cur_palette[256 * 3];

void I_InitGraphics(void)
{
    I_VideoBuffer = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT);
    screenvisible = true;
}

void I_ShutdownGraphics(void)
{
}

void I_StartFrame(void)
{
}

void I_GetEvent(void);

void I_StartTic(void)
{
    I_GetEvent();
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    /* the host reads I_VideoBuffer whenever it likes */
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette)
{
    int i;
    for (i = 0; i < 256 * 3; i++)
        pd_cur_palette[i] = gammatable[usegamma][palette[i]];
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int i, best = 0, best_diff = 0x7fffffff;
    for (i = 0; i < 256; i++)
    {
        int dr = r - pd_cur_palette[i * 3];
        int dg = g - pd_cur_palette[i * 3 + 1];
        int db = b - pd_cur_palette[i * 3 + 2];
        int diff = dr * dr + dg * dg + db * db;
        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
            if (diff == 0)
                break;
        }
    }
    return best;
}

void I_BeginRead(void) {}
void I_EndRead(void) {}
void I_SetWindowTitle(char *title) {}
void I_GraphicsCheckCommandLine(void) {}
void I_SetGrabMouseCallback(grabmouse_callback_t func) {}
void I_EnableLoadingDisk(void) {}
void I_BindVideoVariables(void) {}
void I_DisplayFPSDots(boolean dots_on) {}
void I_CheckIsScreensaver(void) {}
void I_InitWindowTitle(void) {}
void I_InitWindowIcon(void) {}

/* ======================================================================
 * Input: key queue drained by i_input.c's I_GetEvent via DG_GetKey.
 * ==================================================================== */

#define KEYQUEUE 64

static unsigned short keyqueue[KEYQUEUE];
static int keyhead, keytail;

void pd_queue_key(int pressed, int key)
{
    int next = (keyhead + 1) % KEYQUEUE;
    if (next == keytail)
        return;     /* full: drop */
    keyqueue[keyhead] = (unsigned short)((pressed ? 0x100 : 0) | (key & 0xff));
    keyhead = next;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (keytail == keyhead)
        return 0;
    *pressed = (keyqueue[keytail] & 0x100) != 0;
    *key = keyqueue[keytail] & 0xff;
    keytail = (keytail + 1) % KEYQUEUE;
    return 1;
}

/* ======================================================================
 * Sound effects: an 8-channel software mixer over the WAD's DMX sounds.
 * ==================================================================== */

#define MIX_CHANNELS 16

typedef struct
{
    const byte *data;       /* unsigned 8-bit samples */
    uint32_t length;        /* samples */
    uint32_t pos;           /* 16.16 position */
    uint32_t step;          /* 16.16 step per output frame */
    int left, right;        /* volume 0..255 */
    int rate;
    boolean playing;
} mixchan_t;

static mixchan_t mixchans[MIX_CHANNELS];
static int mix_rate = 44100;
static boolean use_prefix;
static boolean host_pulled_audio;

static void SetChannelVolume(mixchan_t *c, int vol, int sep)
{
    /* vol 0..127, sep 0 (left) .. 254 (right), like Chocolate Doom */
    int left = ((254 - sep) * vol) / 127;
    int right = (sep * vol) / 127;
    c->left = left < 0 ? 0 : left > 255 ? 255 : left;
    c->right = right < 0 ? 0 : right > 255 ? 255 : right;
}

static boolean Snd_Init(boolean prefix)
{
    use_prefix = prefix;
    memset(mixchans, 0, sizeof(mixchans));
    return true;
}

static void Snd_Shutdown(void)
{
}

static int Snd_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    if (sfx->link)
        sfx = sfx->link;
    if (use_prefix)
        M_snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
    else
        M_StringCopy(namebuf, DEH_String(sfx->name), sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

/* If the host does not pull audio (it plays sounds itself from events),
 * still advance the channels in game time so Doom's channel bookkeeping
 * (which sounds are still playing) behaves exactly the same. */
static void Snd_Update(void)
{
    int i;
    if (host_pulled_audio)
    {
        host_pulled_audio = false;
        return;
    }
    for (i = 0; i < MIX_CHANNELS; i++)
    {
        mixchan_t *c = &mixchans[i];
        if (!c->playing)
            continue;
        c->pos += (uint32_t)(((uint64_t)c->rate << 16) / TICRATE);
        if ((c->pos >> 16) >= c->length)
            c->playing = false;
    }
}

static void Snd_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel >= 0 && channel < MIX_CHANNELS)
        SetChannelVolume(&mixchans[channel], vol, sep);
}

static int Snd_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    const byte *data;
    int lumplen, rate;
    uint32_t length;
    mixchan_t *c;

    if (channel < 0 || channel >= MIX_CHANNELS)
        return -1;
    c = &mixchans[channel];
    c->playing = false;

    if (sfx->lumpnum < 0)
        sfx->lumpnum = Snd_GetSfxLumpNum(sfx);
    data = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    lumplen = W_LumpLength(sfx->lumpnum);

    /* DMX header: format 3, rate, length; samples have 16 pad bytes
     * at each end which vanilla did not play. */
    if (lumplen < 8 || data[0] != 3 || data[1] != 0)
        return -1;
    rate = data[2] | (data[3] << 8);
    length = data[4] | (data[5] << 8) | (data[6] << 16)
           | ((uint32_t)data[7] << 24);
    if (length > (uint32_t)lumplen - 8 || length <= 48)
        return -1;

    c->data = data + 8 + 16;
    c->length = length - 32;
    c->rate = rate;
    c->pos = 0;
    c->step = (uint32_t)(((uint64_t)rate << 16) / mix_rate);
    SetChannelVolume(c, vol, sep);
    c->playing = true;
    return channel;
}

static void Snd_StopSound(int channel)
{
    if (channel >= 0 && channel < MIX_CHANNELS)
        mixchans[channel].playing = false;
}

static boolean Snd_SoundIsPlaying(int channel)
{
    if (channel < 0 || channel >= MIX_CHANNELS)
        return false;
    return mixchans[channel].playing;
}

static void Snd_CacheSounds(sfxinfo_t *sounds, int num_sounds)
{
}

static snddevice_t sound_devices[] =
{
    SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module =
{
    sound_devices,
    arrlen(sound_devices),
    Snd_Init,
    Snd_Shutdown,
    Snd_GetSfxLumpNum,
    Snd_Update,
    Snd_UpdateSoundParams,
    Snd_StartSound,
    Snd_StopSound,
    Snd_SoundIsPlaying,
    Snd_CacheSounds,
};

void pd_mixer_set_rate(int rate)
{
    int i;
    if (rate <= 0)
        return;
    for (i = 0; i < MIX_CHANNELS; i++)
        if (mixchans[i].rate)
            mixchans[i].step =
                (uint32_t)(((uint64_t)mixchans[i].rate << 16) / rate);
    mix_rate = rate;
}

void pd_mixer_render(int16_t *out, int frames)
{
    int f, i;

    host_pulled_audio = true;
    for (f = 0; f < frames; f++)
    {
        int l = 0, r = 0;
        for (i = 0; i < MIX_CHANNELS; i++)
        {
            mixchan_t *c = &mixchans[i];
            uint32_t idx;
            int s;
            if (!c->playing)
                continue;
            idx = c->pos >> 16;
            if (idx >= c->length)
            {
                c->playing = false;
                continue;
            }
            s = (int)c->data[idx] - 128;    /* -128..127 */
            l += s * c->left;
            r += s * c->right;
            c->pos += c->step;
        }
        /* s * vol is up to +-32k per channel; scale so 2-3 loud channels
         * don't clip and then clamp */
        l += pd_music_sample_l(f);
        r += pd_music_sample_r(f);
        l = l < -32768 ? -32768 : l > 32767 ? 32767 : l;
        r = r < -32768 ? -32768 : r > 32767 ? 32767 : r;
        out[f * 2] = (int16_t)l;
        out[f * 2 + 1] = (int16_t)r;
    }
}

/* ======================================================================
 * Music: the song is reported to the host as an event (PD_EV_MUSIC) and
 * is available as MIDI via pd_res_music_midi. Built-in synthesis can be
 * plugged in behind pd_music_sample_*.
 * ==================================================================== */

int pd_music_sample_l(int frame) { return 0; }
int pd_music_sample_r(int frame) { return 0; }

static boolean music_playing;

static boolean Mus_Init(void) { return true; }
static void Mus_Shutdown(void) {}
static void Mus_SetMusicVolume(int volume) {}
static void Mus_PauseMusic(void) {}
static void Mus_ResumeMusic(void) {}
static void *Mus_RegisterSong(void *data, int len) { return data; }
static void Mus_UnRegisterSong(void *handle) {}
static void Mus_PlaySong(void *handle, boolean looping) { music_playing = true; }
static void Mus_StopSong(void) { music_playing = false; }
static boolean Mus_MusicIsPlaying(void) { return music_playing; }
static void Mus_Poll(void) {}

music_module_t DG_music_module =
{
    sound_devices,
    arrlen(sound_devices),
    Mus_Init,
    Mus_Shutdown,
    Mus_SetMusicVolume,
    Mus_PauseMusic,
    Mus_ResumeMusic,
    Mus_RegisterSong,
    Mus_UnRegisterSong,
    Mus_PlaySong,
    Mus_StopSong,
    Mus_MusicIsPlaying,
    Mus_Poll,
};
