"""
Tiny LZ77 packer used for McM3's cheat database (decoder: Sources/Database.cpp).

Stream of sequences:
    token           u8   high 4 bits: literal count, low 4 bits: match length - 4
    [extra]         u8*  if a count is 15, add bytes until one is < 255
    literals        u8 * literal count
    offset          u16  little endian, 1..65535 (absent in the final sequence)
    [extra]         u8*  match length extension, same scheme

The last sequence only carries literals; the stream ends right after them.
"""

MIN_MATCH = 4
WINDOW = 0xFFFF
MAX_CHAIN = 32


def _put_len(out, n):
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)


def _emit(out, literals, match_len, offset):
    lit = len(literals)
    ml = match_len - MIN_MATCH if match_len else 0
    token = (min(lit, 15) << 4) | (min(ml, 15) if match_len else 0)
    out.append(token)
    if lit >= 15:
        _put_len(out, lit - 15)
    out += literals
    if match_len:
        out += bytes((offset & 0xFF, offset >> 8))
        if ml >= 15:
            _put_len(out, ml - 15)


def compress(data):
    data = bytes(data)
    n = len(data)
    out = bytearray()
    chains = {}
    i = 0
    lit_start = 0

    def insert(pos):
        if pos + MIN_MATCH <= n:
            chains.setdefault(data[pos:pos + MIN_MATCH], []).append(pos)

    while i + MIN_MATCH <= n:
        key = data[i:i + MIN_MATCH]
        best_len, best_off = 0, 0
        cands = chains.get(key)
        if cands:
            for p in reversed(cands[-MAX_CHAIN:]):
                off = i - p
                if off > WINDOW:
                    break
                length = MIN_MATCH
                while i + length < n and data[p + length] == data[i + length]:
                    length += 1
                if length > best_len:
                    best_len, best_off = length, off
        if best_len >= MIN_MATCH:
            _emit(out, data[lit_start:i], best_len, best_off)
            for k in range(i, i + best_len):
                insert(k)
            i += best_len
            lit_start = i
        else:
            insert(i)
            i += 1

    _emit(out, data[lit_start:], 0, 0)
    return bytes(out)


def decompress(packed):
    out = bytearray()
    i = 0
    n = len(packed)
    while i < n:
        token = packed[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = packed[i]
                i += 1
                lit += b
                if b != 255:
                    break
        out += packed[i:i + lit]
        i += lit
        if i >= n:
            break
        off = packed[i] | (packed[i + 1] << 8)
        i += 2
        ml = (token & 15)
        if ml == 15:
            while True:
                b = packed[i]
                i += 1
                ml += b
                if b != 255:
                    break
        ml += MIN_MATCH
        start = len(out) - off
        for k in range(ml):
            out.append(out[start + k])
    return bytes(out)
