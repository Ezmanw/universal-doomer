#!/bin/sh
# Universal Doomer installer: downloads the engine and adds the "doomify"
# skill to every AI coding agent found on this machine (Claude Code,
# Gemini CLI, Codex CLI).
#
#   curl -fsSL https://raw.githubusercontent.com/Ezmanw/universal-doomer/main/install.sh | sh
set -e

REPO="https://github.com/Ezmanw/universal-doomer"
DEST="${UNIVERSAL_DOOMER_HOME:-$HOME/.local/share/universal-doomer}"

if [ -d "$DEST/.git" ]; then
    echo "Updating $DEST"
    git -C "$DEST" pull --ff-only
else
    echo "Downloading engine to $DEST"
    mkdir -p "$(dirname "$DEST")"
    git clone --depth 1 "$REPO" "$DEST"
fi

installed=0
for agent in .claude .gemini .codex .agents; do
    if [ -d "$HOME/$agent" ] || [ "$agent" = ".claude" ]; then
        mkdir -p "$HOME/$agent/skills"
        rm -rf "$HOME/$agent/skills/doomify"
        ln -s "$DEST/skills/doomify" "$HOME/$agent/skills/doomify"
        echo "Added doomify skill to ~/$agent/skills"
        installed=1
    fi
done

[ "$installed" = 1 ] && echo "Done. Restart your agent and ask it to doomify a game."
