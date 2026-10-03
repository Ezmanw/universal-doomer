/*
 * PortaDoom internals shared between the platform layer, the API and the
 * small hooks patched into the engine. Not part of the public API.
 */
#ifndef PD_INTERNAL_H
#define PD_INTERNAL_H

#include "doomtype.h"
#include "d_ticcmd.h"

struct mobj_s;
struct player_s;

/* virtual clock (tics the host has run) */
extern int pd_clock_tics;

/* set by I_Quit */
extern int pd_quit_requested;

/* current palette with gamma, for the framebuffer */
extern byte pd_cur_palette[256 * 3];

/* keyboard queue read by i_input.c */
void pd_queue_key(int pressed, int key);

/* mixer */
void pd_mixer_set_rate(int rate);
void pd_mixer_render(int16_t *out, int frames);
int pd_music_sample_l(int frame);
int pd_music_sample_r(int frame);

/* engine hooks (see pd_world.c / pd_api.c) */
void pd_hook_mobj_remove(struct mobj_s *mo);
void pd_hook_sound(void *origin, int sfx_id);
void pd_hook_music(const char *lumpname, int lumpnum, int looping);
void pd_hook_music_stop(void);
void pd_hook_ticcmd(ticcmd_t *cmd);
void pd_hook_level_start(void);
void pd_hook_level_end(void);
void pd_hook_pickup(struct player_s *player, struct mobj_s *item);
void pd_hook_player_hurt(struct player_s *player, int damage);

/* world module */
void pd_world_begin_tick(void);
void pd_world_end_tick(void);

/* engine entry points (d_main.c) */
void D_DoomMain(void);
void D_PortaDoomTick(boolean render);

#endif
