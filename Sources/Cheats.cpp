#include "Cheats.hpp"

#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

using namespace CTRPluginFramework;

namespace McM3
{
    // ------------------------------------------------------------------
    // File helpers
    // ------------------------------------------------------------------

    static bool ReadWholeFile(const std::string &path, std::string &out)
    {
        if (File::Exists(path) != 1)
            return false;

        File file(path, File::READ);
        if (!file.IsOpen())
            return false;

        u64 size = file.GetSize();
        if (size == 0 || size > 512 * 1024)   // 512 KB is way more than any cheat file
            return false;

        out.resize((size_t)size);
        if (file.Read(&out[0], (u32)size) != File::SUCCESS)
            return false;
        return true;
    }

    static void EnsureDir(const std::string &path)
    {
        if (!Directory::IsExists(path))
            Directory::Create(path);
    }

    static std::string Trim(const std::string &s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos)
            return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    static bool IsHex(char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    static bool ParseHex8(const std::string &s, u32 &out)
    {
        if (s.size() != 8)
            return false;
        u32 v = 0;
        for (char c : s)
        {
            if (!IsHex(c))
                return false;
            v <<= 4;
            if (c <= '9')      v |= (u32)(c - '0');
            else if (c <= 'F') v |= (u32)(c - 'A' + 10);
            else               v |= (u32)(c - 'a' + 10);
        }
        out = v;
        return true;
    }

    // ------------------------------------------------------------------
    // Parsing
    // ------------------------------------------------------------------

    void ParseCheatText(const std::string &text, std::vector<Cheat> &out)
    {
        Cheat *current = nullptr;
        bool   inNote = false;
        size_t pos = 0;

        while (pos <= text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos)
                end = text.size();
            std::string line = Trim(text.substr(pos, end - pos));
            pos = end + 1;

            if (line.empty())
                continue;

            // Multi line notes: { ... }
            if (inNote)
            {
                size_t close = line.find('}');
                if (current)
                {
                    if (!current->note.empty())
                        current->note += "\n";
                    current->note += Trim(line.substr(0, close));
                }
                if (close != std::string::npos)
                    inNote = false;
                continue;
            }

            if (line[0] == '[')
            {
                size_t close = line.rfind(']');
                std::string name = Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
                out.emplace_back();
                current = &out.back();
                current->name = name.empty() ? "(unnamed)" : name;
                continue;
            }

            if (line[0] == '{')
            {
                size_t close = line.find('}');
                if (current)
                {
                    current->note = Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
                }
                if (close == std::string::npos)
                    inNote = true;
                continue;
            }

            if (!current)
                continue;

            // Code line: "XXXXXXXX YYYYYYYY" (anything after is ignored)
            size_t sp = line.find_first_of(" \t");
            if (sp == std::string::npos)
                continue;
            std::string a = line.substr(0, sp);
            std::string rest = Trim(line.substr(sp));
            std::string b = rest.substr(0, rest.find_first_of(" \t"));

            CodeLine cl;
            if (ParseHex8(a, cl.left) && ParseHex8(b, cl.right))
                current->lines.push_back(cl);
        }

        // Drop headers that contain no code at all (section titles etc.)
        std::vector<Cheat> cleaned;
        cleaned.reserve(out.size());
        for (auto &c : out)
            if (!c.lines.empty())
                cleaned.push_back(std::move(c));
        out.swap(cleaned);
    }

    void LoadCheats(GameInfo &game, std::vector<Cheat> &out)
    {
        out.clear();
        game.sourcePath.clear();

        const std::string candidates[] =
        {
            "/McM3/cheats/" + game.tidString + ".txt",
            "/cheats/" + game.tidString + ".txt",
        };

        for (const auto &path : candidates)
        {
            std::string text;
            if (ReadWholeFile(path, text))
            {
                ParseCheatText(text, out);
                game.sourcePath = path;
                break;
            }
        }
    }

    // ------------------------------------------------------------------
    // Enabled state
    // ------------------------------------------------------------------

    static std::string EnabledPath(const GameInfo &game)
    {
        return "/McM3/enabled/" + game.tidString + ".txt";
    }

    void SaveEnabled(const GameInfo &game, const std::vector<Cheat> &cheats)
    {
        EnsureDir("/McM3");
        EnsureDir("/McM3/enabled");

        std::string path = EnabledPath(game);
        if (File::Exists(path) == 1)
            File::Remove(path);

        std::string body;
        for (const auto &c : cheats)
            if (c.enabled)
                body += c.name + "\n";

        if (body.empty())
            return;

        File file(path, File::RWC | File::TRUNCATE);
        if (file.IsOpen())
            file.Write(body.data(), (u32)body.size());
    }

    void LoadEnabled(const GameInfo &game, std::vector<Cheat> &cheats)
    {
        std::string text;
        if (!ReadWholeFile(EnabledPath(game), text))
            return;

        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos)
                end = text.size();
            std::string name = Trim(text.substr(pos, end - pos));
            pos = end + 1;
            for (auto &c : cheats)
                if (c.name == name)
                    c.enabled = true;
        }
    }

    // ------------------------------------------------------------------
    // Action Replay (Gateway format) engine
    // Code type reference: Luma3DS rosalina/source/menus/cheats.c
    // ------------------------------------------------------------------

    struct Loop
    {
        size_t startLine;   // first line inside the loop
        u32    remaining;
    };

    bool RunCheat(const Cheat &cheat, u32 keysHeld)
    {
        u32  offset = 0;
        u32  data   = 0;

        // Each entry is "is this block running?". Execution happens only when
        // every level is true.
        std::vector<bool> conds;
        std::vector<Loop> loops;

        auto active = [&]() -> bool
        {
            for (bool b : conds)
                if (!b)
                    return false;
            return true;
        };

        const auto &lines = cheat.lines;
        size_t i = 0;
        size_t guard = 0;   // protects against runaway loops

        while (i < lines.size())
        {
            if (++guard > 100000)
                return false;

            const u32 left  = lines[i].left;
            const u32 right = lines[i].right;
            const u32 type  = left >> 28;
            const u32 addr  = left & 0x0FFFFFFF;
            const bool run  = active();

            switch (type)
            {
                case 0x0: if (run) Process::Write32(addr + offset, right); break;
                case 0x1: if (run) Process::Write16(addr + offset, (u16)right); break;
                case 0x2: if (run) Process::Write8(addr + offset, (u8)right); break;

                // 32 bit conditionals: compare YYYYYYYY with word at address
                case 0x3: case 0x4: case 0x5: case 0x6:
                {
                    bool ok = false;
                    if (run)
                    {
                        u32 v = 0;
                        if (Process::Read32(addr + offset, v))
                        {
                            if (type == 0x3) ok = right > v;
                            if (type == 0x4) ok = right < v;
                            if (type == 0x5) ok = right == v;
                            if (type == 0x6) ok = right != v;
                        }
                    }
                    conds.push_back(ok);
                    break;
                }

                // 16 bit conditionals with mask: ZZZZYYYY, value = half & ~ZZZZ
                case 0x7: case 0x8: case 0x9: case 0xA:
                {
                    bool ok = false;
                    if (run)
                    {
                        u16 v = 0;
                        if (Process::Read16(addr + offset, v))
                        {
                            u16 mask = (u16)(right >> 16);
                            u16 cmp  = (u16)(right & 0xFFFF);
                            u16 val  = (u16)(v & ~mask);
                            if (type == 0x7) ok = cmp > val;
                            if (type == 0x8) ok = cmp < val;
                            if (type == 0x9) ok = cmp == val;
                            if (type == 0xA) ok = cmp != val;
                        }
                    }
                    conds.push_back(ok);
                    break;
                }

                case 0xB:   // offset = word[XXXXXXX + offset]
                    if (run)
                    {
                        u32 v = 0;
                        if (Process::Read32(addr + offset, v))
                            offset = v;
                        else
                            conds.push_back(false);   // bad pointer: skip until terminator
                    }
                    break;

                case 0xC:   // loop YYYYYYYY times
                    loops.push_back({ i + 1, run ? right : 0 });
                    break;

                case 0xD:
                {
                    const u32 sub = (left >> 24) & 0xF;
                    switch (sub)
                    {
                        case 0x0:   // end if
                            if (!conds.empty())
                                conds.pop_back();
                            break;

                        case 0x1:   // loop end (next)
                        case 0x2:   // full terminator
                        {
                            if (!loops.empty())
                            {
                                Loop &lp = loops.back();
                                if (run && lp.remaining > 1)
                                {
                                    lp.remaining--;
                                    i = lp.startLine;
                                    continue;
                                }
                                loops.pop_back();
                            }
                            if (sub == 0x2)
                            {
                                conds.clear();
                                loops.clear();
                                offset = 0;
                                data = 0;
                            }
                            break;
                        }

                        case 0x3: if (run) offset = right; break;
                        case 0x4: if (run) data += right; break;
                        case 0x5: if (run) data = right; break;

                        case 0x6: if (run) { Process::Write32(right + offset, data); offset += 4; } break;
                        case 0x7: if (run) { Process::Write16(right + offset, (u16)data); offset += 2; } break;
                        case 0x8: if (run) { Process::Write8(right + offset, (u8)data); offset += 1; } break;

                        case 0x9: if (run) { u32 v = 0; Process::Read32(right + offset, v); data = v; } break;
                        case 0xA: if (run) { u16 v = 0; Process::Read16(right + offset, v); data = v; } break;
                        case 0xB: if (run) { u8  v = 0; Process::Read8(right + offset, v);  data = v; } break;

                        case 0xC: if (run) offset += right; break;

                        case 0xD:   // button conditional
                            conds.push_back(run && ((keysHeld & right) == right));
                            break;

                        default:
                            return false;
                    }
                    break;
                }

                case 0xE:   // patch: write YYYYYYYY bytes that follow to XXXXXXX + offset
                {
                    u32 count = right;
                    size_t dataLines = (count + 7) / 8;
                    if (run)
                    {
                        u32 dst = addr + offset;
                        for (size_t k = 0; k < dataLines && (i + 1 + k) < lines.size(); k++)
                        {
                            const CodeLine &dl = lines[i + 1 + k];
                            u32 words[2] = { dl.left, dl.right };
                            for (int w = 0; w < 2; w++)
                            {
                                for (int bIdx = 0; bIdx < 4 && count > 0; bIdx++, count--)
                                {
                                    // Bytes are stored little endian inside each word
                                    u8 byte = (u8)(words[w] >> (bIdx * 8));
                                    Process::Write8(dst++, byte);
                                }
                            }
                        }
                    }
                    i += dataLines;
                    break;
                }

                default:
                    return false;
            }

            i++;
        }

        return true;
    }
}
