// SPDX-License-Identifier: CC0-1.0
// Builds the game's own pause.gfx (Help & Options and its Settings pages) with the additions in
// ui/advanced.as, for the library to load in its place:
//   1. find data/ui/pause/opt/pause.gfx in the player's Man_Gfx pack (Double Fine's DFPF v2:
//      a .~h index and a .~p data file) and inflate it (zlib), then again (Scaleform's CFX is a
//      zlib-compressed SWF);
//   2. insert the compiled ActionScript (advanced_action.h, a DoAction tag) at the end of the main
//      timeline's frame 1, after the game's own script, so it can wrap the game's functions;
//   3. write it uncompressed ("GFX" header) to a file.
// No game data is shipped: the movie is always made from the player's copy.
//
// Uses from the including file: FILE, fopen, fread, fwrite, fseek, fclose, malloc, free, memcpy,
// memcmp, strcmp, and a logf(const char *fmt, ...).
#ifndef PAUSEPATCH_H
#define PAUSEPATCH_H

#include "advanced_action.h"

// ---- inflate (RFC 1951) --------------------------------------------------------------------------------
typedef struct {
    const unsigned char *in;
    size_t in_len, in_pos;
    unsigned bits, nbits;
    unsigned char *out;
    size_t out_len, out_pos;
    int error;
} pp_inflate;

typedef struct {
    short count[16];   // codes of each length
    short symbol[320]; // symbols ordered by code
} pp_huffman;

static unsigned pp_bits(pp_inflate *s, int n) {
    while (s->nbits < (unsigned)n) {
        if (s->in_pos >= s->in_len) {
            s->error = 1;
            return 0;
        }
        s->bits |= (unsigned)s->in[s->in_pos++] << s->nbits;
        s->nbits += 8;
    }
    unsigned v = s->bits & ((1u << n) - 1);
    s->bits >>= n;
    s->nbits -= n;
    return v;
}

static void pp_build(pp_huffman *h, const short *lengths, int n) {
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int i = 0; i < n; i++)
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (short)i;
}

// Canonical Huffman decode, one bit at a time (codes are stored most significant bit first).
static int pp_decode(pp_inflate *s, const pp_huffman *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= (int)pp_bits(s, 1);
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->error = 1;
    return 0;
}

static void pp_put(pp_inflate *s, unsigned char c) {
    if (s->out_pos < s->out_len) s->out[s->out_pos] = c;
    else s->error = 1;
    s->out_pos++;
}

static void pp_codes(pp_inflate *s, const pp_huffman *lit, const pp_huffman *dist) {
    static const short kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                        6145, 8193, 12289, 16385, 24577};
    static const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int sym = pp_decode(s, lit);
        if (s->error) return;
        if (sym < 256) pp_put(s, (unsigned char)sym);
        else if (sym == 256) return;
        else {
            sym -= 257;
            if (sym >= 29) { s->error = 1; return; }
            int len = kLenBase[sym] + (int)pp_bits(s, kLenExtra[sym]);
            int d = pp_decode(s, dist);
            if (d >= 30) { s->error = 1; return; }
            size_t back = (size_t)kDistBase[d] + pp_bits(s, kDistExtra[d]);
            if (back > s->out_pos) { s->error = 1; return; }
            while (len--) pp_put(s, s->out_pos - back < s->out_len ? s->out[s->out_pos - back] : 0);
        }
        if (s->error) return;
    }
}

// Inflates a zlib stream (2-byte header, deflate data) into out[out_len]; returns bytes written,
// or -1.
static long pp_inflate_zlib(const unsigned char *in, size_t in_len, unsigned char *out, size_t out_len) {
    pp_inflate s = {in, in_len, 2, 0, 0, out, out_len, 0, 0};
    if (in_len < 2 || (in[0] & 0x0f) != 8) return -1;
    int last;
    do {
        last = (int)pp_bits(&s, 1);
        int type = (int)pp_bits(&s, 2);
        if (type == 0) {  // stored
            s.bits = 0, s.nbits = 0;
            if (s.in_pos + 4 > s.in_len) return -1;
            unsigned len = s.in[s.in_pos] | s.in[s.in_pos + 1] << 8;
            s.in_pos += 4;
            if (s.in_pos + len > s.in_len) return -1;
            while (len--) pp_put(&s, s.in[s.in_pos++]);
        } else if (type == 1) {  // fixed codes
            static pp_huffman lit, dist;
            static int built;
            if (!built) {
                short l[288];
                for (int i = 0; i < 288; i++) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                pp_build(&lit, l, 288);
                for (int i = 0; i < 30; i++) l[i] = 5;
                pp_build(&dist, l, 30);
                built = 1;
            }
            pp_codes(&s, &lit, &dist);
        } else if (type == 2) {  // dynamic codes
            static const unsigned char kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            int nlen = (int)pp_bits(&s, 5) + 257, ndist = (int)pp_bits(&s, 5) + 1, ncode = (int)pp_bits(&s, 4) + 4;
            short lengths[320];
            memset(lengths, 0, sizeof lengths);
            for (int i = 0; i < ncode; i++) lengths[kOrder[i]] = (short)pp_bits(&s, 3);
            pp_huffman lencode, lit, dist;
            pp_build(&lencode, lengths, 19);
            int i = 0;
            while (i < nlen + ndist && !s.error) {
                int sym = pp_decode(&s, &lencode), rep = 0;
                short v = 0;
                if (sym < 16) {
                    lengths[i++] = (short)sym;
                    continue;
                }
                if (sym == 16) {
                    if (!i) return -1;
                    v = lengths[i - 1], rep = 3 + (int)pp_bits(&s, 2);
                } else if (sym == 17) rep = 3 + (int)pp_bits(&s, 3);
                else rep = 11 + (int)pp_bits(&s, 7);
                if (i + rep > nlen + ndist) return -1;
                while (rep--) lengths[i++] = v;
            }
            pp_build(&lit, lengths, nlen);
            pp_build(&dist, lengths + nlen, ndist);
            pp_codes(&s, &lit, &dist);
        } else return -1;
        if (s.error) return -1;
    } while (!last);
    return (long)s.out_pos;
}

// ---- the pack ------------------------------------------------------------------------------------------
static unsigned long long pp_be64(const unsigned char *p) {
    unsigned long long v = 0;
    for (int i = 0; i < 8; i++) v = v << 8 | p[i];
    return v;
}

// Reads `name` from a DFPF v2 pack (`index` is the .~h path; the data is the same path ending in
// .~p). Returns malloc'ed data and its size, or NULL.
static unsigned char *pp_read_pack(const char *index, const char *name, size_t *size) {
    unsigned char *h = NULL, *out = NULL, *raw = NULL;
    FILE *f = fopen(index, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 20, n = 0;
    h = (unsigned char *)malloc(cap);
    for (size_t r; h && (r = fread(h + n, 1, cap - n, f)) > 0;) {
        n += r;
        if (n == cap) {
            unsigned char *g = (unsigned char *)malloc(cap * 2);
            if (g) memcpy(g, h, n);
            free(h);
            h = g, cap *= 2;
        }
    }
    fclose(f);
    if (!h || n < 0x24 || memcmp(h, "dfpf", 4) || h[4] != 2) goto done;
    unsigned long long names = pp_be64(h + 16);
    unsigned count = (unsigned)h[0x20] << 24 | h[0x21] << 16 | h[0x22] << 8 | h[0x23];
    if (names > n || (unsigned long long)count * 16 > names) goto done;
    const unsigned char *ents = h + names - (size_t)count * 16;
    for (unsigned i = 0; i < count; i++) {
        unsigned long long a = pp_be64(ents + 16 * i), b = pp_be64(ents + 16 * i + 8);
        unsigned lo = (unsigned)b, name_off = lo >> 11;
        if (names + name_off >= n || strcmp((const char *)h + names + name_off, name)) continue;
        size_t usize = (size_t)(a >> 41), csize = (size_t)((a & 0xffffffffu) >> 1);
        long off = (long)((b >> 32) >> 3);
        char data[600];
        size_t len = strlen(index);
        if (len >= sizeof data) goto done;
        memcpy(data, index, len + 1);
        data[len - 1] = 'p';
        if (!(f = fopen(data, "rb"))) goto done;
        raw = (unsigned char *)malloc(csize);
        int ok = raw && fseek(f, off, 0) == 0 && fread(raw, 1, csize, f) == csize;
        fclose(f);
        if (!ok || !(out = (unsigned char *)malloc(usize))) goto done;
        if ((lo & 0xf) == 4) {
            if (pp_inflate_zlib(raw, csize, out, usize) != (long)usize) free(out), out = NULL;
        } else if (csize == usize) memcpy(out, raw, usize);
        else free(out), out = NULL;
        if (out) *size = usize;
        goto done;
    }
done:
    free(h);
    free(raw);
    return out;
}

// ---- the movie -----------------------------------------------------------------------------------------
// Offset of the first tag after a SWF header (signature, version, length, RECT, rate, count).
static size_t pp_body(const unsigned char *d) { return 8 + ((5 + 4 * (d[8] >> 3) + 7) / 8) + 4; }

static size_t pp_tag(const unsigned char *d, size_t p, int *code, size_t *hl) {
    unsigned h = d[p] | d[p + 1] << 8, len = h & 63;
    *code = (int)(h >> 6), *hl = 2;
    if (len == 63) len = d[p + 2] | d[p + 3] << 8 | d[p + 4] << 16 | (unsigned)d[p + 5] << 24, *hl = 6;
    return len;
}

// Writes the patched pause.gfx to `out_path` from the pack at `pack_index`. Returns 1 on success.
static int pp_build_pause(const char *pack_index, const char *out_path) {
    size_t cfx_size = 0;
    unsigned char *cfx = pp_read_pack(pack_index, "data/ui/pause/opt/pause.gfx", &cfx_size), *swf = NULL;
    int ok = 0;
    if (!cfx || cfx_size < 8 || memcmp(cfx + 1, "FX", 2)) {
        logf("pause.gfx: not found in %s", pack_index);
        goto done;
    }
    // The header's length isn't reliable in these movies (pause.gfx says 112820 and inflates to
    // 114696), so inflate into plenty of room and use what comes out.
    size_t size = cfx_size, room = cfx_size * 8 + (1 << 20);
    if (!(swf = (unsigned char *)malloc(room + sizeof kAdvancedAction))) goto done;
    memcpy(swf, cfx, 8);
    if (cfx[0] == 'C') {
        long n = pp_inflate_zlib(cfx + 8, cfx_size - 8, swf + 8, room - 8);
        if (n < 0) {
            logf("pause.gfx: could not inflate");
            goto done;
        }
        size = 8 + (size_t)n;
    } else if (cfx[0] == 'G') memcpy(swf, cfx, size);
    else goto done;
    // The end of frame 1: the first ShowFrame of the main timeline.
    size_t p = pp_body(swf), at = 0;
    while (p + 2 <= size) {
        int code;
        size_t hl, len = pp_tag(swf, p, &code, &hl);
        if (code == 1) { at = p; break; }
        if (code == 0) break;
        p += hl + len;
    }
    if (!at) {
        logf("pause.gfx: frame 1 not found");
        goto done;
    }
    memmove(swf + at + sizeof kAdvancedAction, swf + at, size - at);
    memcpy(swf + at, kAdvancedAction, sizeof kAdvancedAction);
    size += sizeof kAdvancedAction;
    swf[0] = 'G', swf[1] = 'F', swf[2] = 'X';  // uncompressed
    swf[4] = (unsigned char)size, swf[5] = (unsigned char)(size >> 8);
    swf[6] = (unsigned char)(size >> 16), swf[7] = (unsigned char)(size >> 24);
    FILE *f = fopen(out_path, "wb");
    if (f) {
        ok = fwrite(swf, 1, size, f) == size;
        fclose(f);
    }
    logf(ok ? "pause.gfx: Advanced page additions ready (%s)" : "pause.gfx: could not write %s", out_path);
done:
    free(cfx);
    free(swf);
    return ok;
}

#endif
