/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Added 2026-10-26 for Hindsight. The protocol is in hindsight.h. */
#include "hindsight.h"
#include "fm1.h"
#include <string.h>

static struct hs_item items[HS_ITEMS];
static unsigned count, active, revision, background;
/* One ack per port waits while that port's queue is full; a newer ack
 * replaces it, as only the latest answer is worth sending late. */
static struct { uint8_t bytes[11], length, pending; uint32_t since; } acks[3];

int hindsight_frame(const uint8_t *m, size_t n)
{
    return m && n >= 8 && m[0] == 0xf0 && m[1] == 0x7d && m[2] == 'H' && m[3] == 'S' &&
           m[4] == HS_VERSION && m[n - 1] == 0xf7;
}

static unsigned text_at(const uint8_t *p, unsigned n)
{
    if (n < 6 || n - 6 > HS_TEXT) return HS_MALFORMED;
    unsigned x = p[0] | (unsigned)p[1] << 7, y = p[2] | (unsigned)p[3] << 7, scale = p[4], colour = p[5];
    if (x >= FM1_SCREEN_WIDTH || y >= FM1_SCREEN_HEIGHT || !scale || scale > 4 || colour >= HS_COLOURS)
        return HS_MALFORMED;
    for (unsigned i = 6; i < n; ++i)
        if (p[i] < ' ' || p[i] > '~') return HS_MALFORMED;
    unsigned at = 0;
    while (at < count && (items[at].x != x || items[at].y != y)) ++at;
    if (n == 6) {
        /* No text removes what was at (x, y). */
        if (at < count) memmove(items + at, items + at + 1, (--count - at) * sizeof *items);
    } else {
        if (at == count) {
            if (count == HS_ITEMS) return HS_FULL;
            ++count;
        }
        struct hs_item *item = items + at;
        item->x = (uint8_t)x; item->y = (uint8_t)y; item->scale = (uint8_t)scale; item->colour = (uint8_t)colour;
        memcpy(item->text, p + 6, n - 6);
        item->text[n - 6] = 0;
    }
    active = 1;
    ++revision;
    return HS_OK;
}

void hindsight_message(const uint8_t *m, size_t n, unsigned port)
{
    if (!hindsight_frame(m, n) || port >= 3) return;
    const uint8_t *payload = m + 7;
    unsigned length = (unsigned)n - 8, command = m[6], status = HS_OK;
    for (unsigned i = 5; i + 1 < n; ++i)
        if (m[i] >= 0x80) status = HS_MALFORMED;
    if (status)
        ;
    else if (command == HS_HELLO)
        status = length ? HS_MALFORMED : HS_OK;
    else if (command == HS_CLEAR) {
        if (length != 1 || payload[0] >= HS_COLOURS) status = HS_MALFORMED;
        else { background = payload[0]; count = 0; active = 1; ++revision; }
    } else if (command == HS_TEXT_AT)
        status = text_at(payload, length);
    else if (command == HS_RELEASE) {
        if (length) status = HS_MALFORMED;
        else hindsight_release();
    } else
        status = HS_UNKNOWN;

    uint8_t *ack = acks[port].bytes;
    memcpy(ack, m, 6); /* F0 7D 48 53 01 seq */
    ack[5] &= 0x7f;
    unsigned at = 6;
    ack[at++] = HS_ACK;
    ack[at++] = (uint8_t)status;
    if (command == HS_HELLO && !status) { ack[at++] = HS_VERSION; ack[at++] = HS_ITEMS; }
    ack[at++] = 0xf7;
    acks[port].length = (uint8_t)at;
    acks[port].since = fm1_millis();
    acks[port].pending = 1;
    hindsight_poll();
}

void hindsight_poll(void)
{
    uint32_t now = fm1_millis();
    for (unsigned p = 0; p < 3; ++p) {
        if (!acks[p].pending) continue;
        int r = fm1_midi_send_port(p, acks[p].bytes, acks[p].length);
        if (r != -2 || now - acks[p].since > 500U) acks[p].pending = 0;
    }
}

unsigned hindsight_active(void) { return active; }
void hindsight_release(void) { active = 0; ++revision; }
unsigned hindsight_revision(void) { return revision; }
unsigned hindsight_background(void) { return background; }
const struct hs_item *hindsight_item(unsigned index) { return index < count ? items + index : NULL; }
