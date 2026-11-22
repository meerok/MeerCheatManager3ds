#include "UI.hpp"

#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <string>
#include <vector>

using namespace CTRPluginFramework;

namespace McM3
{
    // ------------------------------------------------------------------
    // Theme: dark terminal, soft contrast
    // ------------------------------------------------------------------
    namespace Theme
    {
        static const Color Bg        (10, 12, 14);
        static const Color Panel     (17, 20, 23);
        static const Color Row       (26, 31, 36);
        static const Color Line      (38, 44, 50);
        static const Color Text      (216, 222, 228);
        static const Color Dim       (110, 122, 132);
        static const Color Faint     (66, 75, 83);
        static const Color Accent    (61, 220, 132);    // terminal green
        static const Color AccentDim (30, 96, 62);
        static const Color Warn      (240, 180, 80);
        static const Color Bad       (235, 95, 95);
    }

    // CTRPF's built in font is 6 px wide and 10 px tall
    static constexpr int kCharW = 6;

    static constexpr int kTopW = 400;
    static constexpr int kBotW = 320;
    static constexpr int kH    = 240;

    static constexpr int kHeaderH = 18;
    static constexpr int kFooterH = 16;
    static constexpr int kRowH    = 14;
    static constexpr int kListY   = kHeaderH + 6;
    static constexpr int kVisibleRows = (kH - kListY - kFooterH - 4) / kRowH;

    // ------------------------------------------------------------------
    // Small helpers
    // ------------------------------------------------------------------

    static std::string Fit(const std::string &s, int maxChars)
    {
        if ((int)s.size() <= maxChars)
            return s;
        if (maxChars <= 1)
            return s.substr(0, maxChars);
        return s.substr(0, maxChars - 1) + "~";
    }

    static std::vector<std::string> Wrap(const std::string &text, int width)
    {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t nl = text.find('\n', pos);
            std::string para = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = (nl == std::string::npos) ? text.size() : nl + 1;

            std::string line;
            size_t p = 0;
            while (p < para.size())
            {
                size_t sp = para.find(' ', p);
                std::string word = para.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
                p = (sp == std::string::npos) ? para.size() : sp + 1;

                while ((int)word.size() > width)
                {
                    if (!line.empty()) { out.push_back(line); line.clear(); }
                    out.push_back(word.substr(0, width));
                    word = word.substr(width);
                }
                if ((int)(line.size() + word.size() + (line.empty() ? 0 : 1)) > width)
                {
                    out.push_back(line);
                    line.clear();
                }
                if (!line.empty())
                    line += " ";
                line += word;
            }
            out.push_back(line);
        }
        return out;
    }

    static std::string Hex8(u32 v)
    {
        char buf[12];
        snprintf(buf, sizeof(buf), "%08lX", (unsigned long)v);
        return buf;
    }

    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    static void Text(const Screen &s, const std::string &str, int x, int y, const Color &fg, const Color &bg)
    {
        s.Draw(str, (u32)x, (u32)y, fg, bg);
    }

    // ------------------------------------------------------------------
    // Menu state
    // ------------------------------------------------------------------

    struct MenuState
    {
        int    cursor = 0;
        int    scroll = 0;
        float  barY = kListY;        // animated highlight position
        float  open = 0.f;           // 0 -> 1 open animation
        bool   showCodes = false;    // Y toggles code view on bottom screen
        u32    frame = 0;
        int    repeatTimer = 0;
        std::string status;          // short message on the bottom prompt line
        int    statusTimer = 0;
    };

    static int CountEnabled(const std::vector<Cheat> &cheats)
    {
        int n = 0;
        for (const auto &c : cheats)
            if (c.enabled)
                n++;
        return n;
    }

    // ------------------------------------------------------------------
    // Drawing
    // ------------------------------------------------------------------

    static void DrawTop(const Screen &s, const GameInfo &game, const std::vector<Cheat> &cheats, MenuState &st)
    {
        // Slide down reveal: the panel grows from the header
        const int revealH = (int)(kH * st.open);

        s.DrawRect(0, 0, kTopW, kH, Theme::Bg);

        // Header
        s.DrawRect(0, 0, kTopW, kHeaderH, Theme::Panel);
        s.DrawRect(0, kHeaderH, kTopW, 1, Theme::AccentDim);
        Text(s, " McM3 ", 4, 4, Theme::Bg, Theme::Accent);
        std::string title = game.name.empty() ? game.tidString : game.name;
        char ver[16];
        snprintf(ver, sizeof(ver), "v%u", (unsigned)game.version);
        std::string right = game.tidString + " " + ver;
        int rightX = kTopW - 6 - (int)right.size() * kCharW;
        int titleMax = (rightX - 54) / kCharW - 1;
        Text(s, Fit(title, titleMax), 50, 4, Theme::Text, Theme::Panel);
        Text(s, right, rightX, 4, Theme::Dim, Theme::Panel);

        if (cheats.empty())
        {
            int y = kListY + 6;
            Text(s, "$ load cheats", 10, y, Theme::Accent, Theme::Bg); y += 16;
            Text(s, "no cheat file found for this game.", 10, y, Theme::Text, Theme::Bg); y += 14;
            Text(s, "put one at:", 10, y, Theme::Dim, Theme::Bg); y += 14;
            Text(s, "  sd:/McM3/cheats/" + game.tidString + ".txt", 10, y, Theme::Warn, Theme::Bg); y += 12;
            Text(s, "  sd:/cheats/" + game.tidString + ".txt", 10, y, Theme::Warn, Theme::Bg);
        }
        else
        {
            // Highlight bar (animated)
            int barY = (int)st.barY;
            if (barY + kRowH <= revealH)
            {
                s.DrawRect(4, barY, kTopW - 14, kRowH, Theme::Row);
                s.DrawRect(4, barY, 2, kRowH, Theme::Accent);
            }

            for (int r = 0; r < kVisibleRows; r++)
            {
                int idx = st.scroll + r;
                if (idx >= (int)cheats.size())
                    break;
                int y = kListY + r * kRowH;
                if (y + kRowH > revealH)
                    break;

                const Cheat &c = cheats[idx];
                const bool selected = idx == st.cursor;

                // Use the row color as text background when the bar is under this row
                bool barHere = (barY > y - kRowH / 2) && (barY < y + kRowH / 2);
                const Color &bg = barHere ? Theme::Row : Theme::Bg;

                std::string box = c.enabled ? "[x]" : "[ ]";
                const Color &boxCol = c.broken ? Theme::Bad : (c.enabled ? Theme::Accent : Theme::Faint);
                Text(s, box, 12, y + 2, boxCol, bg);

                const Color &nameCol = selected ? Theme::Text : (c.enabled ? Theme::Text : Theme::Dim);
                Text(s, Fit(c.name, (kTopW - 50) / kCharW), 36, y + 2, nameCol, bg);
            }

            // Scrollbar
            if ((int)cheats.size() > kVisibleRows)
            {
                int trackY = kListY;
                int trackH = kVisibleRows * kRowH;
                int thumbH = trackH * kVisibleRows / (int)cheats.size();
                if (thumbH < 10) thumbH = 10;
                int thumbY = trackY + (trackH - thumbH) * st.scroll / ((int)cheats.size() - kVisibleRows);
                s.DrawRect(kTopW - 6, trackY, 2, trackH, Theme::Line);
                s.DrawRect(kTopW - 6, thumbY, 2, thumbH, Theme::Accent);
            }
        }

        // Footer
        int fy = kH - kFooterH;
        s.DrawRect(0, fy - 1, kTopW, 1, Theme::Line);
        s.DrawRect(0, fy, kTopW, kFooterH, Theme::Panel);
        char foot[64];
        snprintf(foot, sizeof(foot), "%d cheats  /  %d active", (int)cheats.size(), CountEnabled(cheats));
        Text(s, foot, 8, fy + 3, Theme::Dim, Theme::Panel);
        if (!game.sourcePath.empty())
        {
            std::string src = Fit("sd:" + game.sourcePath.substr(0, game.sourcePath.rfind('/') + 1), 30);
            Text(s, src, kTopW - 8 - (int)src.size() * kCharW, fy + 3, Theme::Faint, Theme::Panel);
        }
    }

    static void DrawBottom(const Screen &s, const std::vector<Cheat> &cheats, MenuState &st)
    {
        s.DrawRect(0, 0, kBotW, kH, Theme::Bg);

        // Header
        s.DrawRect(0, 0, kBotW, kHeaderH, Theme::Panel);
        s.DrawRect(0, kHeaderH, kBotW, 1, Theme::Line);
        Text(s, st.showCodes ? "$ cat codes" : "$ info", 6, 4, Theme::Accent, Theme::Panel);

        const int cols = (kBotW - 16) / kCharW;
        int y = kHeaderH + 8;

        if (!cheats.empty())
        {
            const Cheat &c = cheats[st.cursor];

            for (const auto &l : Wrap(c.name, cols))
            {
                Text(s, l, 8, y, Theme::Text, Theme::Bg);
                y += 11;
            }
            y += 2;
            Text(s, c.enabled ? "status: ON" : "status: off", 8, y,
                 c.enabled ? Theme::Accent : Theme::Dim, Theme::Bg);
            if (c.broken)
                Text(s, "  unsupported code", 8 + 11 * kCharW, y, Theme::Bad, Theme::Bg);
            y += 16;

            const int maxY = kH - 50;
            if (st.showCodes)
            {
                for (const auto &line : c.lines)
                {
                    if (y > maxY) { Text(s, "...", 8, y, Theme::Faint, Theme::Bg); break; }
                    Text(s, Hex8(line.left) + " " + Hex8(line.right), 8, y, Theme::Dim, Theme::Bg);
                    y += 11;
                }
            }
            else
            {
                std::string note = c.note.empty() ? "no description for this cheat." : c.note;
                for (const auto &l : Wrap(note, cols))
                {
                    if (y > maxY) { Text(s, "...", 8, y, Theme::Faint, Theme::Bg); break; }
                    Text(s, l, 8, y, c.note.empty() ? Theme::Faint : Theme::Dim, Theme::Bg);
                    y += 11;
                }
            }
        }

        // Controls
        int cy = kH - 44;
        s.DrawRect(0, cy - 4, kBotW, 1, Theme::Line);
        Text(s, "A toggle   Y codes   X all off", 8, cy, Theme::Dim, Theme::Bg);
        Text(s, "L/R page   B close", 8, cy + 12, Theme::Dim, Theme::Bg);

        // Prompt line with blinking cursor
        int py = kH - 14;
        std::string prompt = "mcm3> " + (st.statusTimer > 0 ? st.status : std::string());
        Text(s, prompt, 8, py, Theme::Accent, Theme::Bg);
        if ((st.frame / 20) % 2 == 0)
            s.DrawRect(8 + (int)prompt.size() * kCharW + 1, py, kCharW, 10, Theme::Accent);
    }

    // ------------------------------------------------------------------
    // Input
    // ------------------------------------------------------------------

    static void MoveCursor(MenuState &st, int delta, int count)
    {
        if (count == 0)
            return;
        st.cursor += delta;
        if (st.cursor < 0)        st.cursor = (delta == -1) ? count - 1 : 0;
        if (st.cursor >= count)   st.cursor = (delta == 1) ? 0 : count - 1;

        if (st.cursor < st.scroll)
            st.scroll = st.cursor;
        if (st.cursor >= st.scroll + kVisibleRows)
            st.scroll = st.cursor - kVisibleRows + 1;
    }

    static void SetStatus(MenuState &st, const std::string &msg)
    {
        st.status = msg;
        st.statusTimer = 90;
    }

    static void WaitForRelease(void)
    {
        do
        {
            Controller::Update();
            svcSleepThread(16 * 1000 * 1000LL);
        } while (Controller::GetKeysDown() != 0);
    }

    // ------------------------------------------------------------------
    // Main menu loop
    // ------------------------------------------------------------------

    void RunMenu(GameInfo &game, std::vector<Cheat> &cheats)
    {
        static MenuState st;   // keep cursor position between openings
        st.open = 0.f;
        st.statusTimer = 0;
        if (st.cursor >= (int)cheats.size())
            st.cursor = st.scroll = 0;
        st.barY = kListY + (st.cursor - st.scroll) * kRowH;

        Process::Pause();
        WaitForRelease();   // don't let the hotkey leak into the menu

        bool changed = false;
        bool running = true;

        while (running)
        {
            Controller::Update();
            const int count = (int)cheats.size();

            // Up / down with hold-to-repeat
            bool up   = Controller::IsKeyDown(Key::DPadUp)   || Controller::IsKeyDown(Key::CPadUp);
            bool down = Controller::IsKeyDown(Key::DPadDown) || Controller::IsKeyDown(Key::CPadDown);
            if (up || down)
            {
                if (st.repeatTimer == 0 || (st.repeatTimer > 18 && st.repeatTimer % 4 == 0))
                    MoveCursor(st, up ? -1 : 1, count);
                st.repeatTimer++;
            }
            else
                st.repeatTimer = 0;

            if (Controller::IsKeyPressed(Key::L))
                for (int k = 0; k < kVisibleRows; k++) MoveCursor(st, -1, count);
            if (Controller::IsKeyPressed(Key::R))
                for (int k = 0; k < kVisibleRows; k++) MoveCursor(st, 1, count);

            if (Controller::IsKeyPressed(Key::A) && count > 0)
            {
                Cheat &c = cheats[st.cursor];
                c.enabled = !c.enabled;
                c.broken = false;
                changed = true;
                SetStatus(st, (c.enabled ? "enabled " : "disabled ") + Fit(c.name, 30));
            }

            if (Controller::IsKeyPressed(Key::X) && count > 0)
            {
                for (auto &c : cheats)
                    c.enabled = false;
                changed = true;
                SetStatus(st, "all cheats off");
            }

            if (Controller::IsKeyPressed(Key::Y))
                st.showCodes = !st.showCodes;

            if (Controller::IsKeyPressed(Key::B))
                running = false;

            // Animations
            st.open = Lerp(st.open, 1.f, 0.28f);
            if (st.open > 0.995f) st.open = 1.f;
            float targetY = (float)(kListY + (st.cursor - st.scroll) * kRowH);
            st.barY = Lerp(st.barY, targetY, 0.35f);
            if (st.barY - targetY < 0.5f && targetY - st.barY < 0.5f) st.barY = targetY;
            if (st.statusTimer > 0) st.statusTimer--;
            st.frame++;

            DrawTop(OSD::GetTopScreen(), game, cheats, st);
            DrawBottom(OSD::GetBottomScreen(), cheats, st);
            OSD::SwapBuffers();

            svcSleepThread(16 * 1000 * 1000LL);
        }

        if (changed)
            SaveEnabled(game, cheats);

        WaitForRelease();   // don't let B leak into the game
        Process::Play();
    }
}
