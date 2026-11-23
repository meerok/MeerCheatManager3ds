#!/usr/bin/env python3
"""
Appends McM3's cheat database to the end of McM3.3gx.

    python3 tools/pack.py McM3.3gx Data/builtin.bin

Luma only loads the 3GX's code and data sections into memory, so anything after them
stays on the SD card. McM3 reads just the running game's part of the database at startup
(Sources/Database.cpp), which keeps the plugin's memory use small.

Layout after packing:   <3gx> <database> "MCM3TAIL" u32 dbOffset u32 dbSize
"""
import struct
import sys


def main(plugin_path, db_path):
    plugin = open(plugin_path, "rb").read()
    if plugin[-16:-8] == b"MCM3TAIL":
        sys.exit("%s already has a database" % plugin_path)
    db = open(db_path, "rb").read()
    if not db.startswith(b"MCM3DB01"):
        sys.exit("%s is not a McM3 database" % db_path)

    out = plugin + db + b"MCM3TAIL" + struct.pack("<II", len(plugin), len(db))
    open(plugin_path, "wb").write(out)
    print("packed %s: %d byte plugin + %d byte database" % (plugin_path, len(plugin), len(db)))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
