#pragma once

#include <3ds.h>
#include <string>
#include <vector>

namespace McM3
{
    struct CodeLine
    {
        u32 left;
        u32 right;
    };

    struct Cheat
    {
        std::string            name;
        std::string            note;
        std::vector<CodeLine>  lines;
        bool                   enabled = false;
        bool                   broken = false;   // last run hit an unknown code type or bad address
    };

    struct GameInfo
    {
        u64          titleId = 0;
        u16          version = 0;
        std::string  tidString;   // 16 uppercase hex chars
        std::string  name;        // process name, e.g. "Pokemon"
        std::string  sourcePath;  // the cheat file that was loaded ("" if none)
    };

    // Loads cheats for the running game. Looks in sd:/McM3/cheats/<TID>.txt first,
    // then the Luma/Rosalina location sd:/cheats/<TID>.txt.
    void LoadCheats(GameInfo &game, std::vector<Cheat> &out);

    // Parses the standard [Name] / {note} / "XXXXXXXX YYYYYYYY" cheat format.
    void ParseCheatText(const std::string &text, std::vector<Cheat> &out);

    // Saves / restores which cheats are switched on (sd:/McM3/enabled/<TID>.txt).
    void SaveEnabled(const GameInfo &game, const std::vector<Cheat> &cheats);
    void LoadEnabled(const GameInfo &game, std::vector<Cheat> &cheats);

    // Runs one cheat once. Call every frame for every enabled cheat.
    // Returns false if the code used an unsupported type.
    bool RunCheat(const Cheat &cheat, u32 keysHeld);
}
