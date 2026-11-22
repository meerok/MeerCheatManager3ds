#!/usr/bin/env python3
"""
Builds Data/builtin.bin, McM3's built in cheat database.

    python3 tools/build_db.py <path to Sharkive/3ds>

Layout of the file (plain text, embedded into the plugin by bin2o):

    @<TitleID>|<Game name>
    @quick
    ...McM3 quick picks for this game (tools/quickpicks.txt)...
    @end-quick
    ...Sharkive cheats for this game...

Cheat codes come from Sharkive (https://github.com/FlagBrew/Sharkive, GPLv3).
Text is reduced to plain ASCII because the 3DS overlay font has no accents.
"""
import os
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Every Pokemon 3DS title Sharkive has codes for (title IDs from Bl4ckSh4rk's Pokemon 3DS title list)
GAMES = [
    ("0004000000055D00", "Pokemon X"),
    ("0004000000055E00", "Pokemon Y"),
    ("000400000011C400", "Pokemon Omega Ruby"),
    ("000400000011C500", "Pokemon Alpha Sapphire"),
    ("0004000000164800", "Pokemon Sun"),
    ("0004000000175E00", "Pokemon Moon"),
    ("00040000001B5000", "Pokemon Ultra Sun"),
    ("00040000001B5100", "Pokemon Ultra Moon"),
    ("0004000000171000", "Pokemon Red (VC)"),
    ("0004000000171100", "Pokemon Blue (VC)"),
    ("0004000000171200", "Pokemon Yellow (VC)"),
    ("0004000000172600", "Pokemon Gold (VC)"),
    ("0004000000172700", "Pokemon Silver (VC)"),
    ("0004000000172800", "Pokemon Crystal (VC)"),
    ("00040000000AE100", "Pokemon Dream Radar"),
    ("000400000017C100", "Pokemon Picross"),
    ("0004000000185A00", "Pokemon Rumble World"),
    ("0004000000066C00", "Pokemon Rumble Blast"),
    ("00040000000BA900", "Pokemon Mystery Dungeon: Gates to Infinity"),
    ("0004000000174400", "Pokemon Super Mystery Dungeon"),
    ("0004000000174600", "Pokemon Super Mystery Dungeon"),
]


def ascii_only(text):
    text = unicodedata.normalize("NFD", text)
    text = "".join(c for c in text if not unicodedata.combining(c))
    return text.encode("ascii", "ignore").decode("ascii")


def clean(text):
    text = ascii_only(text.replace("\r", ""))
    return text if text.endswith("\n") else text + "\n"


def quick_sections(path):
    sections, current = {}, None
    for line in open(path, encoding="utf-8").read().split("\n"):
        if line.startswith("#"):
            continue
        if line.startswith("@0004"):
            current = line[1:].strip()
            sections[current] = []
            continue
        if current:
            sections[current].append(line)
    return {k: "\n".join(v).strip() + "\n" for k, v in sections.items() if "\n".join(v).strip()}


def build(sharkive_dir):
    quick = quick_sections(os.path.join(HERE, "quickpicks.txt"))
    out = []
    for tid, name in GAMES:
        path = os.path.join(sharkive_dir, tid + ".txt")
        if not os.path.exists(path):
            continue
        out.append("@%s|%s\n" % (tid, name))
        if tid in quick:
            out.append("@quick\n" + quick[tid] + "@end-quick\n")
        out.append(clean(open(path, encoding="utf-8").read()))
    return "".join(out)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    data = build(sys.argv[1])
    os.makedirs(os.path.join(ROOT, "Data"), exist_ok=True)
    with open(os.path.join(ROOT, "Data", "builtin.bin"), "w", encoding="ascii", newline="\n") as f:
        f.write(data)
    print("wrote Data/builtin.bin (%d bytes)" % len(data))
