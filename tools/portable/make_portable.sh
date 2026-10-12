#!/usr/bin/env bash
# make_portable.sh: turns on portable mode for this copy of ICO. Saves,
# settings and the game's data are then kept in the folder userdata beside
# ico_pc instead of in ~/.local/share/ico-pc/ico-pc. Run it from anywhere.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ud="$here/userdata"
pu="${XDG_DATA_HOME:-$HOME/.local/share}/ico-pc/ico-pc"

echo "Make this copy of ICO portable."
if pgrep -x ico_pc >/dev/null 2>&1; then
    echo "ICO is running. Close it and run this again. Nothing was changed." >&2
    exit 1
fi
if [[ -d "$ud" ]] && [[ -n "$(ls -A "$ud" 2>/dev/null)" ]]; then
    echo "The folder userdata beside the program is not empty, so nothing was changed." >&2
    echo "If it holds your files, this copy is already portable." >&2
    exit 1
fi
if ! mkdir -p "$ud" 2>/dev/null; then
    echo "Could not make the folder $ud. Nothing was changed." >&2
    echo "Copy the game to a folder you can write to." >&2
    exit 1
fi
echo "Made the folder userdata beside the program."

if [[ -d "$pu" ]] && [[ -n "$(ls -A "$pu" 2>/dev/null)" ]]; then
    echo
    echo "Your saves and settings are now in $pu"
    answer=n
    if [[ -t 0 ]]; then
        read -r -p "Move them into userdata? [y/N] " answer || answer=n
    fi
    if [[ "$answer" == [yY]* ]]; then
        shopt -s dotglob nullglob
        if mv "$pu"/* "$ud"/; then
            echo "Moved. Your saves and settings are in userdata now."
        else
            echo "Moving the files failed. Move them by hand from $pu into $ud" >&2
        fi
    else
        echo "Left them there. The game starts fresh in userdata. To use them, close the game and move"
        echo "everything from $pu into $ud"
    fi
fi

if grep -qi '^portable=0' "$here/ico-pc.ini" 2>/dev/null; then
    echo
    echo "Note: ico-pc.ini has the line portable=0, which turns portable mode off even"
    echo "with a userdata folder. Delete that line to use portable mode."
fi
echo
echo "Done. To go back, put the line portable=0 in ico-pc.ini beside the program."
