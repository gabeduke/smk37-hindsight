/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Modified 2026-10-26 for Hindsight: the workbench's project transfer
 * protocol (F0 7D 'F' 'M' 02) is replaced by Hindsight's (hindsight.h).
 * MMC transport and DX7 single-voice dumps are handled as before. */
#include "instrument.h"
#include "fm_patch.h"
#include "hindsight.h"
#include <string.h>

/* SysEx arrives on the routing task, which may not draw, write Flash or
 * block. fm1_app_sysex copies one accepted frame into this mailbox and asks
 * for the control task, whose groove_transfer_poll handles it. Audio never
 * handles SysEx. */
#ifdef FM1_HOST_TEST
#include <stdatomic.h>
#define TRANSFER_WORD _Atomic(unsigned)
#else
#define TRANSFER_WORD volatile unsigned
#endif
static struct { uint8_t bytes[192], source; TRANSFER_WORD length; } incoming;
static void barrier(void)
{
#ifndef FM1_HOST_TEST
    __asm__ volatile("csync" ::: "memory");
#endif
}
void fm1_app_sysex(const uint8_t *m, size_t n)
{
    if (!m || n < 6 || n > sizeof incoming.bytes || m[0] != 0xf0 || m[n - 1] != 0xf7 ||
        incoming.length)
        return;
    if (!hindsight_frame(m, n) &&
        !(n == 6 && m[1] == 0x7f && (m[2] == 0 || m[2] == 0x7f) && m[3] == 6) &&
        !(n == 163 && m[1] == 0x43 && m[2] < 16 && m[3] == 0 && m[4] == 1 && m[5] == 27))
        return;
    incoming.source = (uint8_t)fm1_midi_input_source();
    memcpy(incoming.bytes, m, n);
    barrier();
    incoming.length = (unsigned)n;
    fm1_control_request();
}
void groove_transfer_init(void)
{
    incoming.length = 0;
}
void groove_transfer_poll(void)
{
    hindsight_poll();
    unsigned n = incoming.length;
    if (!n) return;
    barrier();
    unsigned source = incoming.source;
    const uint8_t *message = incoming.bytes;
    for (unsigned i = 1; i + 1 < n; ++i)
        if (message[i] >= 128)
            goto discard;
    if (message[1] == 0x7f) {
        unsigned code = message[4];
        barrier();
        incoming.length = 0;
        if (groove_midi_clock_accept(source, code == 1 ? 0xfc : 0xfa)) groove_screen_mmc(code);
        return;
    }
    if (message[1] == 0x43) {
        unsigned sum = 0;
        for (unsigned i = 6; i < 162; ++i)
            sum += message[i];
        if (!(sum & 127U)) {
            uint8_t patch[156];
            unsigned channel = message[2];
            memcpy(patch, message + 6, 155);
            patch[155] = 63;
            barrier();
            incoming.length = 0;
            for (unsigned track=0;track<GROOVE_TRACKS;++track)
                if (groove_track_role(track) != GROOVE_TRACK_OFF && channel == groove_midi_input_channel(track))
                    (void)groove_fm_import(track, patch);
            return;
        }
        goto discard;
    }
    hindsight_message(message, n, source);
discard:
    barrier();
    incoming.length = 0;
}
