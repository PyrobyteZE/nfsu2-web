/* nosmod.c -- the Mods tab's Nitrous setting (NFSU2_NOS_MODE = off, unlimited, faster or both).  Unlimited keeps the
 * player's shot full, faster gives it twice the push.  A car with no nitrous part stays without.  Alone (the career,
 * Quick Race, split screen, free roam) it is the player's own affair; in a free-roam lobby's race it counts only when
 * every racer has it on (roam_game.c: each JOIN says so, the host's START says whether all did); Online never. */
#include "cpu.h"
#include "guest.h"
#include "game.h"
#include "splitscreen_mem.h"
#include "setting.h"
#include <stdio.h>
#include <string.h>

uint32_t mprace_perf(uint32_t car);
int mprace_phase(void);

#define CAR_NOS      0x40u           /* Car: its nitrous (roam_game.c nos_left) */
#define NOS_FULL     0x414u          /* a full shot, in G_TICKS */
#define NOS_LEFT     0x41cu          /* what was left when it went on */
#define NOS_SINCE    0x420u          /* when it went on */
#define NO_TIME      0x7fffffffu
#define G_TICKS      0x0086518cu
#define PI_NITROUS   0x2dcu          /* the car's performance info: the nitrous' torque (mprace.c) */
#define FASTER       2.f
#define MODE_LAN     0x200u
#define MODE_ONLINE  0x8u

enum { M_UNLIMITED = 1, M_FASTER = 2 };
static int g_race_ok;                /* the lobby race's racers all have it on (its START said so) */
static uint32_t g_pi;                /* the car whose push we set, what it was and what we made it */
static float g_base, g_set;

static int mode(void){
    static int m = -1;
    if (m < 0){
        const char *e = getenv("NFSU2_NOS_MODE");
        m = !e ? 0 : !strcasecmp(e, "unlimited") ? M_UNLIMITED : !strcasecmp(e, "faster") ? M_FASTER : !strcasecmp(e, "both") ? M_UNLIMITED | M_FASTER : 0;
        if (m) fprintf(stderr, "[nos] the Nitrous setting is on: %s\n", e);
    }
    return m;
}
int nosmod_on(void){ return mode() != 0; }
void nosmod_race(int ok){
    if (ok != g_race_ok && mode()) fprintf(stderr, "[nos] this lobby's race: %s\n", ok ? "every racer has it on, it counts" : "not every racer has it on, normal nitrous");
    g_race_ok = ok;
}
static int allowed(void){
    uint32_t gm = guest_rd32(G_GAMEMODE);
    if (gm & MODE_ONLINE) return 0;
    if (gm & MODE_LAN) return mprace_phase() != 0 && g_race_ok;
    return 1;
}
/* the push back as the game has it, if it is still ours */
static void push_back(void){
    if (g_pi && guest_ok(g_pi, PI_NITROUS + 4u) && MRF32(g_pi + PI_NITROUS) == g_set) MRF32(g_pi + PI_NITROUS) = g_base;
    g_pi = 0;
}
void nosmod_frame(CPU *c){
    (void)c;
    int m = mode();
    if (!m) return;
    uint32_t p = guest_rd32(G_FLOW) == 6u ? guest_rd32(SPLIT_PLAYERS_INDEX) : 0u, car = guest_ok(p, 8u) ? guest_rd32(p + 4u) : 0u;
    if (!guest_ok(car + CAR_NOS, NOS_SINCE + 4u) || !guest_ok(car, 0x14u)){ g_pi = 0; return; }
    if (!allowed()){ push_back(); return; }
    uint32_t n = car + CAR_NOS, full = MR32(n + NOS_FULL);
    if (!full || full == NO_TIME) return;                        /* no nitrous on this car */
    if (m & M_UNLIMITED){
        uint32_t since = MR32(n + NOS_SINCE);
        MR32(n + NOS_LEFT) = full;
        if (since && since != NO_TIME) MR32(n + NOS_SINCE) = MR32(G_TICKS);
    }
    if (m & M_FASTER){
        uint32_t pi = mprace_perf(car);
        if (pi != g_pi && g_pi) push_back();
        if (!pi){ g_pi = 0; return; }
        float v = MRF32(pi + PI_NITROUS);
        if (v > 0.f && (pi != g_pi || v != g_set)){              /* a car new to us, or its info built again */
            g_pi = pi; g_base = v; g_set = v * FASTER;
            MRF32(pi + PI_NITROUS) = g_set;
        }
    }
}
