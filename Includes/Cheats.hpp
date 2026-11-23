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

    // How an adjustable cheat puts the chosen number into its codes
    enum class ValueMode : u8
    {
        None,
        Match,   // every write whose value == match becomes (value << shl) | orBits
        Qty,     // bag words: amount field at bit "at" (10 for Gen 7, 16 for Gen 6), item id below it
    };

    struct Cheat
    {
        std::string            name;      // name as written in the file (used to save state)
        std::string            display;   // name shown in the menu (version tag removed)
        std::string            note;
        std::string            copyFrom;  // "@from <name>": borrow the codes of another cheat
        std::vector<CodeLine>  tmpl;      // codes as written
        std::vector<CodeLine>  lines;     // codes that actually run (value applied)
        bool                   enabled = false;
        bool                   broken = false;    // hit an unsupported code type
        bool                   quick = false;     // McM3 "quick pick" (shown first)
        bool                   custom = false;    // came from the user's own cheat file

        // Adjustable value ("$ min max default ..." line)
        ValueMode              mode = ValueMode::None;
        u32                    vmin = 0, vmax = 0, value = 0;
        u32                    match = 0, shl = 0, orBits = 0, qty = 0, at = 10;

        bool HasValue() const { return mode != ValueMode::None; }
    };

    struct GameInfo
    {
        u64          titleId = 0;
        u16          version = 0;
        int          minor = -1;        // Pokemon style "v1.N" update number, -1 if unknown
        std::string  tidString;         // 16 uppercase hex chars
        std::string  name;              // game name from the database, or the process name
        bool         known = false;     // the game is in McM3's built in database
        std::string  sourcePath;        // user cheat file that was loaded ("" if none)
        int          customCount = 0;   // cheats from the user's own file
        int          builtinCount = 0;  // cheats that came from McM3's built in database
        int          hiddenCount = 0;   // cheats hidden because they're for another version
    };

    // Detects the running game and loads its cheats: the user's own file (sd:/McM3/cheats or
    // sd:/cheats) plus McM3's built in database. Cheats for other game versions are dropped.
    void LoadCheats(GameInfo &game, std::vector<Cheat> &out);

    // Parses the [Name] / {note} / "XXXXXXXX YYYYYYYY" cheat format (plus McM3's "$" and "@from" lines).
    void ParseCheatText(const std::string &text, std::vector<Cheat> &out, bool quick = false);

    // Rebuilds cheat.lines from cheat.tmpl using cheat.value.
    void ApplyValue(Cheat &cheat);

    // Saves / restores which cheats are on and their values (sd:/McM3/enabled/<TID>.txt).
    void SaveEnabled(const GameInfo &game, const std::vector<Cheat> &cheats);
    void LoadEnabled(const GameInfo &game, std::vector<Cheat> &cheats);

    // Runs one cheat once. Call every frame for every enabled cheat.
    // Returns false if the code used an unsupported type.
    bool RunCheat(const Cheat &cheat, u32 keysHeld);

    // "1234567" -> "1,234,567"
    std::string FormatNumber(u32 v);
}
