# Universal Doomer

**Put the real Doom inside any game engine.** Minecraft, Source, Godot, Unity,
Unreal... Doom runs the game and your engine draws it.

Universal Doomer is two things:

1. **A portable Doom engine.** It's the full game (monsters, weapons, doors,
   menus, saves, demos) as a C library with zero dependencies. No OS, no libc,
   no files. It gives your engine a live view of the world: walls, floors,
   monsters, items, the player, sounds and music.
2. **A `doomify` skill** for AI coding agents (Claude Code, Gemini CLI, Codex).
   Ask your agent to "doomify Minecraft" and it builds the port.

No Doom game files are included. Bring your own WAD (`DOOM.WAD`, `DOOM2.WAD`),
or use [Freedoom](https://freedoom.github.io/), which is free.

## Install the skill

**Any agent (Claude Code, Gemini CLI, Codex). This adds the skill to every one it finds:**

```bash
curl -fsSL https://raw.githubusercontent.com/Ezmanw/universal-doomer/main/install.sh | sh
```

**Claude Code (as a plugin):**

```
/plugin marketplace add Ezmanw/universal-doomer
/plugin install universal-doomer@universal-doomer
```

**Manual install (any agent):** clone the repo and copy `skills/doomify` into your
agent's skills folder:

| Agent       | Skills folder        |
|-------------|----------------------|
| Claude Code | `~/.claude/skills/`  |
| Gemini CLI  | `~/.gemini/skills/`  |
| Codex CLI   | `~/.codex/skills/`   |

```bash
git clone https://github.com/Ezmanw/universal-doomer ~/.local/share/universal-doomer
cp -r ~/.local/share/universal-doomer/skills/doomify ~/.claude/skills/
```

Then restart your agent and ask it something like:
*"Doomify Minecraft: make E1M1 out of blocks and imps into mobs."*

## Build the engine yourself

```bash
make                     # build/native/libportadoom.a + a headless test host
build/native/pd_headless freedoom1.wad -tics 700 -shot 500 shot.ppm
```

The only requirement is a C compiler (gcc or clang).

## How a port works

```c
pd_init(&config, &host);           // WAD bytes, memory, render = 0
for (;;) {                         // 35 times a second
    pd_set_input(&input);          // your engine's controls -> Doom
    pd_tick();                     // Doom runs one game tic
    events = pd_world_events(&n);  // doors moved, monsters spawned, sounds...
    pd_world_mobjs(things, max);   // where every monster/item/projectile is
    pd_world_player(&player);      // camera, health, ammo, gun sprite
    // ...draw it all your way
}
```

The full API, with comments, is in [`include/portadoom.h`](include/portadoom.h).
It covers the map (walls, sectors, ready-made convex floor polygons), live things,
the player, events, and the graphics and sounds inside the WAD.

Doom's own 320x200 software renderer is still built in (`render = 1`), which
is handy for testing and for "Doom on a TV inside my game".

## Status

Working:
- The full Doom engine with no dependencies.
- The world API. Gameplay is identical with or without Doom's renderer, verified on a 3,000-tic demo.
- Textures, sprites, sound effects, and music as MIDI.

Not done yet:
- Built-in music synthesis.
- Example ports.

See [PLAN.md](PLAN.md).

## Credits and license

Based on [doomgeneric](https://github.com/ozkl/doomgeneric) by ozkl, which is based on
Chocolate Doom and id Software's Doom source release. Licensed under the
**GNU GPL v2** (see [LICENSE](LICENSE)).

DOOM is a trademark of ZeniMax Media / id Software. This project is not affiliated
with or endorsed by them, and it ships no Doom game data.
