/* custompaint.c: paints of any colour, by key */
#ifndef CUSTOMPAINT_H
#define CUSTOMPAINT_H
#include "cpu.h"
#include <stdint.h>
enum { CP_GLOSS, CP_METAL, CP_RIM, CP_HOSE, CP_MUFFLER, CP_CALIPER, CP_VINYL, CP_N };
uint32_t custompaint_make(int type, uint32_t rgb);  /* the key of that paint in that colour */
int  custompaint_is(uint32_t slot, uint32_t key);   /* a key of ours, in a slot it can be in */
void custompaint_get(CPU *restrict c);              /* NewGetCarPart's answer for one */
void sub_0061b980(CPU *restrict c);
#endif
