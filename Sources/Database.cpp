#include "Database.hpp"

#include <CTRPluginFramework.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace CTRPluginFramework;

// McM3's cheat database is appended to the end of McM3.3gx (tools/pack.py), after the
// parts Luma loads into memory. Only the running game's section is read from the SD card,
// so the database costs no plugin memory. File end:
//
//     ...3gx... | database (tools/build_db.py) | "MCM3TAIL" u32 dbOffset u32 dbSize

namespace McM3
{
    struct Entry
    {
        u64 titleId;
        u32 offset;      // from the start of the database
        u32 packedSize;
        u32 rawSize;
        u32 flags;
    };

    static std::string g_error;
    static u32         g_count = 0;

    // Asks Luma's plugin loader where this plugin was loaded from (plg:ldr command 10)
    static std::string PluginPathFromLuma(void)
    {
        Handle handle = 0;
        if (R_FAILED(srvGetServiceHandle(&handle, "plg:ldr")))
            return "";

        static char path[256];
        memset(path, 0, sizeof(path));

        u32 *cmdbuf = getThreadCommandBuffer();
        cmdbuf[0] = IPC_MakeHeader(10, 0, 2);
        cmdbuf[1] = IPC_Desc_Buffer(255, IPC_BUFFER_RW);
        cmdbuf[2] = (u32)(uintptr_t)path;

        Result res = svcSendSyncRequest(handle);
        if (R_SUCCEEDED(res))
            res = cmdbuf[1];
        svcCloseHandle(handle);

        path[255] = 0;
        return R_SUCCEEDED(res) ? std::string(path) : std::string();
    }

    static bool ReadAt(File &file, u64 offset, void *dst, u32 size)
    {
        return file.Seek((s64)offset, File::SET) == File::SUCCESS && file.Read(dst, size) == File::SUCCESS;
    }

    // Opens the plugin file and finds the database inside it
    static bool OpenDatabase(File &file, u64 &dbStart, u32 &dbSize, u64 titleId)
    {
        std::vector<std::string> candidates;

        std::string fromLuma = PluginPathFromLuma();
        if (!fromLuma.empty())
            candidates.push_back(fromLuma);

        // Older Luma versions can't tell us the path: try the usual places
        char tidFolder[40];
        snprintf(tidFolder, sizeof(tidFolder), "/luma/plugins/%016llX", (unsigned long long)titleId);
        if (Directory::IsExists(tidFolder) == 1)
        {
            Directory dir(tidFolder);
            if (dir.IsOpen())
            {
                std::vector<std::string> files;
                dir.ListFiles(files, ".3gx");
                for (const auto &f : files)
                    candidates.push_back(std::string(tidFolder) + "/" + f);
            }
        }
        candidates.push_back("/luma/plugins/default.3gx");

        for (const auto &path : candidates)
        {
            if (File::Open(file, path, File::READ) != File::SUCCESS)
                continue;

            u64 size = file.GetSize();
            u8 tail[16];
            if (size > 16 && ReadAt(file, size - 16, tail, 16) && memcmp(tail, "MCM3TAIL", 8) == 0)
            {
                u32 off, len;
                memcpy(&off, tail + 8, 4);
                memcpy(&len, tail + 12, 4);
                if ((u64)off + len + 16 <= size)
                {
                    dbStart = off;
                    dbSize = len;
                    return true;
                }
            }
            file.Close();
        }
        return false;
    }

    // Decoder for tools/lz.py. Returns false on corrupt data.
    static bool Unpack(const u8 *in, u32 inSize, std::string &out, u32 rawSize)
    {
        out.clear();
        out.reserve(rawSize);
        u32 i = 0;

        auto readLen = [&](u32 &len) -> bool
        {
            if (len != 15)
                return true;
            while (true)
            {
                if (i >= inSize) return false;
                u8 b = in[i++];
                len += b;
                if (b != 255) return true;
            }
        };

        while (i < inSize)
        {
            u8 token = in[i++];
            u32 lit = token >> 4;
            if (!readLen(lit) || i + lit > inSize)
                return false;
            out.append((const char *)in + i, lit);
            i += lit;
            if (i >= inSize)
                break;

            if (i + 2 > inSize)
                return false;
            u32 off = in[i] | (in[i + 1] << 8);
            i += 2;
            u32 len = token & 15;
            if (!readLen(len))
                return false;
            len += 4;
            if (off == 0 || off > out.size() || out.size() + len > rawSize)
                return false;
            size_t start = out.size() - off;
            for (u32 k = 0; k < len; k++)
                out.push_back(out[start + k]);
        }
        return out.size() == rawSize;
    }

    bool FindBuiltin(u64 titleId, std::string &name, std::string &text, u32 &flags)
    {
        g_error.clear();

        File file;
        u64 dbStart = 0;
        u32 dbSize = 0;
        if (!OpenDatabase(file, dbStart, dbSize, titleId))
        {
            g_error = "built-in cheats not found in the plugin file";
            return false;
        }

        u8 header[12];
        if (!ReadAt(file, dbStart, header, 12) || memcmp(header, "MCM3DB01", 8) != 0)
        {
            g_error = "built-in cheat database is damaged";
            return false;
        }
        memcpy(&g_count, header + 8, 4);
        if (g_count == 0 || 12 + (u64)g_count * 24 > dbSize)
        {
            g_error = "built-in cheat database is damaged";
            return false;
        }

        // Binary search the index (sorted by title ID), one entry read at a time
        u32 lo = 0, hi = g_count;
        while (lo < hi)
        {
            u32 mid = (lo + hi) / 2;
            u8 raw[24];
            if (!ReadAt(file, dbStart + 12 + (u64)mid * 24, raw, 24))
                return false;

            Entry e;
            memcpy(&e.titleId,    raw,      8);
            memcpy(&e.offset,     raw + 8,  4);
            memcpy(&e.packedSize, raw + 12, 4);
            memcpy(&e.rawSize,    raw + 16, 4);
            memcpy(&e.flags,      raw + 20, 4);

            if (e.titleId == titleId)
            {
                if ((u64)e.offset + e.packedSize > dbSize || e.rawSize > 4 * 1024 * 1024)
                    return false;

                std::vector<u8> packed(e.packedSize);
                std::string unpacked;
                if (!ReadAt(file, dbStart + e.offset, packed.data(), e.packedSize)
                    || !Unpack(packed.data(), e.packedSize, unpacked, e.rawSize))
                {
                    g_error = "built-in cheat database is damaged";
                    return false;
                }
                packed.clear();
                packed.shrink_to_fit();

                // First line is the game's name, the rest is cheat text (no extra copy)
                size_t nl = unpacked.find('\n');
                name = unpacked.substr(0, nl);
                if (nl == std::string::npos)
                    unpacked.clear();
                else
                    unpacked.erase(0, nl + 1);
                text.swap(unpacked);
                flags = e.flags;
                return true;
            }
            if (e.titleId < titleId) lo = mid + 1;
            else hi = mid;
        }
        return false;   // game simply isn't in the database
    }

    u32 BuiltinGameCount(void)
    {
        return g_count;
    }

    const std::string &BuiltinError(void)
    {
        return g_error;
    }
}
