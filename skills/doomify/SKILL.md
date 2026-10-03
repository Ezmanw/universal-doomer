---
name: doomify
description: Port the full Doom game into any game or engine (Minecraft, Source, Godot, Unity, Unreal...) using the Universal Doomer engine, with the host engine drawing everything natively. Use when the user wants to "doomify" a game, put Doom in an engine, or play a WAD inside another game.
---

# Doomify

Make Doom playable inside the user's chosen game or engine. The Universal Doomer
engine runs the real Doom game logic. The host engine draws the world, monsters,
weapons and HUD with its own renderer.

## Get the engine

The engine is the repository this skill came from:
<https://github.com/Ezmanw/universal-doomer>

- If `~/.local/share/universal-doomer` exists (created by `install.sh`), use it.
- Otherwise clone it: `git clone https://github.com/Ezmanw/universal-doomer`
- Build: `make` (gives `build/native/libportadoom.a`). It needs only a C compiler.
- The whole API is in `include/portadoom.h`. Read it before writing any code.
- `hosts/headless/main.c` is a complete, small example host.

## The rule: identical by default

The result must play **exactly like Doom** unless the user says otherwise:
same maps, monsters and their behaviour, weapons, damage, speeds, doors, lifts,
secrets, pickups, sounds and music, at 35 tics per second.

- Doom decides everything that happens. The host only draws it and sends input.
- Never re-implement Doom gameplay in the host. Read it from the engine.
- Only change behaviour when the user asks (e.g. "let me sprint", "make imps bigger").

## Before building, ask the user

Ask these in one short message, offering the default for each:

1. **Which game/engine?** (if not already clear)
2. **How should it look?**
   - *Faithful* (default): Doom's own textures and sprites, mapped onto host geometry.
   - *Native style*: the host's own look (e.g. Minecraft blocks and mobs that match
     each Doom texture and monster).
3. **Anything that should differ from Doom?** (default: nothing)
4. **Which WAD?** They supply their own (DOOM.WAD, DOOM2.WAD, or the free Freedoom).
   Never download or bundle commercial id Software WADs.

## How to build it

1. Compile the engine for the user's own machine and link it into the host:
   - C/C++ engines (Source, Unreal, Godot GDExtension): link `libportadoom.a`.
   - Java (Minecraft mods): build a shared library and call it through JNI or
     the Foreign Function API. Bundle it inside the `.jar`.
   - C# (Unity): build a shared library and use P/Invoke.
2. Start: fill `pd_config_t` (heap, WAD bytes, `render = 0`) and call `pd_init`.
3. Every 1/35 s: send input (`pd_set_input`, `pd_set_player_angle`, `pd_key` for
   menus), call `pd_tick()`, then update the host scene from `pd_world_events()`.
4. On `PD_EV_LEVEL_START`, clear the scene and build the level:
   - floors and ceilings: `pd_world_subsector_poly` (convex polygons, fan-triangulate)
     at each sector's `floor_height` / `ceiling_height`
   - walls: `pd_world_lines` + `pd_world_sides` (upper / middle / lower parts)
   - textures: `pd_res_texture`, `pd_res_flat`, colours from `pd_palette_base`
   - voxel hosts (Minecraft): sample `pd_world_point_sector` on a grid
5. Keep it in sync:
   - `PD_EV_SECTOR` / `PD_EV_SIDE`: doors, lifts, lights and switches changed
   - `PD_EV_MOBJ_SPAWN` / `PD_EV_MOBJ_REMOVE` + `pd_world_mobjs` each tick: monsters,
     items and projectiles (sprite, frame, angle; `pd_res_sprite` for images)
   - `pd_world_player`: camera position (`view_z` = eye height), health, ammo,
     weapon sprite for the first-person gun
6. Sound: play `PD_EV_SOUND` at its position using `pd_res_sound`, or mix with
   `pd_audio_render`. Music: `PD_EV_MUSIC` + `pd_res_music_midi`.

## Check it is identical

- Run the same session with `pd_set_render(1)` and compare the host's view with
  Doom's own `pd_framebuffer()`.
- Play a demo (`-playdemo demo1`): the host must show the same thing, tic for tic.
- Walk E1M1/MAP01 to the exit: doors, lifts, switches, secrets, pickups.
