/* custompaint.c -- a paint of any colour: a part whose key says its colour.
 *
 * A paint is a part of the car part database (paints.c), and a car has its parts by key: in a profile, in the car record
 * friends get (roam_game.c car_cfg).  A key here is a tag byte and 24 bits of RGB, the tag saying which paint it is like
 * (k_tag).  No such part is in the database, whose lists (the shops', the AI cars') stay as they are: asked for one
 * (NewGetCarPart, 0x61B980) we answer with a record of our own, a copy of the tag's real paint with the key put in, and
 * asked for that record's attributes (0x60FEF0) we answer as for the real paint, but for RED, GREEN and BLUE.
 *
 * The tags are top bytes no paint or vinyl colour of the game's has (tools: over the 450 of them in GLOBALB.BUN), and
 * count only in a slot of those kinds, so no other part is ever taken for one.  A record once made stays that colour
 * (a car on screen holds its pointer) until the table is full: then the oldest goes, which at worst shows one old colour
 * wrong until that car is next built.
 *
 * The Custom tabs: the Graphics shop's paint screen (UIPaint, the grids of the body's paint, the rims', hoses',
 * mufflers', calipers' and the vinyls' colours) pages with its shoulder messages through Gloss, Pearl and Metallic
 * (the body) or keeps its one list; here it pages on to Custom Gloss and Custom Metallic (the body) or Custom.  A
 * custom tab is the same grid of 3 x 20 swatches (+0x178 its width), its rows the colour's hue, saturation and
 * brightness, 20 steps each: each swatch is the colour you get by stopping on it, so moving along a row changes that
 * one and the other two rows follow.  The grid's parts are 60 records of our own whose colours change; the one shown
 * on the car, priced and bought is always the made one of that colour (the preview, 0x55C1F0, swaps it in). */
#include "cpu.h"
#include "guest.h"
#include "custompaint.h"
#include <stdio.h>
#include <string.h>

uint32_t gheap_alloc(int heap, uint32_t n, int zero);
const char *paints_name(int list);                 /* paints.c */

#define G_CARPARTDB   0x008a2b68u
#define G_SLOTKIND    0x00802960u                  /* a ride slot's part kind */
#define FN_GETCARPART 0x0061b980u
#define PART_SZ       14u
#define REC_SZ        40u                          /* the part, 2 bytes to spare, its three attribute rows */
#define REC_ROWS      16u
#define N_REC         2048u
#define N_HASH        (N_REC * 4u)
#define N_GRID        60u
#define N_ALLREC      (N_REC + N_GRID)
#define SCRATCH       (N_ALLREC * REC_SZ)          /* 64 bytes past them: strings for the game */

static const struct { uint8_t tag; int list; } k_tag[CP_N] = {   /* paints.c's lists */
    { 0xf0u, 0 }, { 0xf1u, 1 }, { 0xf3u, 2 }, { 0xf4u, 3 }, { 0xf5u, 4 }, { 0xf6u, 5 }, { 0xf7u, 6 },
};
static uint32_t g_base, g_next, g_tmpl[N_ALLREC], g_key[N_ALLREC], g_hash[N_HASH];   /* g_hash: index + 1 */
static uint32_t g_hred, g_hgrn, g_hblu, g_hdr, g_hdg, g_hdb;

static uint32_t bhash(const char *s){ uint32_t h = 0xffffffffu; for (; *s; s++) h = h * 33u + (uint8_t)*s; return h; }
static int tag_of(uint32_t key){
    for (int i = 0; i < CP_N; i++) if (k_tag[i].tag == key >> 24) return i;
    return -1;
}
uint32_t custompaint_make(int type, uint32_t rgb){ return (uint32_t)k_tag[type].tag << 24 | (rgb & 0xffffffu); }

static int paint_slot(uint32_t slot){
    if (slot >= 0xaau) return 0;
    uint32_t k = MR32(G_SLOTKIND + 4u * slot);
    return k == 0x30u || k == 0x31u;
}
int custompaint_is(uint32_t slot, uint32_t key){ return tag_of(key) >= 0 && paint_slot(slot); }

static uint32_t hash_at(uint32_t key){
    uint32_t h = key * 0x9e3779b1u;
    return (h ^ h >> 15) & (N_HASH - 1u);
}
static void forget(uint32_t i){            /* linear probing: the entries after it put back */
    uint32_t h = hash_at(g_key[i]);
    while (g_hash[h] != i + 1u) h = (h + 1u) & (N_HASH - 1u);
    g_hash[h] = 0;
    for (uint32_t n = (h + 1u) & (N_HASH - 1u); g_hash[n]; n = (n + 1u) & (N_HASH - 1u)){
        uint32_t e = g_hash[n] - 1u, w = hash_at(g_key[e]);
        g_hash[n] = 0;
        while (g_hash[w]) w = (w + 1u) & (N_HASH - 1u);
        g_hash[w] = e + 1u;
    }
    g_tmpl[i] = 0;
}
static int ready(void){
    if (g_base) return 1;
    if (!(g_base = gheap_alloc(1, SCRATCH + 64u, 1))){ fprintf(stderr, "[custompaint] no memory\n"); return 0; }
    g_hred = bhash("RED"); g_hgrn = bhash("GREEN"); g_hblu = bhash("BLUE");
    g_hdr = bhash("DISPRED"); g_hdg = bhash("DISPGREEN"); g_hdb = bhash("DISPBLUE");
    return 1;
}
/* the real paint a key's tag is like, for this car type and slot (0: this slot takes no such paint) */
static uint32_t template_of(CPU *c, uint32_t type, uint32_t slot, int t){
    uint32_t a[5] = { type, slot, bhash(paints_name(k_tag[t].list)), 0, 0xffffffffu };
    return gcall(c, FN_GETCARPART, G_CARPARTDB, 5, a);
}
static void fill(uint32_t i, uint32_t tmpl, uint32_t key){
    uint32_t r = g_base + i * REC_SZ;
    g_tmpl[i] = tmpl; g_key[i] = key;
    memcpy(&MR8(r), &MR8(tmpl), PART_SZ);
    MR16(r) = (uint16_t)key; MR16(r + 2u) = (uint16_t)(key >> 16);
    const uint32_t col[3] = { key >> 16 & 255u, key >> 8 & 255u, key & 255u }, nm[3] = { g_hred, g_hgrn, g_hblu };
    for (uint32_t j = 0; j < 3u; j++){ MR32(r + REC_ROWS + 8u * j) = nm[j]; MR32(r + REC_ROWS + 8u * j + 4u) = col[j]; }
}
static uint32_t intern(CPU *c, uint32_t type, uint32_t slot, uint32_t key){
    if (!ready()) return 0;
    for (uint32_t h = hash_at(key); g_hash[h]; h = (h + 1u) & (N_HASH - 1u))
        if (g_key[g_hash[h] - 1u] == key) return g_base + (g_hash[h] - 1u) * REC_SZ;
    uint32_t tmpl = template_of(c, type, slot, tag_of(key));
    if (!tmpl) return 0;
    uint32_t i = g_next++ % N_REC, r = g_base + i * REC_SZ;
    if (g_tmpl[i]){
        forget(i);
        static int said;
        if (said++ < 4) fprintf(stderr, "[custompaint] the table is full: the oldest colours go\n");
    }
    fill(i, tmpl, key);
    uint32_t h = hash_at(key);
    while (g_hash[h]) h = (h + 1u) & (N_HASH - 1u);
    g_hash[h] = i + 1u;
    return r;
}

/* NewGetCarPart for one of ours (custompaint_is): thiscall (type, slot, key, previous, level), ret 0x14 */
void custompaint_get(CPU *restrict c){
    uint32_t e = c->esp, type = MR32(e + 4u), slot = MR32(e + 8u), key = MR32(e + 12u), prev = MR32(e + 16u);
    uint32_t p = prev ? 0u : intern(c, type, slot, key);
    c->eax = p; POP32(c->eip); c->esp += 0x14u;
}

/* a part's attribute (0x60FEF0, thiscall (name hash, previous), the row {hash, value} or 0) */
void sub_0060fef0_orig(CPU *c);
void sub_0060fef0(CPU *restrict c){
    uint32_t p = c->ecx;
    if (!g_base || p < g_base || p >= g_base + N_ALLREC * REC_SZ){ sub_0060fef0_orig(c); return; }
    uint32_t i = (p - g_base) / REC_SZ, h = MR32(c->esp + 4u);
    c->ecx = g_tmpl[i] ? g_tmpl[i] : p;
    sub_0060fef0_orig(c);
    int at = h == g_hred || h == g_hdr ? 0 : h == g_hgrn || h == g_hdg ? 1 : h == g_hblu || h == g_hdb ? 2 : -1;
    if (c->eax && at >= 0) c->eax = p + REC_ROWS + 8u * (uint32_t)at;
}

/* ---- the Custom tabs ------------------------------------------------------------------------------------------- */
#define VT_UIPAINT    0x0079d8ecu
#define G_CUSTOMIZE   0x00838980u                  /* the shop: +0x50 the ride being dressed */
#define FN_PART_ATTR  0x0061b500u                  /* thiscall part (name hash, default) */
#define FN_FE_SETSTR  0x00535fe0u
#define M_NEXT        0x5073ef13u                  /* the tabs: Gloss -> Pearl -> Metallic -> */
#define M_PREV        0xd9feec59u
#define B_GLOSS       0x02daab07u
#define B_PEARL       0x03797533u
#define B_METAL       0x03437a52u
#define CB_GLOSS      0xc0570001u                  /* our tabs (a brand no part has) */
#define CB_METAL      0xc0570002u
#define CB_OTHER      0xc0570003u
#define U_SLOT(s)     MR32(MR32((s) + 0x58u) + 0x48u)
#define U_BRAND       0x174u
#define U_FIRST       0x158u                       /* the grid's list: {next, prev, part at +8} */
#define U_SEL         0x168u
#define U_WIDTH       0x178u
#define U_TITLE       0x16cu
#define STEPS         20

static uint32_t g_other, g_want, g_scr;            /* the slot's own brand; the tab a rebuild is to show; the screen */
static int g_h = 15, g_s = STEPS - 1, g_v = STEPS - 1, g_focus = 15, g_dirty, g_busy;

static int custom(uint32_t b){ return b == CB_GLOSS || b == CB_METAL || b == CB_OTHER; }
static uint32_t real_brand(uint32_t b){ return b == CB_GLOSS ? B_GLOSS : b == CB_METAL ? B_METAL : g_other; }
static int tag_of_brand(uint32_t b){
    switch (b){
    case B_GLOSS: return CP_GLOSS;      case B_METAL: return CP_METAL;      case 0xda27u: return CP_RIM;
    case 0x16bdceu: return CP_HOSE;     case 0x66350f0u: return CP_MUFFLER; case 0xd6640dffu: return CP_CALIPER;
    case 0x3e871f1u: return CP_VINYL;   default: return -1;
    }
}
static int screen(uint32_t s){ return s && guest_ok(s, 0x190u) && MR32(s) == VT_UIPAINT && MR32(s + 0x58u) && guest_ok(MR32(s + 0x58u), 0x4cu); }

static uint32_t hsv(int h, int s, int v){             /* steps -> 0xRRGGBB */
    float H = (float)h * 6.0f / STEPS, S = (float)s / (STEPS - 1), V = (float)v / (STEPS - 1);
    int i = (int)H; float f = H - (float)i, p = V * (1 - S), q = V * (1 - S * f), t = V * (1 - S * (1 - f)), r, g, b;
    switch (i % 6){ case 0: r = V; g = t; b = p; break; case 1: r = q; g = V; b = p; break; case 2: r = p; g = V; b = t; break;
                    case 3: r = p; g = q; b = V; break; case 4: r = t; g = p; b = V; break; default: r = V; g = p; b = q; }
    return (uint32_t)(r * 255.0f + 0.5f) << 16 | (uint32_t)(g * 255.0f + 0.5f) << 8 | (uint32_t)(b * 255.0f + 0.5f);
}
static void from_rgb(uint32_t r, uint32_t g, uint32_t b){
    float R = r / 255.0f, G = g / 255.0f, B = b / 255.0f, mx = R > G ? (R > B ? R : B) : (G > B ? G : B);
    float mn = R < G ? (R < B ? R : B) : (G < B ? G : B), d = mx - mn, h = 0;
    if (d > 0){ h = mx == R ? (G - B) / d : mx == G ? 2 + (B - R) / d : 4 + (R - G) / d; if (h < 0) h += 6; }
    g_h = (int)(h / 6.0f * STEPS + 0.5f) % STEPS;
    g_s = mx > 0 ? (int)(d / mx * (STEPS - 1) + 0.5f) : 0;
    g_v = (int)(mx * (STEPS - 1) + 0.5f);
    if (g_s == 0 && g_v == 0) g_v = STEPS - 1, g_s = STEPS - 1;   /* black's hue row would be all black: start lit */
}
/* swatch i of a grid `w` wide: its colour */
static uint32_t colour_of(int i, int w){
    int row = i / w, k = i % w;
    return row == 0 ? hsv(k, g_s, g_v) : row == 1 ? hsv(g_h, k, g_v) : hsv(g_h, g_s, k);
}
static int width(uint32_t s){ int w = (int)MR32(s + U_WIDTH); return w == STEPS ? w : STEPS; }
static uint32_t ride_type(void){ uint32_t r = G_CUSTOMIZE + 0x50u; return MR32(r); }

/* the grid's parts (0x5449C0, cdecl (slot, list, brand, 0)): for one of our tabs, the real brand's list with our parts */
void sub_005449c0_orig(CPU *c);
void sub_005449c0(CPU *restrict c){
    uint32_t slot = MR32(c->esp + 4u), list = MR32(c->esp + 8u), brand = MR32(c->esp + 12u);
    if (!custom(brand) || !ready()){ sub_005449c0_orig(c); return; }
    int t = tag_of_brand(real_brand(brand));
    MR32(c->esp + 12u) = real_brand(brand);
    sub_005449c0_orig(c);
    MR32(c->esp + 12u) = brand;
    uint32_t type = ride_type(), tmpl = t >= 0 ? template_of(c, type, slot, t) : 0u;
    int w = screen(g_scr) ? width(g_scr) : STEPS, i = 0;
    for (uint32_t n = MR32(list); tmpl && n && n != list && guest_ok(n, 12u) && i < (int)N_GRID; n = MR32(n), i++){
        uint32_t key = custompaint_make(t, colour_of(i, w)), p = 0;
        if (i == g_focus) p = intern(c, type, slot, key);
        if (!p){ fill(N_REC + (uint32_t)i, tmpl, key); p = g_base + (N_REC + (uint32_t)i) * REC_SZ; }
        MR32(n + 8u) = p;
    }
    if (!tmpl) fprintf(stderr, "[custompaint] slot 0x%x takes no %s: the tab is the game's list\n", slot, t >= 0 ? paints_name(k_tag[t].list) : "paint");
}

/* the grid built again (0x548630, thiscall screen): to the tab asked for; the focus on our swatch */
void sub_00548630_orig(CPU *c);
void sub_00548630(CPU *restrict c){
    uint32_t s = c->ecx;
    if (!screen(s)){ sub_00548630_orig(c); return; }
    g_scr = s;
    if (g_want) MR32(s + U_BRAND) = g_want;
    sub_00548630_orig(c);
    if (!custom(MR32(s + U_BRAND))) return;
    int i = 0;
    for (uint32_t n = MR32(s + U_FIRST); n && n != s + U_FIRST && guest_ok(n, 12u); n = MR32(n), i++)
        if (i == g_focus){ MR32(s + U_SEL) = n; break; }
}

/* the colour now on the slot, as the tabs open */
static void start_from(CPU *c, uint32_t slot){
    uint32_t ride = G_CUSTOMIZE + 0x50u, p = slot < 0xaau ? MR32(ride + 0x590u + 4u * slot) : 0u;
    if (!p || !guest_ok(p, PART_SZ)) return;
    uint32_t a[3][2] = { { g_hred, 0 }, { g_hgrn, 0 }, { g_hblu, 0 } }, v[3];
    for (int i = 0; i < 3; i++) v[i] = gcall(c, FN_PART_ATTR, p, 2, a[i]);
    from_rgb(v[0] & 255u, v[1] & 255u, v[2] & 255u);
    g_focus = g_h;                                  /* on the hue row */
}

/* the screen's messages (0x55D0D0, thiscall (message), ret 4): the tabs paged on to ours.  The page-turn itself is
 * the game's (it builds the grid again, puts the focus, the preview and the price right): asked from Gloss, so it
 * turns, with the tab we want put in as the grid is built (g_want) */
void sub_0055d0d0_orig(CPU *c);
/* the game's page-turn, asked from Gloss so it turns, with tab `to` put in as the grid is built (g_want) */
static void rebuild(CPU *c, uint32_t s, uint32_t to){
    uint32_t a = M_NEXT;
    g_want = to; g_busy++;
    MR32(s + U_BRAND) = B_GLOSS;
    gcall(c, 0x0055d0d0u, s, 1, &a);
    g_want = 0; g_busy--;
}
void sub_0055d0d0(CPU *restrict c){
    uint32_t s = c->ecx, msg = MR32(c->esp + 4u);
    if (g_busy || !screen(s) || (msg != M_NEXT && msg != M_PREV)){ sub_0055d0d0_orig(c); return; }
    uint32_t slot = U_SLOT(s), cur = MR32(s + U_BRAND);
    /* the slots painted like the body (0x50F5B0 gives them Gloss: base, spoiler, roof, mirror) get its five tabs */
    int full = slot == 0x3fu || slot == 0x45u || slot == 0x4bu || slot == 0x4cu;
    static const uint32_t body[5] = { B_GLOSS, B_PEARL, B_METAL, CB_GLOSS, CB_METAL };
    uint32_t other[2] = { custom(cur) ? g_other : cur, CB_OTHER };
    const uint32_t *tabs = full ? body : other;
    int n = full ? 5 : 2, at = -1;
    for (int i = 0; i < n; i++) if (tabs[i] == cur) at = i;
    if (at < 0 || (!full && tag_of_brand(cur == CB_OTHER ? g_other : cur) < 0)){ sub_0055d0d0_orig(c); return; }
    uint32_t to = tabs[(at + (msg == M_NEXT ? 1 : n - 1)) % n];
    if (!custom(cur) && !custom(to)){ sub_0055d0d0_orig(c); return; }
    if (!custom(cur)){ if (!full) g_other = cur; start_from(c, slot); }
    g_dirty = 0;
    rebuild(c, s, to);
    POP32(c->eip); c->esp += 4u;
    fprintf(stderr, "[custompaint] slot 0x%x: tab %08x -> %08x (hue %d, saturation %d, brightness %d)\n", slot, cur, to, g_h, g_s, g_v);
}

/* the screen's messages (0x563410, its method 2, thiscall (message, object, a3, a4), ret 0x10): its tab buttons go to
 * the page-turn (0x55D0D0, 0x563A5A) only once the grid is built (+0x188) and while the tab is Gloss, Pearl or
 * Metallic, so a slot of another brand (rims, spinners, brakes, engine, exhaust: Powder Coat and the like) had no
 * tabs, and none of ours could be left by them.  Here they turn for every brand with a custom tab, and for ours. */
#define U_READY       0x188u
#define FN_FE_SOUND   0x0050d150u                  /* cdecl (screen's FE object, sound, 0xb): 4 next, 5 previous */
void sub_00563410_orig(CPU *c);
void sub_00563410(CPU *restrict c){
    uint32_t s = c->ecx, msg = MR32(c->esp + 4u);
    if (msg != M_NEXT && msg != M_PREV){ sub_00563410_orig(c); return; }
    static int said;
    if (said++ < 40) fprintf(stderr, "[custompaint] tab %s: screen %d, grid %d, slot 0x%x, brand %08x\n", msg == M_NEXT ? "next" : "previous",
                             screen(s), screen(s) ? MR8(s + U_READY) : -1, screen(s) ? U_SLOT(s) : 0u, screen(s) ? MR32(s + U_BRAND) : 0u);
    if (!screen(s) || !MR8(s + U_READY)){ sub_00563410_orig(c); return; }
    uint32_t b = MR32(s + U_BRAND);
    if (b == B_GLOSS || b == B_PEARL || b == B_METAL || (!custom(b) && tag_of_brand(b) < 0)){ sub_00563410_orig(c); return; }
    uint32_t a = msg, snd[3] = { MR32(s + 4u), msg == M_NEXT ? 4u : 5u, 0xbu };
    gcall(c, 0x0055d0d0u, s, 1, &a);
    gcall(c, FN_FE_SOUND, 0, 3, snd);
    POP32(c->eip); c->esp += 0x10u;
}

/* the screen's moves (0x55D2D0, thiscall (message), ret 4): within a custom tab, the other rows after it */
void sub_0055d2d0_orig(CPU *c);
void sub_0055d2d0(CPU *restrict c){
    uint32_t s = c->ecx;
    g_dirty = 0;
    sub_0055d2d0_orig(c);
    if (!g_busy && screen(s) && g_dirty && custom(MR32(s + U_BRAND))){ g_dirty = 0; rebuild(c, s, MR32(s + U_BRAND)); }
}

/* the preview (0x55C1F0, thiscall shop (slot, part), ret 8): one of the grid's records -> the made part of its colour,
 * also in the grid's list (what is priced and bought), and that row's step taken */
void sub_0055c1f0_orig(CPU *c);
void sub_0055c1f0(CPU *restrict c){
    uint32_t slot = MR32(c->esp + 4u), p = MR32(c->esp + 8u), lo = g_base + N_REC * REC_SZ;
    if (!g_base || p < lo || p >= lo + N_GRID * REC_SZ){ sub_0055c1f0_orig(c); return; }
    int i = (int)((p - lo) / REC_SZ), w = screen(g_scr) ? width(g_scr) : STEPS, row = i / w, k = i % w;
    if (row == 0) g_h = k; else if (row == 1) g_s = k; else g_v = k;
    if (g_focus != i) g_dirty = 1;
    g_focus = i;
    uint32_t q = intern(c, ride_type(), slot, g_key[N_REC + (uint32_t)i]);
    static int said;
    if (said++ < 64) fprintf(stderr, "[custompaint] slot 0x%x swatch %d: %06x (hue %d, saturation %d, brightness %d), part 0x%08x\n",
                             slot, i, g_key[N_REC + (uint32_t)i] & 0xffffffu, g_h, g_s, g_v, q);
    if (q){
        MR32(c->esp + 8u) = q;
        if (screen(g_scr))
            for (uint32_t n = MR32(g_scr + U_FIRST); n && n != g_scr + U_FIRST && guest_ok(n, 12u); n = MR32(n))
                if (MR32(n + 8u) == p) MR32(n + 8u) = q;
    }
    sub_0055c1f0_orig(c);
}

/* the screen drawn (0x5484E0, its method 4): our tab's name over the brand's */
void sub_005484e0_orig(CPU *c);
void sub_005484e0(CPU *restrict c){
    uint32_t s = c->ecx;
    sub_005484e0_orig(c);
    if (!screen(s) || !custom(MR32(s + U_BRAND)) || !g_base) return;
    uint32_t obj = MR32(s + U_TITLE), str = g_base + SCRATCH;
    if (!obj || !guest_ok(obj, 0x20u)) return;
    uint32_t b = MR32(s + U_BRAND);
    guest_write_str(str, b == CB_GLOSS ? "CUSTOM GLOSS" : b == CB_METAL ? "CUSTOM METALLIC" : "CUSTOM", 64);
    gcall(c, FN_FE_SETSTR, obj, 1, &str);
    MR32(obj + 0x1cu) |= 2u;
}
