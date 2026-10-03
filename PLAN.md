# PortaDoom: implementation plan

## Status (2026-10-03)
- [x] Phase 1: freestanding libc. The library has zero external symbols (`ld -r` + `nm -u`).
- [x] Phase 2: platform layer (`src/pd_platform.c`): virtual clock, framebuffer, key queue, SFX mixer.
- [x] Phase 3: tick-driven loop, non-blocking melt, engine hooks for world events.
- [x] Phase 4: public API (`include/portadoom.h`).
- [x] Phase 6 (core): world API with map, BSP floor polygons, things, player, events, textures, sprites, sounds, MIDI.
- [x] Headless host. Rendering on and off give identical game state; ~500x realtime with no rendering.
- [ ] WASM build (needs wasm-ld: lld or zig)
- [ ] X11 playable host
- [ ] OPL music synth
- [ ] Skill reference docs and adapter recipes

**Priority: native rendering in the host engine ("doomified").** Doom's own framebuffer is mainly for testing.

The goal is the full Doom engine (all game logic, menus, intermissions, saves, demos, sound and music) as a freestanding C library. A host can show Doom's own software-rendered frames, or skip them and render the live world state natively.

Base: doomgeneric (GPL-2), copied into `engine/`. Test data: Freedoom (`wads/`, not distributed). No id assets are shipped.

## Phase 1: Freestanding libc shim (`libc/`)
- Headers the engine includes (`stdio`, `stdlib`, `string`, `ctype`, `math`…) with our own `stdint`/`stddef`/`stdarg`/`limits` built from compiler builtins, so no system headers are needed.
- `pd_libc.c` provides: mem/str functions, ctype, `vsnprintf` family, the small `sscanf` subset the engine uses, malloc/free/realloc over one arena, and `sin`/`tan`/`atan`/`fabs`.
- `FILE*` is backed by a virtual filesystem. WADs come from host memory with zero copies. Saves and config live in memory and are passed to the host through `file_read`/`file_write` callbacks.
- Every symbol gets a `pd_` prefix through macros, so the engine links into any host without clashing with its libc.

## Phase 2: Platform layer (`src/`), replacing the `i_*` files
- `i_system`: `I_Error` calls the host's `fatal` callback; the zone allocator comes from the arena.
- `i_timer`: a virtual clock that counts tics. It never sleeps and never reads wall time.
- `i_video`: a 320×200 indexed framebuffer plus the palette, including damage and pickup flashes.
- `i_input`: a key/mouse event queue that the host fills.
- `pd_sound`: a built-in SFX mixer that outputs stereo s16 at any sample rate. Music is a MUS→MIDI event stream for now; an OPL synth comes in Phase 7.
- A memory WAD file class. The IWAD is identified by its lumps, not its filename.

## Phase 3: Minimal engine edits
- Deterministic stepping: one host `tick()` runs exactly one game tic (`singletics`).
- The screen melt becomes non-blocking, one step per tick. It currently busy-waits.
- Strip OS `#include`s and filesystem calls (`d_iwad`, `m_misc`, `m_config`, `v_video` PNG).
- Rendering can be turned off: the playsim runs without `D_Display`.

## Phase 4: Public API (`include/portadoom.h`)
```c
pd_init(&config, &host);      // WAD buffers, args, sample rate, render on/off
pd_post_event(&ev);           // key up/down, mouse delta/buttons
pd_tick();                    // advance 1/35 s
pd_framebuffer(); pd_palette();
pd_audio_render(buf, frames);
pd_world_*();                 // Phase 6
```
Only one instance per process. For several instances, run one WASM instance each.

## Phase 5: Prove portability with three different hosts
1. `hosts/headless`: runs demos and dumps PPM frames. A per-tic frame hash is the determinism test.
2. `hosts/x11`: a playable desktop window with keyboard and mouse.
3. `hosts/web`: the same core built as one `.wasm` with no Emscripten, plus a small JS host. This needs `wasm-ld`, which isn't installed yet (lld or `zig cc`).

**Acceptance:** Freedoom plays start to finish, and a demo produces identical frame hashes on all three hosts.

## Phase 6: World layer (Tier 2 ports)
- Static map: vertices, linedefs, sidedefs, sectors (floor/ceiling heights, flats, light), textures, and the flat and patch graphics turned into RGBA.
- Live state, read every tick: player (position, angle, health, ammo, weapon sprite), mobjs (type, position, state, sprite frame, rotation), moving sector heights, and sound events with positions.
- Change events, so hosts can update incrementally (for example, a door moved, so update those Minecraft blocks).

## Phase 7: Music synthesis
- Port Chocolate Doom's OPL emulator and GENMIDI player, so music works on any host with no MIDI device.

## Phase 8: The Claude skill
- `SKILL.md`: the engine contract, how to choose a tier, and the build matrix (native static lib, WASM).
- Adapter recipes: **Tier 1** framebuffer as a texture; **Tier 2** world state mapped to native geometry and entities; **Tier 3** offline map conversion (BSP→Source brushes, map→Minecraft schematic).
- Reference adapters: browser, Minecraft (Java via Chicory WASM), Source (C++ plugin linking the static lib), Godot/Unity.
- Guardrails: never bundle IWADs; the user supplies the WAD.
