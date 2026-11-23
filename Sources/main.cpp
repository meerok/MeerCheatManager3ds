// McM3 - Meer Cheat Manager 3ds
// A Luma3DS game plugin (3GX) built on CTRPluginFramework.
// Open in game with R + Down + Select.

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstdio>
#include <cstdarg>
#include <malloc.h>
#include <unistd.h>
#include <vector>

#include "Cheats.hpp"
#include "Database.hpp"
#include "UI.hpp"

extern "C" char *fake_heap_end;   // end of the plugin heap (newlib)

namespace CTRPluginFramework
{
    // CTRPF's "System heap": a pool it reserves at boot, mostly for its memory search.
    namespace Heap
    {
        extern u8  *__ctrpf_heap;
        extern u32  __ctrpf_heap_size;
    }

    using namespace McM3;
}

namespace McM3
{
    using namespace CTRPluginFramework;

    // A short step-by-step log on the SD card (sd:/McM3/log.txt, rewritten every time a game
    // starts), so a hang or crash can be pinned down from a tester's SD card.
    void DebugLog(const char *fmt, ...)
    {
        static bool first = true;
        static int lines = 0;
        if (lines++ > 200)
            return;

        char msg[160];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);

        struct mallinfo mi = mallinfo();
        const u32 unclaimed = (u32)(uintptr_t)fake_heap_end - (u32)(uintptr_t)sbrk(0);
        char line[240];
        const int n = snprintf(line, sizeof(line), "[%llu ms] %s | free %u KB (in heap %u + unclaimed %u)\n",
            (unsigned long long)(svcGetSystemTick() / 268111ULL), msg,
            (unsigned)((mi.fordblks + unclaimed) / 1024), (unsigned)(mi.fordblks / 1024), (unsigned)(unclaimed / 1024));

        Directory::Create("/McM3");
        File f;
        const int mode = File::WRITE | File::CREATE | (first ? File::TRUNCATE : File::APPEND) | File::SYNC;
        if (File::Open(f, "/McM3/log.txt", mode) == File::SUCCESS)
            f.Write(line, (u32)n);
        first = false;
    }
}

namespace CTRPluginFramework
{
    using namespace McM3;

    // McM3 runs its own small loop instead of CTRPF's PluginMenu, which needs too much
    // memory to fit in 2 MiB (the most an Old 3DS can safely give a plugin).
    // Buttons may only be read while the game is running normally: the process events
    // below stop the loop before Luma unmaps anything (HOME Menu, sleep, closing the game).
    static volatile bool g_exiting = false;     // game is closing: stop for good
    static volatile bool g_suspended = false;   // HOME Menu or sleep: wait

    // Called before the game starts
    void PatchProcess(FwkSettings &settings)
    {
        // McM3 has its own cheat engine and menu: turn off CTRPF's built in
        // Action Replay handler (it would also read sd:/cheats) and memory search.
        DebugLog("McM3 v0.2.5: PatchProcess start, pool %u KB", (unsigned)(Heap::__ctrpf_heap_size / 1024));
        settings.AllowActionReplay = false;
        settings.AllowSearchEngine = false;

        // Keep memory free for the game (matters on Old 3DS): no menu sounds from the SD card
        settings.TryLoadSDSounds = false;

        // CTRPF reserves 1 MB here for its memory search, which McM3 turns off. In a 2 MiB
        // plugin that, plus its 676 KB of screen buffers, left only ~100 KB for everything
        // else, and opening the menu ran the heap dry (v0.2.3 crash when pressing the hotkey).
        // The pool is only set up on first use, and nothing has used it yet at this point,
        // so swap it for a small one. 128 KB still covers its other users (folder listing
        // needs ~55 KB, line reader 4 KB).
        static constexpr u32 kSmallPool = 0x20000;
        if (Heap::__ctrpf_heap != nullptr && Heap::__ctrpf_heap_size > kSmallPool)
        {
            ::operator delete(Heap::__ctrpf_heap);
            Heap::__ctrpf_heap = static_cast<u8 *>(::operator new(kSmallPool));
            Heap::__ctrpf_heap_size = kSmallPool;
        }
        DebugLog("PatchProcess done, pool %u KB", (unsigned)(Heap::__ctrpf_heap_size / 1024));
    }

    void OnProcessExit(void)
    {
        g_exiting = true;
        McM3::g_closeMenu = true;
    }

    static void OnProcessEvent(Process::Event event)
    {
        switch (event)
        {
            case Process::Event::EXIT:
                g_exiting = true;
                McM3::g_closeMenu = true;
                break;
            case Process::Event::SLEEP_ENTER:
            case Process::Event::HOME_ENTER:
                g_suspended = true;
                McM3::g_closeMenu = true;
                break;
            case Process::Event::SLEEP_EXIT:
            case Process::Event::HOME_EXIT:
            case Process::Event::SWAP_EXIT:
                g_suspended = false;
                break;
            default:
                break;
        }
    }

    int main(void)
    {
        DebugLog("main start");
        Process::SetProcessEventCallback(OnProcessEvent);

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
        DebugLog("loading cheats for %s", tid);
        LoadCheats(game, cheats);
        LoadEnabled(game, cheats);
        DebugLog("loaded %u cheats, known=%d, db error='%s'", (unsigned)cheats.size(), (int)game.known, BuiltinError().c_str());

        int active = 0;
        for (const auto &c : cheats)
            if (c.enabled)
                active++;

        const Color green(61, 220, 132), dim(150, 160, 170), bg(10, 12, 14);
        std::string detected = game.known ? game.name : ("Unknown game " + game.tidString);
        OSD::Notify("McM3 detected: " + detected, green, bg);
        if (!BuiltinError().empty())
            OSD::Notify("McM3: " + BuiltinError(), Color(240, 180, 80), bg);

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

        DebugLog("notifications sent, entering loop");

        // true while the whole combo is held, so it only fires once per press
        bool comboHeld = true;

        while (!g_exiting)
        {
            if (g_suspended)
            {
                comboHeld = true;   // don't open the menu from buttons held while away
                svcSleepThread(50 * 1000 * 1000LL);
                continue;
            }

            Controller::Update();
            const u32 held = Controller::GetKeysDown();

            // R + Down + Select in any order: open as soon as all three are down
            const bool combo = (held & kMenuHotkey) == kMenuHotkey;
            if (combo && !comboHeld && !g_exiting && !g_suspended)
            {
                comboHeld = true;
                DebugLog("hotkey pressed, opening menu");
                McM3::g_closeMenu = false;
                RunMenu(game, cheats);
                continue;
            }
            comboHeld = combo;

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
