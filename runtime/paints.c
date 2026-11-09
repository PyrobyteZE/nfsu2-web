/* paints.c -- this server's paint colours, in the Graphics shops' grids of 60 swatches (Gloss, Metallic) and the grids of
 * the rims, engine hoses, muffler tips, brake calipers and vinyls' colours (kind 0x31, without a material), which take no more than 60: a paint is a part (kind 0x30) whose colour is its RED, GREEN and BLUE attributes and
 * whose shine is a light material the game has (the attribute 0x6BA02C05, by name hash).  Our colours go on the paints
 * nearest another of the game's (tools: distance in Lab), so a part's key stays the game's and a profile needs nothing
 * new.  The last four lists' 20 take plain slots only (the rest are pearl paints, some with a second colour, or chrome);
 * vinylpacks.c hands this the chunk: attributes are shared between parts, so the changed parts get rows of their own. */
#include "cpu.h"
#include "guest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t gheap_alloc(int heap, uint32_t n, int zero);
void gheap_free(int heap, uint32_t p);

#define CHUNK_PARTS   0x80034602u
#define PART_SZ       14u
#define KIND_PAINT    0x30u
#define ATTR_N        6             /* the attributes of a plain paint */
#define ATTR_MATERIAL 0x6ba02c05u
#define N_COLOUR      30
#define N_RIM         20
#define N_PAINTS      (N_COLOUR + N_COLOUR + 4 * N_RIM)  /* those with a material */
#define N_ALL         (N_PAINTS + N_COLOUR)          /* and the vinyls' colours, which have none */
#define KIND_VINYL_COLOUR 0x31u
#define ATTR_N_VINYL  5

static const uint32_t SUB[7] = { 0x34603u, 0x34606u, 0x3460cu, 0x34605u, 0x3460au, 0x3460bu, 0x34604u };
enum { S_HDR, S_STR, S_LIST, S_ATTR, S_MODEL, S_OWNER, S_PART };

/* the colours, and the materials of the game's paints nearest them (gloss, metallic) */
static const struct { uint8_t r, g, b; const char *gloss, *metal; } k_colour[N_COLOUR] = {
    {  75,   0, 160, "REGPAINTBLUE",   "METPAINTBLUE" },   /* royal purple */
    {  58,   0, 120, "REGPAINTBLUE",   "METPAINTBLUE" },   /* deep violet */
    { 110,  30, 150, "REGPAINTBLUE",   "METPAINTRED" },    /* grape */
    { 150,  70, 200, "REGPAINTBLUE",   "METPAINTBLUE" },   /* amethyst */
    { 180, 140, 230, "REGPAINTBLUE",   "METPAINTBLUE" },   /* lavender */
    { 170,   0, 255, "REGPAINTBLUE",   "METPAINTBLUE" },   /* electric purple */
    { 100,  20,  90, "REGPAINTRED",    "METPAINTRED" },    /* plum */
    {  40,   0, 130, "REGPAINTBLUE",   "METPAINTBLUE" },   /* indigo */
    { 100,  40, 220, "REGPAINTBLUE",   "METPAINTBLUE" },   /* blue violet */
    { 200,  80, 210, "REGPAINTPINK",   "METPAINTRED" },    /* orchid */
    { 255,  30, 130, "REGPAINTPINK",   "METPAINTRED" },    /* hot pink */
    { 255, 120, 180, "REGPAINTPINK",   "METPAINTRED" },    /* bubblegum */
    { 230,   0, 160, "REGPAINTPINK",   "METPAINTRED" },    /* magenta */
    { 255,   0, 200, "REGPAINTPINK",   "METPAINTRED" },    /* fuchsia */
    { 215,  60, 100, "REGPAINTRED",    "METPAINTRED" },    /* rose */
    {   0, 130, 125, "REGPAINTGREEN",  "METPAINTGREEN" },  /* teal */
    {   0, 190, 175, "REGPAINTGREEN",  "METPAINTGREEN" },  /* turquoise */
    {  40, 230, 230, "REGPAINTBLUE",   "METPAINTBLUE" },   /* aqua */
    { 120, 240, 180, "REGPAINTGREEN",  "METPAINTGREEN" },  /* mint */
    {  90, 200, 150, "REGPAINTGREEN",  "METPAINTGREEN" },  /* seafoam */
    { 150, 250,   0, "REGPAINTGREEN",  "METPAINTGREEN" },  /* lime */
    {  57, 255,  20, "REGPAINTGREEN",  "METPAINTGREEN" },  /* neon green */
    { 180, 255,   0, "REGPAINTYELLOW", "METPAINTYELLOW" }, /* toxic */
    { 235, 255,   0, "REGPAINTYELLOW", "METPAINTYELLOW" }, /* neon yellow */
    { 255, 120,   0, "REGPAINTORANGE", "METPAINTGOLD" },   /* tangerine */
    { 255, 100,  80, "REGPAINTORANGE", "METPAINTRED" },    /* coral */
    { 170,   0,  40, "REGPAINTRED",    "METPAINTRED" },    /* crimson */
    {  90, 180, 255, "REGPAINTBLUE",   "METPAINTBLUE" },   /* sky blue */
    {   0, 110, 255, "REGPAINTBLUE",   "METPAINTBLUE" },   /* electric blue */
    { 180,  90,  40, "REGPAINTORANGE", "METPAINTGOLD" },   /* copper */
};
/* the game's paints that take them (tools: of each list the nearest to another first) */
static const char *const k_gloss[N_COLOUR] = {
    "GLOSS_L1_COLOR05", "GLOSS_L2_COLOR07", "GLOSS_L2_COLOR06", "GLOSS_L1_COLOR10", "GLOSS_L2_COLOR05",
    "GLOSS_L2_COLOR12", "GLOSS_L3_TEST05", "GLOSS_L1_COLOR08", "GLOSS_L2_COLOR14", "GLOSS_L3_TEST06",
    "GLOSS_L2_COLOR08", "GLOSS_L3_TEST26", "GLOSS_L2_COLOR04", "GLOSS_L1_COLOR07", "GLOSS_L2_COLOR19",
    "GLOSS_L3_TEST27", "GLOSS_L3_TEST04", "GLOSS_L2_COLOR09", "GLOSS_L1_COLOR01", "GLOSS_L2_COLOR03",
    "GLOSS_L3_TEST13", "GLOSS_L1_COLOR02", "GLOSS_L2_COLOR13", "GLOSS_L2_COLOR18", "GLOSS_L2_COLOR11",
    "GLOSS_L2_COLOR15", "GLOSS_L3_TEST20", "GLOSS_L2_COLOR16", "GLOSS_L3_TEST08", "GLOSS_L3_TEST02",
};
static const char *const k_metal[N_COLOUR] = {
    "METAL_L3_TEST01", "METAL_L2_NEW_06", "METAL_L2_COLOR09", "METAL_L2_COLOR06", "METAL_L3_TEST07",
    "METAL_L2_NEW_13", "METAL_L2_COLOR07", "METAL_L3_TEST10", "METAL_L2_COLOR01", "METAL_L3_NEW_21",
    "METAL_L3_TEST06", "METAL_L2_NEW_05", "METAL_L3_TEST08", "METAL_L2_COLOR03", "METAL_L3_TEST09",
    "METAL_L2_NEW_14", "METAL_L2_COLOR05", "METAL_L2_COLOR04", "METAL_L2_NEW_03", "METAL_L3_TEST14",
    "METAL_L2_NEW_11", "METAL_L3_TEST12", "METAL_L3_TEST02", "METAL_L3_TEST11", "METAL_L2_NEW_07",
    "METAL_L3_TEST03", "METAL_L3_TEST18", "METAL_L3_TEST15", "METAL_L2_COLOR02", "METAL_L3_TEST05",
};
static const char *const k_rim[N_RIM] = {
    "RIMS_L1_COLOR07", "RIMS_L1_COLOR04", "RIMS_L2_COLOR05", "RIMS_L2_COLOR11", "RIMS_L1_COLOR05",
    "RIMS_L2_COLOR08", "RIMS_L1_COLOR10", "RIMS_L2_COLOR06", "RIMS_L2_COLOR02", "RIMS_L1_COLOR06",
    "RIMS_L2_COLOR12", "RIMS_L2_COLOR01", "RIMS_L1_COLOR09", "RIMS_L2_COLOR16", "RIMS_L2_COLOR03",
    "RIMS_L1_COLOR08", "RIMS_L2_COLOR04", "RIMS_L2_COLOR17", "RIMS_L2_COLOR10", "RIMS_L2_COLOR15",
};
static const char *const k_hose[N_RIM] = {
    "HOSES_L2_COLOR05", "HOSES_L1_COLOR07", "HOSES_L2_COLOR12", "HOSES_L1_COLOR09", "HOSES_L1_COLOR06",
    "HOSES_L2_COLOR10", "HOSES_L2_COLOR06", "HOSES_L2_COLOR07", "HOSES_L2_COLOR13", "HOSES_L2_COLOR08",
    "HOSES_L2_COLOR02", "HOSES_L1_COLOR05", "HOSES_L2_COLOR01", "HOSES_L2_COLOR15", "HOSES_L2_COLOR16",
    "HOSES_L2_COLOR03", "HOSES_L2_COLOR17", "HOSES_L1_COLOR08", "HOSES_L1_COLOR10", "HOSES_L2_COLOR04",
};
static const char *const k_muffler[N_RIM] = {
    "MUFFLERS_L2_COLOR05", "MUFFLERS_L1_COLOR07", "MUFFLERS_L2_COLOR12", "MUFFLERS_L1_COLOR09",
    "MUFFLERS_L1_COLOR06", "MUFFLERS_L2_COLOR10", "MUFFLERS_L2_COLOR06", "MUFFLERS_L2_COLOR07",
    "MUFFLERS_L2_COLOR13", "MUFFLERS_L2_COLOR08", "MUFFLERS_L2_COLOR02", "MUFFLERS_L1_COLOR05",
    "MUFFLERS_L2_COLOR01", "MUFFLERS_L2_COLOR15", "MUFFLERS_L2_COLOR16", "MUFFLERS_L2_COLOR03",
    "MUFFLERS_L2_COLOR17", "MUFFLERS_L1_COLOR08", "MUFFLERS_L1_COLOR10", "MUFFLERS_L2_COLOR04",
};
static const char *const k_caliper[N_RIM] = {
    "CALIPERS_L2_COLOR05", "CALIPERS_L1_COLOR07", "CALIPERS_L2_COLOR12", "CALIPERS_L1_COLOR09",
    "CALIPERS_L1_COLOR06", "CALIPERS_L2_COLOR10", "CALIPERS_L2_COLOR06", "CALIPERS_L2_COLOR07",
    "CALIPERS_L2_COLOR13", "CALIPERS_L2_COLOR08", "CALIPERS_L2_COLOR02", "CALIPERS_L1_COLOR05",
    "CALIPERS_L2_COLOR01", "CALIPERS_L2_COLOR15", "CALIPERS_L2_COLOR16", "CALIPERS_L2_COLOR03",
    "CALIPERS_L2_COLOR17", "CALIPERS_L1_COLOR08", "CALIPERS_L1_COLOR10", "CALIPERS_L2_COLOR04",
};
static const char *const k_vinyl[N_COLOUR] = {
    "VINYL_L3_COLOR05", "VINYL_L2_COLOR06", "VINYL_L2_COLOR05", "VINYL_L1_COLOR10", "VINYL_L1_COLOR07",
    "VINYL_L2_COLOR12", "VINYL_L1_COLOR09", "VINYL_L1_COLOR01", "VINYL_L2_COLOR14", "VINYL_L3_COLOR07",
    "VINYL_L2_COLOR11", "VINYL_L2_COLOR08", "VINYL_L3_COLOR27", "VINYL_L3_COLOR02", "VINYL_L2_COLOR07",
    "VINYL_L2_COLOR19", "VINYL_L1_COLOR06", "VINYL_L2_COLOR09", "VINYL_L2_COLOR10", "VINYL_L2_COLOR03",
    "VINYL_L3_COLOR06", "VINYL_L3_COLOR14", "VINYL_L2_COLOR04", "VINYL_L2_COLOR13", "VINYL_L2_COLOR18",
    "VINYL_L2_COLOR17", "VINYL_L2_COLOR15", "VINYL_L3_COLOR21", "VINYL_L3_COLOR28", "VINYL_L3_COLOR09",
};
/* the colours each list's 20 plain slots get: the purples, the pinks, and a few of the rest */
static const uint8_t k_rim_colour[N_RIM] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 20, 28 };

static uint32_t g_raw;

static uint32_t bhash(const char *s){
    uint32_t h = 0xffffffffu;
    for (; *s; s++) h = h * 33u + (uint8_t)*s;
    return h;
}
/* slot i of them all: its part's name, colour and material */
static void slot(int i, const char **name, int *col, const char **mat){
    if (i < N_COLOUR){ *name = k_gloss[i]; *col = i; *mat = k_colour[i].gloss; }
    else if (i < 2 * N_COLOUR){ *name = k_metal[i - N_COLOUR]; *col = i - N_COLOUR; *mat = k_colour[i - N_COLOUR].metal; }
    else if (i >= N_PAINTS){ *name = k_vinyl[i - N_PAINTS]; *col = i - N_PAINTS; *mat = 0; }
    else {
        int at = i - 2 * N_COLOUR, r = at % N_RIM;
        static const char *const *const lists[4] = { k_rim, k_hose, k_muffler, k_caliper };
        *name = lists[at / N_RIM][r]; *col = k_rim_colour[r]; *mat = "USER_RIMS_MET";
    }
}

/* the chunk with our colours in it, in guest memory for good; 0: as it is (logged why) */
uint32_t paints_grow(uint32_t chunk){
    if (!guest_ok_bytes(chunk, 16u) || MR32(chunk) != CHUNK_PARTS || !guest_ok_bytes(chunk, MR32(chunk + 4u) + 8u)) return 0;
    uint32_t size = MR32(chunk + 4u), at = chunk + 8u, end = chunk + 8u + size, sub[16][2];
    int n = 0;
    while (at + 8u <= end && n < 16){
        uint32_t sz = MR32(at + 4u);
        if (sz > end - at - 8u) break;
        sub[n][0] = at; sub[n][1] = sz; n++;
        at += 8u + sz;
    }
    int laid = n >= 7 && at == end;
    for (int k = 0; laid && k < 7; k++) laid = MR32(sub[k][0]) == SUB[k];
    if (!laid){ fprintf(stderr, "[paints] the car part database is laid out unlike the game's: no new colours\n"); return 0; }
    uint32_t hdr = sub[S_HDR][0] + 8u, lst = sub[S_LIST][0] + 8u, atr = sub[S_ATTR][0] + 8u, prt = sub[S_PART][0] + 8u;
    uint32_t nattr = MR32(hdr + 0x20u), nparts = MR32(hdr + 0x38u);
    if (sub[S_HDR][1] < 0x3cu || nattr * 8u > sub[S_ATTR][1] || nparts * PART_SZ > sub[S_PART][1]){
        fprintf(stderr, "[paints] the car part database's counts are not its tables': no new colours\n"); return 0;
    }
    /* each slot's part, and that it is a plain paint */
    uint32_t hred = bhash("RED"), hgrn = bhash("GREEN"), hblu = bhash("BLUE"), part[N_ALL], g_lst = 0, g_atr = 0;
    for (int i = 0; i < N_ALL; i++){
        const char *nm, *mat; int col;
        slot(i, &nm, &col, &mat);
        uint32_t key = bhash(nm), p = 0, kind = mat ? KIND_PAINT : KIND_VINYL_COLOUR;
        int na = mat ? ATTR_N : ATTR_N_VINYL;
        for (uint32_t j = 0; j < nparts && !p; j++){ uint32_t q = prt + j * PART_SZ; if (MR32(q) == key && MR8(q + 4u) == kind) p = q; }
        if (!p){ fprintf(stderr, "[paints] the car part database has no %s: no new colours\n", nm); return 0; }
        uint32_t li = MR16(p + 0xau);
        if (li == 0xffffu || (uint64_t)li * 2u + 2u + 2u * (uint32_t)na > sub[S_LIST][1] || (int16_t)MR16(lst + li * 2u) != na){
            fprintf(stderr, "[paints] %s is not a plain paint: no new colours\n", nm); return 0;
        }
        int seen = 0;
        for (int j = 0; j < na; j++){
            int ix = (int16_t)MR16(lst + li * 2u + 2u + 2u * (uint32_t)j);
            if (ix < 0 || (uint32_t)ix >= nattr){ fprintf(stderr, "[paints] %s's attributes are out of range: no new colours\n", nm); return 0; }
            uint32_t h = MR32(atr + 8u * (uint32_t)ix);
            seen += h == hred || h == hgrn || h == hblu || h == ATTR_MATERIAL;
        }
        if (seen != (mat ? 4 : 3)){ fprintf(stderr, "[paints] %s's attributes are not a paint's: no new colours\n", nm); return 0; }
        part[i] = p;
        g_lst += 2u + 2u * (uint32_t)na; g_atr += (uint32_t)na * 8u;
    }
    /* what the tables gain: attribute rows and lists (kept to 8 bytes) */
    g_lst = (g_lst + 7u) & ~7u;
    uint32_t gain[7] = { 0, 0, g_lst, g_atr, 0, 0, 0 }, nsize = size;
    for (int k = 0; k < 7; k++) nsize += gain[k];
    if ((sub[S_LIST][1] + g_lst) / 2u > 0xffffu || nattr + g_atr / 8u > 0x7fffu){
        fprintf(stderr, "[paints] the car part database's tables are too full: no new colours\n"); return 0;
    }
    uint32_t out = gheap_alloc(1, nsize + 8u + 16u, 1);
    if (!out){ fprintf(stderr, "[paints] no memory for the car part database's copy\n"); return 0; }
    g_raw = out;
    out = (out + 15u) & ~15u;
    MR32(out) = CHUNK_PARTS; MR32(out + 4u) = nsize;
    uint32_t o = out + 8u, place[7];
    for (int k = 0; k < n; k++){
        uint32_t sz = sub[k][1] + (k < 7 ? gain[k] : 0u);
        MR32(o) = MR32(sub[k][0]); MR32(o + 4u) = sz;
        memcpy(&MR8(o + 8u), &MR8(sub[k][0] + 8u), sub[k][1]);
        if (k < 7) place[k] = o + 8u;
        o += 8u + sz;
    }
    uint32_t l_at = place[S_LIST] + sub[S_LIST][1], a_at = place[S_ATTR] + nattr * 8u, ai = nattr;
    memmove(&MR8(a_at + g_atr), &MR8(a_at), sub[S_ATTR][1] - nattr * 8u);    /* what the table had past its count (none in the game's) */
    memset(&MR8(a_at), 0, g_atr);
    for (int i = 0; i < N_ALL; i++){
        const char *nm, *mat; int col;
        slot(i, &nm, &col, &mat);
        uint32_t old = MR16(part[i] + 0xau), li = (l_at - place[S_LIST]) / 2u;
        int na = mat ? ATTR_N : ATTR_N_VINYL;
        MR16(l_at) = (uint16_t)na;
        for (int j = 0; j < na; j++){
            uint32_t ix = (uint16_t)MR16(lst + old * 2u + 2u + 2u * (uint32_t)j), h = MR32(atr + 8u * ix), v = MR32(atr + 8u * ix + 4u);
            if (h == hred) v = k_colour[col].r;
            else if (h == hgrn) v = k_colour[col].g;
            else if (h == hblu) v = k_colour[col].b;
            else if (h == ATTR_MATERIAL && mat) v = bhash(mat);
            MR32(a_at) = h; MR32(a_at + 4u) = v; a_at += 8u;
            MR16(l_at + 2u + 2u * (uint32_t)j) = (uint16_t)ai++;
        }
        l_at += 2u + 2u * (uint32_t)na;
        MR16(place[S_PART] + (part[i] - prt) + 0xau) = (uint16_t)li;
    }
    MR32(place[S_HDR] + 0x20u) = ai;
    fprintf(stderr, "[paints] the car part database: %d colours (%d Gloss, %d Metallic, %d each rims, hoses, mufflers, calipers, %d vinyl) on the game's paints\n", N_ALL, N_COLOUR, N_COLOUR, N_RIM, N_COLOUR);
    return out;
}
void paints_free(void){ if (g_raw){ gheap_free(1, g_raw); g_raw = 0; } }
