#pragma once

#include <3ds.h>
#include <string>

namespace McM3
{
    enum DatabaseFlags : u32
    {
        DB_POKEMON_VERSIONS = 1,   // update version 1040 * N means game version 1.N
    };

    // Looks the running game up in McM3's built in database, which sits at the end of
    // McM3.3gx (see tools/pack.py). On success fills the game's name (may be empty),
    // its cheat text and flags.
    bool FindBuiltin(u64 titleId, std::string &name, std::string &text, u32 &flags);

    // Number of games in the built in database (after FindBuiltin ran).
    u32 BuiltinGameCount(void);

    // Why the database couldn't be read ("" if it was fine or the game just isn't in it).
    const std::string &BuiltinError(void);
}
