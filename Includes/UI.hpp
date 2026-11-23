#pragma once

#include "Cheats.hpp"
#include <vector>

namespace McM3
{
    // The button combo that opens McM3 in game: R + Down + Select.
    // It mirrors Luma's Rosalina combo (L + Down + Select) on the other shoulder, and does
    // not overlap NTR (X + Y) or the Pokemon soft reset (L + R + Start + Select).
    // The buttons can be pressed in any order.
    static constexpr u32 kMenuHotkey = (1u << 8) | (1u << 7) | (1u << 2);   // R + DPad Down + Select

    // Set when the game is closing or the console is going to sleep: the menu closes at once.
    extern volatile bool g_closeMenu;

    // Opens the overlay. Pauses the game, blocks until the user closes it.
    void RunMenu(GameInfo &game, std::vector<Cheat> &cheats);
}
