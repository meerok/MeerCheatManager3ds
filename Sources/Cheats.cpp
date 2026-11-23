#include "Cheats.hpp"

#include <CTRPluginFramework.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "Database.hpp"

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

    static bool ParseHexAny(const std::string &s, u32 &out)
    {
        if (s.empty() || s.size() > 8)
            return false;
        std::string padded = std::string(8 - s.size(), '0') + s;
        return ParseHex8(padded, out);
    }

    static u32 ParseDec(const std::string &s)
    {
        u32 v = 0;
        for (char c : s)
        {
            if (c < '0' || c > '9')
                break;
            v = v * 10 + (u32)(c - '0');
        }
        return v;
    }

    std::string FormatNumber(u32 v)
    {
        std::string digits = std::to_string(v);
        std::string out;
        int n = (int)digits.size();
        for (int i = 0; i < n; i++)
        {
            out += digits[i];
            int left = n - 1 - i;
            if (left > 0 && left % 3 == 0)
                out += ',';
        }
        return out;
    }

    // "$ min max default [match=HEX] [shl=N] [or=HEX] [qty=N] [at=10|16]"
    static void ParseValueLine(Cheat &c, const std::string &line)
    {
        std::vector<std::string> tok;
        size_t p = 1;
        while (p < line.size())
        {
            size_t a = line.find_first_not_of(" \t", p);
            if (a == std::string::npos) break;
            size_t b = line.find_first_of(" \t", a);
            tok.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
            p = (b == std::string::npos) ? line.size() : b;
        }
        if (tok.size() < 3)
            return;

        c.vmin  = ParseDec(tok[0]);
        c.vmax  = ParseDec(tok[1]);
        c.value = ParseDec(tok[2]);
        c.mode  = ValueMode::Match;

        for (size_t i = 3; i < tok.size(); i++)
        {
            const std::string &t = tok[i];
            size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string key = t.substr(0, eq), val = t.substr(eq + 1);
            if (key == "match") ParseHexAny(val, c.match);
            else if (key == "shl") c.shl = ParseDec(val);
            else if (key == "or")  ParseHexAny(val, c.orBits);
            else if (key == "qty") { c.qty = ParseDec(val); c.mode = ValueMode::Qty; }
            else if (key == "at")  c.at = ParseDec(val) == 16 ? 16 : 10;
        }
        if (c.value < c.vmin) c.value = c.vmin;
        if (c.value > c.vmax) c.value = c.vmax;
    }

    // ------------------------------------------------------------------
    // Parsing
    // ------------------------------------------------------------------

    void ParseCheatText(const std::string &text, std::vector<Cheat> &out, bool quick)
    {
        const size_t firstNew = out.size();
        Cheat *current = nullptr;
        bool   inNote = false;
        bool   inQuick = quick;
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
                current->quick = inQuick;
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

            // McM3 database markers around the quick picks block
            if (line == "@quick")     { inQuick = true;  current = nullptr; continue; }
            if (line == "@end-quick") { inQuick = quick; current = nullptr; continue; }

            if (!current)
                continue;

            if (line[0] == '$')
            {
                ParseValueLine(*current, line);
                continue;
            }

            if (line.compare(0, 6, "@from ") == 0)
            {
                current->copyFrom = Trim(line.substr(6));
                continue;
            }

            // Code line: "XXXXXXXX YYYYYYYY" (anything after is ignored)
            size_t sp = line.find_first_of(" \t");
            if (sp == std::string::npos)
                continue;
            std::string a = line.substr(0, sp);
            std::string rest = Trim(line.substr(sp));
            std::string b = rest.substr(0, rest.find_first_of(" \t"));

            CodeLine cl;
            if (ParseHex8(a, cl.left) && ParseHex8(b, cl.right))
                current->tmpl.push_back(cl);
        }

        // Resolve "@from": copy codes from the named cheat in the same text
        for (size_t i = firstNew; i < out.size(); i++)
        {
            Cheat &c = out[i];
            if (c.copyFrom.empty() || !c.tmpl.empty())
                continue;
            for (size_t j = firstNew; j < out.size(); j++)
            {
                if (j != i && out[j].name == c.copyFrom)
                {
                    c.tmpl = out[j].tmpl;
                    break;
                }
            }
        }

        // Drop headers that contain no code at all (section titles, unresolved @from)
        std::vector<Cheat> cleaned;
        cleaned.reserve(out.size());
        for (size_t i = 0; i < out.size(); i++)
        {
            if (i < firstNew || !out[i].tmpl.empty())
            {
                cleaned.push_back(std::move(out[i]));
                if (i >= firstNew)
                    ApplyValue(cleaned.back());
            }
        }
        out.swap(cleaned);
    }

    // ------------------------------------------------------------------
    // Adjustable values
    // ------------------------------------------------------------------

    // Is this word a bag entry carrying the default amount?  If so, swap in the new amount.
    static void PatchQty(const Cheat &c, u32 &w, u32 v)
    {
        const u32 bits = (c.at == 16) ? 16 : 10;
        const u32 mask = (1u << bits) - 1;
        const u32 low  = (1u << c.at) - 1;
        if (c.at + bits < 32 && (w >> (c.at + bits)) != 0)
            return;
        if (((w >> c.at) & mask) != c.qty)
            return;
        w = ((v & mask) << c.at) | (w & low);
    }

    void ApplyValue(Cheat &c)
    {
        c.lines = c.tmpl;
        if (!c.HasValue())
            return;

        const u32 v = c.value;
        size_t i = 0;
        while (i < c.lines.size())
        {
            CodeLine &l = c.lines[i];
            const u32 type = l.left >> 28;
            const bool isWrite = type <= 0x2 || (l.left >> 24) == 0xD5;

            if (type == 0xE)
            {
                // Patch block: the data words that follow may hold bag entries
                size_t dataLines = (l.right + 7) / 8;
                for (size_t k = 1; k <= dataLines && i + k < c.lines.size(); k++)
                {
                    CodeLine &d = c.lines[i + k];
                    if (c.mode == ValueMode::Qty)
                    {
                        PatchQty(c, d.left, v);
                        PatchQty(c, d.right, v);
                    }
                }
                i += dataLines + 1;
                continue;
            }

            if (isWrite)
            {
                if (c.mode == ValueMode::Match && l.right == c.match)
                    l.right = (v << c.shl) | c.orBits;
                else if (c.mode == ValueMode::Qty)
                    PatchQty(c, l.right, v);
            }
            i++;
        }
    }

    // ------------------------------------------------------------------
    // Game versions
    // ------------------------------------------------------------------

    // Finds "v1.N" / "V1.N" tags in a cheat name. Returns false if there are none.
    static bool VersionTags(const std::string &name, std::vector<int> &minors)
    {
        minors.clear();
        for (size_t i = 0; i + 3 < name.size(); i++)
        {
            char c = name[i];
            if ((c != 'v' && c != 'V') || (i > 0 && isalnum((unsigned char)name[i - 1])))
                continue;
            if (name[i + 1] != '1' || name[i + 2] != '.' || !isdigit((unsigned char)name[i + 3]))
                continue;
            size_t j = i + 3;
            int m = 0;
            while (j < name.size() && isdigit((unsigned char)name[j]))
                m = m * 10 + (name[j++] - '0');
            minors.push_back(m);
        }
        return !minors.empty();
    }

    // "Max Money, Game v1.0" -> "Max Money"   "Items x950 v1.1 & v1.2" -> "Items x950"
    static std::string StripVersion(const std::string &name)
    {
        std::string out;
        for (size_t i = 0; i < name.size(); i++)
        {
            char c = name[i];
            bool tag = (c == 'v' || c == 'V') && (i == 0 || !isalnum((unsigned char)name[i - 1]))
                       && i + 3 < name.size() + 0 && name[i + 1] == '1' && name[i + 2] == '.'
                       && isdigit((unsigned char)name[i + 3]);
            if (tag)
            {
                i += 3;
                while (i + 1 < name.size() && isdigit((unsigned char)name[i + 1]))
                    i++;
                continue;
            }
            out += c;
        }

        // Clean leftovers at the end: spaces, commas, "&", "Game", "for"
        bool again = true;
        while (again)
        {
            again = false;
            out = Trim(out);
            const char *tails[] = { "&", ",", "Game", "for", "(", "()" };
            for (const char *t : tails)
            {
                size_t n = strlen(t);
                if (out.size() > n && out.compare(out.size() - n, n, t) == 0)
                {
                    out.erase(out.size() - n);
                    again = true;
                }
            }
        }
        // Collapse double spaces
        std::string clean;
        for (char c : out)
            if (!(c == ' ' && !clean.empty() && clean.back() == ' '))
                clean += c;
        return clean.empty() ? name : clean;
    }

    static void FilterByVersion(GameInfo &game, std::vector<Cheat> &cheats)
    {
        std::vector<Cheat> kept;
        kept.reserve(cheats.size());
        std::vector<int> minors;

        for (auto &c : cheats)
        {
            bool tagged = VersionTags(c.name, minors);
            if (tagged && game.minor >= 0)
            {
                bool match = false;
                for (int m : minors)
                    if (m == game.minor)
                        match = true;
                if (!match)
                {
                    game.hiddenCount++;
                    continue;
                }
            }
            c.display = (tagged && game.minor >= 0) ? StripVersion(c.name) : c.name;
            kept.push_back(std::move(c));
        }
        cheats.swap(kept);
    }

    void LoadCheats(GameInfo &game, std::vector<Cheat> &out)
    {
        out.clear();
        game.sourcePath.clear();
        game.known = false;
        game.customCount = game.builtinCount = game.hiddenCount = 0;
        game.minor = -1;

        // 1. The user's own cheat file always comes first
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
                for (auto &c : out)
                    c.custom = true;
                game.customCount = (int)out.size();
                game.sourcePath = path;
                break;
            }
        }

        // 2. Built in cheats for this exact title (skip any the user's file already has)
        std::string dbName, dbText;
        u32 dbFlags = 0;
        if (FindBuiltin(game.titleId, dbName, dbText, dbFlags))
        {
            game.known = true;
            if (!dbName.empty())
                game.name = dbName;

            // Pokemon titles report updates as 1040 * N for game version 1.N
            if ((dbFlags & DB_POKEMON_VERSIONS) && game.version % 1040 == 0 && game.version / 1040 <= 9)
                game.minor = game.version / 1040;

            std::vector<Cheat> builtin;
            ParseCheatText(dbText, builtin);
            std::string().swap(dbText);   // free the text before building the final list

            out.reserve(out.size() + builtin.size());
            for (auto &c : builtin)
            {
                bool dup = false;
                for (const auto &u : out)
                    if (u.name == c.name)
                        dup = true;
                if (!dup)
                {
                    out.push_back(std::move(c));
                    game.builtinCount++;
                }
            }
        }

        FilterByVersion(game, out);

        game.customCount = game.builtinCount = 0;
        for (const auto &c : out)
            (c.custom ? game.customCount : game.builtinCount)++;

        // Order: quick picks, then the user's cheats, then everything else
        auto rank = [](const Cheat &c) { return c.quick ? 0 : (c.custom ? 1 : 2); };
        std::stable_sort(out.begin(), out.end(), [&](const Cheat &a, const Cheat &b)
        {
            return rank(a) < rank(b);
        });
    }

    // ------------------------------------------------------------------
    // Enabled state:  one line per enabled cheat, "<name>\t<value>"
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
        {
            if (!c.enabled)
                continue;
            body += c.name;
            if (c.HasValue())
                body += "\t" + std::to_string(c.value);
            body += "\n";
        }

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
            std::string line = text.substr(pos, end - pos);
            pos = end + 1;

            size_t tab = line.find('\t');
            std::string name = Trim(line.substr(0, tab));
            for (auto &c : cheats)
            {
                if (c.name != name)
                    continue;
                c.enabled = true;
                if (tab != std::string::npos && c.HasValue())
                {
                    u32 v = ParseDec(Trim(line.substr(tab + 1)));
                    if (v >= c.vmin && v <= c.vmax)
                    {
                        c.value = v;
                        ApplyValue(c);
                    }
                }
            }
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
