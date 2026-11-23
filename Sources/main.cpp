// McM3 - Meer Cheat Manager 3ds
// A Luma3DS game plugin (3GX) built on CTRPluginFramework.
// Open in game with R + Down + Select.

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstdio>
#include <vector>

#include "Cheats.hpp"
#include "UI.hpp"

namespace CTRPluginFramework
{
    // CTRPF keeps the "open the menu" combo here (libctrpf Preferences::MenuHotkeys).
    // McM3 uses CTRPF's own menu loop so button reading, sleep mode and closing the
    // game are all handled safely, and swaps CTRPF's menu for its own when it opens.
    class Preferences
    {
    public:
        static u32 MenuHotkeys;
    };

    using namespace McM3;

    static GameInfo             g_game;
    static std::vector<Cheat>   g_cheats;
    static volatile bool        g_exiting = false;

    // Called before the game starts
    void PatchProcess(FwkSettings &settings)
    {
        // McM3 has its own cheat engine and menu: turn off CTRPF's built in
        // Action Replay handler (it would also read sd:/cheats) and memory search.
        settings.AllowActionReplay = false;
        settings.AllowSearchEngine = false;
    }

    void OnProcessExit(void)
    {
        g_exiting = true;
        McM3::g_closeMenu = true;
    }

    static void OnProcessEvent(Process::Event event)
    {
        if (event == Process::Event::EXIT)
        {
            g_exiting = true;
            McM3::g_closeMenu = true;
        }
        else if (event == Process::Event::SLEEP_ENTER)
            McM3::g_closeMenu = true;   // lid closed: get out of the way
    }

    // CTRPF calls this when the menu combo is pressed. Show McM3's menu instead of
    // CTRPF's, then return false so CTRPF's own menu never opens.
    static bool OnMenuOpening(void)
    {
        if (!g_exiting)
        {
            McM3::g_closeMenu = false;
            RunMenu(g_game, g_cheats);
        }
        return false;
    }

    // Runs once per game frame while McM3's menu is closed
    static void EveryFrame(void)
    {
        // CTRPF can reload its saved settings, so keep McM3's combo in place
        Preferences::MenuHotkeys = kMenuHotkey;

        if (g_exiting)
            return;

        const u32 held = Controller::GetKeysDown();
        for (auto &c : g_cheats)
        {
            if (!c.enabled || c.broken)
                continue;
            if (!RunCheat(c, held))
                c.broken = true;   // stop running it and show it in red
        }
    }

    int main(void)
    {
        g_game.titleId = Process::GetTitleID();
        g_game.version = Process::GetVersion();
        Process::GetName(g_game.name);   // replaced by the real title if the database knows it

        char tid[20];
        snprintf(tid, sizeof(tid), "%016llX", (unsigned long long)g_game.titleId);
        g_game.tidString = tid;

        // Auto detect: the title ID picks this game's cheats out of the built in database,
        // and the user's own file for it (if any) is loaded on top.
        LoadCheats(g_game, g_cheats);
        LoadEnabled(g_game, g_cheats);

        int active = 0;
        for (const auto &c : g_cheats)
            if (c.enabled)
                active++;

        const Color green(61, 220, 132), dim(150, 160, 170), bg(10, 12, 14);
        std::string detected = g_game.known ? g_game.name : ("Unknown game " + g_game.tidString);
        OSD::Notify("McM3 detected: " + detected, green, bg);

        std::string summary;
        if (g_cheats.empty())
            summary = "No cheats yet. Add yours to sd:/McM3/cheats/";
        else
        {
            summary = std::to_string(g_cheats.size()) + " cheats";
            if (g_game.customCount > 0)
                summary += " (" + std::to_string(g_game.customCount) + " yours)";
            if (active > 0)
                summary += ", " + std::to_string(active) + " on";
        }
        OSD::Notify(summary + "  |  R + Down + Select", dim, bg);

        Process::SetProcessEventCallback(OnProcessEvent);

        PluginMenu menu("McM3", 0, 2, 2, "Meer Cheat Manager 3ds");
        menu.ShowWelcomeMessage(false);
        menu.SynchronizeWithFrame(true);
        menu.OnOpening = OnMenuOpening;
        menu += EveryFrame;
        Preferences::MenuHotkeys = kMenuHotkey;

        // Blocks until the game closes
        menu.Run();
        return 0;
    }
}
