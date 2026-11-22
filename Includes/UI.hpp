#pragma once

#include "Cheats.hpp"
#include <vector>

namespace McM3
{
    // The button combo that opens McM3 in game.
    // R + Down + Select: mirrors Luma's Rosalina combo (L + Down + Select) on the
    // other shoulder, and does not overlap CTRPF (Select), NTR (X + Y) or the
    // Pokemon soft reset (L + R + Start + Select).
    static constexpr u32 kMenuHotkeyHeld  = (1u << 8) | (1u << 2);   // R + Select
    static constexpr u32 kMenuHotkeyPress = (1u << 7);                // DPad Down

    // Opens the overlay. Pauses the game, blocks until the user closes it.
    void RunMenu(GameInfo &game, std::vector<Cheat> &cheats);
}
