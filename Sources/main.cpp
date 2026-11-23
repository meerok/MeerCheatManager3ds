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
    using namespace McM3;

    // Called before the game starts. Nothing to patch for now.
    void PatchProcess(FwkSettings &settings)
    {
    }

    void OnProcessExit(void)
    {
    }

    int main(void)
    {
        GameInfo game;
        game.titleId = Process::GetTitleID();
        game.version = Process::GetVersion();
        Process::GetName(game.name);   // replaced by the real title if the database knows it

        char tid[20];
        snprintf(tid, sizeof(tid), "%016llX", (unsigned long long)game.titleId);
        game.tidString = tid;

        // Auto detect: the title ID picks this game's cheats out of the built in database,
        // and the user's own file for it (if any) is loaded on top.
        std::vector<Cheat> cheats;
        LoadCheats(game, cheats);
        LoadEnabled(game, cheats);

        int active = 0;
        for (const auto &c : cheats)
            if (c.enabled)
                active++;

        // Give the game a few seconds to boot before showing anything
        svcSleepThread(3000LL * 1000 * 1000);

        const Color green(61, 220, 132), dim(150, 160, 170), bg(10, 12, 14);
        std::string detected = game.known ? game.name : ("Unknown game " + game.tidString);
        OSD::Notify("McM3 detected: " + detected, green, bg);

        std::string summary;
        if (cheats.empty())
            summary = "No cheats yet. Add yours to sd:/McM3/cheats/";
        else
        {
            summary = std::to_string(cheats.size()) + " cheats";
            if (game.customCount > 0)
                summary += " (" + std::to_string(game.customCount) + " yours)";
            if (active > 0)
                summary += ", " + std::to_string(active) + " on";
        }
        OSD::Notify(summary + "  |  R + Down + Select", dim, bg);

        while (true)
        {
            Controller::Update();

            if (Controller::IsKeysDown(kMenuHotkeyHeld) && Controller::IsKeyPressed((Key)kMenuHotkeyPress))
            {
                RunMenu(game, cheats);
                continue;
            }

            const u32 held = Controller::GetKeysDown();
            for (auto &c : cheats)
            {
                if (!c.enabled || c.broken)
                    continue;
                if (!RunCheat(c, held))
                    c.broken = true;   // stop running it and show it in red
            }

            svcSleepThread(16 * 1000 * 1000LL);   // about once per frame
        }

        return 0;
    }
}
