#!/usr/bin/env python3
"""
Builds Data/builtin.bin, McM3's built in cheat database.

    python3 tools/build_db.py <Sharkive repo> <Sharkive wiki repo>

GitHub Actions runs this on every build with pinned copies of:
  - Sharkive       https://github.com/FlagBrew/Sharkive        (cheat codes, GPLv3)
  - Sharkive wiki  https://github.com/FlagBrew/Sharkive/wiki   (game names and regions)

File layout (little endian):

    "MCM3DB01"
    u32 count
    count x { u64 titleId, u32 offset, u32 packedSize, u32 rawSize, u32 flags }
    packed sections

Each section unpacks (tools/lz.py format) to plain text:

    <Game name>\n
    @quick ... @end-quick     (McM3 quick picks from tools/quickpicks.txt, if any)
    <Sharkive cheats>

Text is reduced to plain ASCII because the 3DS overlay font has no accents.
"""
import os
import re
import struct
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lz  # noqa: E402

# Games whose update versions follow the "1040 x N = v1.N" rule (used to hide codes for other versions)
FLAG_POKEMON_VERSIONS = 1
POKEMON_MAINLINE = {
    "0004000000055D00", "0004000000055E00", "000400000011C400", "000400000011C500",
    "0004000000164800", "0004000000175E00", "00040000001B5000", "00040000001B5100",
}


def ascii_only(text):
    for a, b in (("’", "'"), ("‘", "'"), ("“", '"'), ("”", '"')):
        text = text.replace(a, b)
    text = unicodedata.normalize("NFD", text)
    text = "".join(c for c in text if not unicodedata.combining(c))
    return text.encode("ascii", "ignore").decode("ascii")


def clean(text):
    text = ascii_only(text.replace("\r", ""))
    return text if text.endswith("\n") else text + "\n"


def game_names(wiki_dir):
    """Title ID -> "Name (Region)" from the Sharkive wiki's game list."""
    md = open(os.path.join(wiki_dir, "3DS-games-in-the-database.md"), encoding="utf-8").read()
    names = {}
    for block in re.finditer(r"<summary>(.*?)</summary>(.*?)</details>", md, re.S):
        name = block.group(1).strip()
        for region, tid in re.findall(r"!\[(\w+)\]\]\(\.\./blob/master/3ds/([0-9A-Fa-f]{16})\.txt\)", block.group(2)):
            names.setdefault(tid.upper(), "%s (%s)" % (name, region))
    return names


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


def build(sharkive_dir, wiki_dir):
    names = game_names(wiki_dir)
    quick = quick_sections(os.path.join(HERE, "quickpicks.txt"))
    cheat_dir = os.path.join(sharkive_dir, "3ds")

    entries = []
    for fname in sorted(os.listdir(cheat_dir)):
        m = re.match(r"^([0-9A-Fa-f]{16})\.txt$", fname)
        if not m:
            continue
        tid = m.group(1).upper()
        text = ascii_only(names.get(tid, "")) + "\n"
        if tid in quick:
            text += "@quick\n" + quick[tid] + "@end-quick\n"
        text += clean(open(os.path.join(cheat_dir, fname), encoding="utf-8", errors="replace").read())
        raw = text.encode("ascii")
        flags = FLAG_POKEMON_VERSIONS if tid in POKEMON_MAINLINE else 0
        entries.append((int(tid, 16), raw, flags))
    entries.sort(key=lambda e: e[0])   # McM3 binary searches the index

    header = b"MCM3DB01" + struct.pack("<I", len(entries))
    offset = len(header) + 24 * len(entries)
    index, blobs = b"", b""
    for tid, raw, flags in entries:
        packed = lz.compress(raw)
        assert lz.decompress(packed) == raw
        index += struct.pack("<QIIII", tid, offset + len(blobs), len(packed), len(raw), flags)
        blobs += packed
    return header + index + blobs, len(entries)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    data, count = build(sys.argv[1], sys.argv[2])
    os.makedirs(os.path.join(ROOT, "Data"), exist_ok=True)
    with open(os.path.join(ROOT, "Data", "builtin.bin"), "wb") as f:
        f.write(data)
    print("wrote Data/builtin.bin: %d games, %d bytes" % (count, len(data)))
