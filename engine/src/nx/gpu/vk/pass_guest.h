/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PASS_GUEST=1, off by default (PARTICLE-SQUARES, docs/arceus-timing.md): a pass the game draws at
   reduced size reads each scaled colour target it samples through a copy at guest size, so its texel fetches step
   guest texels as on the console. Only at a render scale of exactly 2.0, where the copy's one linear blit is the mean
   of each 2x2 block. The GPU thread calls all of it */
#ifndef SNX_GPU_VK_PASS_GUEST_H
#define SNX_GPU_VK_PASS_GUEST_H
#include <stdint.h>

#define PASS_GUEST_WINDOW 600u /* presents a line covers */
#define PASS_GUEST_PROGRAMS 16u /* fragment programs a line names */

/* -1 before the switch is read, then 1 when it is on at scale 2.0, else 0 */
extern int g_pass_guest;
/* reads the switch once, at the render scale it is given, and logs whether it is on */
int pass_guest_on(float scale);
/* the guest size of the picture last presented: 0 before a present (set only while the switch is on) */
extern uint32_t g_pass_shown_w, g_pass_shown_h;
/* 1 when a draw of count vertices and instances instances into the colour targets tw x th (n of them, 0 x 0 for an
   empty slot, at least one bound) is a reduced pass: at most 4 vertices, one instance, and every target at most half
   the last present's guest width and height. Pure */
int pass_guest_reduced(uint32_t count, uint32_t instances, const uint32_t* tw, const uint32_t* th, uint32_t n,
                       uint32_t shown_w, uint32_t shown_h);

/* why a texture of a reduced pass kept today's binding: counted, and named in the line */
enum { PASS_GUEST_INTEGER, PASS_GUEST_FORMAT, PASS_GUEST_MEMORY, PASS_GUEST_WHYS };
void pass_guest_fallback(int why);
/* a guest copy refreshed by its blit */
void pass_guest_blit(void);
/* a reduced pass of the fragment program fs that read textures guest copies */
void pass_guest_served(uint64_t fs, uint32_t textures);
/* at a draw while the switch is on: the line of the window ending before present, once PASS_GUEST_WINDOW presents
   have passed since the window began */
void pass_guest_window(uint32_t present);

#endif
