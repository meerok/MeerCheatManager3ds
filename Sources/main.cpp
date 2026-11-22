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
        Process::GetName(game.name);

        char tid[20];
        snprintf(tid, sizeof(tid), "%016llX", (unsigned long long)game.titleId);
        game.tidString = tid;

        std::vector<Cheat> cheats;
        LoadCheats(game, cheats);
        LoadEnabled(game, cheats);

        // Give the game a few seconds to boot before showing anything
        svcSleepThread(3000LL * 1000 * 1000);
        OSD::Notify("McM3 ready  |  R + Down + Select", Color(61, 220, 132), Color(10, 12, 14));

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
