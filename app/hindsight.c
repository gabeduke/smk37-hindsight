/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Added 2026-10-26 for Hindsight. The protocol is in hindsight.h. */
#include "hindsight.h"
#include "fm1.h"
#include "smk37_controls.h"
#include <string.h>

static struct hs_item items[HS_ITEMS];
static unsigned count, active, revision, background, owner = FM1_MIDI_USB;
static uint32_t held_pads, held_buttons;
static unsigned touches;
static uint8_t knobs[HS_KNOBS];
/* What changed since screen.c last asked, per band: columns [left, right). */
static uint8_t damage_left[HS_BANDS], damage_right[HS_BANDS];
/* One message per port waits while that port's queue is full; a newer one
 * replaces it, as only the latest answer is worth sending late. */
static struct { uint8_t bytes[11], length, pending; uint32_t since; } outgoing[3];

int hindsight_frame(const uint8_t *m, size_t n)
{
    return m && n >= 8 && m[0] == 0xf0 && m[1] == 0x7d && m[2] == 'H' && m[3] == 'S' &&
           m[4] == HS_VERSION && m[n - 1] == 0xf7;
}

static void queue(unsigned port, const uint8_t *bytes, unsigned n)
{
    memcpy(outgoing[port].bytes, bytes, n);
    outgoing[port].length = (uint8_t)n;
    outgoing[port].since = fm1_millis();
    outgoing[port].pending = 1;
    hindsight_poll();
}

static void damage(unsigned x, unsigned y, unsigned w, unsigned h)
{
    for (unsigned b = y / HS_BAND_ROWS; b <= (y + h - 1) / HS_BAND_ROWS; ++b) {
        if (!damage_right[b] || x < damage_left[b]) damage_left[b] = (uint8_t)x;
        if (x + w > damage_right[b]) damage_right[b] = (uint8_t)(x + w);
    }
}

/* Where an item's glyphs are: as many as fit the screen's width. */
static void item_damage(const struct hs_item *item)
{
    unsigned w = 6U * item->scale, n = (unsigned)strlen(item->text), fit = (FM1_SCREEN_WIDTH - item->x) / w;
    damage(item->x, item->y, (n < fit ? n : fit) * w, 7U * item->scale);
}

static unsigned text_at(const uint8_t *p, unsigned n)
{
    if (n < 6 || n - 6 > HS_TEXT) return HS_MALFORMED;
    unsigned x = p[0] | (unsigned)p[1] << 7, y = p[2] | (unsigned)p[3] << 7, scale = p[4], colour = p[5];
    if (!scale || scale > 4 || colour >= HS_COLOURS ||
        x + 6U * scale > FM1_SCREEN_WIDTH || y + 7U * scale > FM1_SCREEN_HEIGHT)
        return HS_MALFORMED;
    for (unsigned i = 6; i < n; ++i)
        if (p[i] < ' ' || p[i] > '~') return HS_MALFORMED;
    if (!active) return HS_RELEASED;
    unsigned at = 0;
    while (at < count && (items[at].x != x || items[at].y != y)) ++at;
    if (n == 6) {
        /* No text removes what was at (x, y). */
        if (at == count) return HS_OK;
        item_damage(items + at);
        memmove(items + at, items + at + 1, (--count - at) * sizeof *items);
    } else {
        struct hs_item next = {(uint8_t)x, (uint8_t)y, (uint8_t)scale, (uint8_t)colour, {0}};
        memcpy(next.text, p + 6, n - 6);
        if (at == count) {
            if (count == HS_ITEMS) return HS_FULL;
            ++count;
        } else if (!memcmp(items + at, &next, sizeof next))
            return HS_OK;
        else
            item_damage(items + at);
        items[at] = next;
        item_damage(&next);
    }
    ++revision;
    return HS_OK;
}

void hindsight_message(const uint8_t *m, size_t n, unsigned port)
{
    /* An ack is never answered, so an echo can't become a loop. */
    if (!hindsight_frame(m, n) || port >= 3 || m[6] == HS_ACK) return;
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
        else { background = payload[0]; count = 0; active = 1; owner = port; ++revision; damage(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT); }
    } else if (command == HS_TEXT_AT)
        status = text_at(payload, length);
    else if (command == HS_RELEASE) {
        if (length) status = HS_MALFORMED;
        else if (active) { active = 0; ++revision; }
    } else if (command == HS_KNOB) {
        if (length != 2 || payload[0] >= HS_KNOBS) status = HS_MALFORMED;
        else knobs[payload[0]] = payload[1];
    } else
        status = HS_UNKNOWN;

    uint8_t ack[11];
    memcpy(ack, m, 6); /* F0 7D 48 53 01 seq */
    ack[5] &= 0x7f;
    unsigned at = 6;
    ack[at++] = HS_ACK;
    ack[at++] = (uint8_t)status;
    if (command == HS_HELLO && !status) { ack[at++] = HS_VERSION; ack[at++] = HS_ITEMS; }
    ack[at++] = 0xf7;
    queue(port, ack, at);
}

void hindsight_poll(void)
{
    uint32_t now = fm1_millis();
    for (unsigned p = 0; p < 3; ++p) {
        if (!outgoing[p].pending) continue;
        int r = fm1_midi_send_port(p, outgoing[p].bytes, outgoing[p].length);
        if (r != -2 || now - outgoing[p].since > 500U) outgoing[p].pending = 0;
    }
}

/* The panel's MIDI to the Pi. A full queue drops it: the Pi's controls are
 * live gestures, and Open's own MIDI out does the same. */
static void surface_send(unsigned status, unsigned a, unsigned b)
{
    const uint8_t m[3] = {(uint8_t)status, (uint8_t)a, (uint8_t)(b > 127 ? 127 : b)};
    (void)fm1_midi_send_port(owner, m, 3);
}

/* A press goes to the Pi while it has the screen, and its release follows
 * it there; a release whose press the Pi never had is Open's. */
static int held(uint32_t *mask, unsigned bit, int value, unsigned status, unsigned note, unsigned velocity)
{
    if (value ? !active : !(*mask & bit)) return 0;
    if (value) *mask |= bit; else *mask &= ~bit;
    surface_send(value ? 0x90 | status : 0x80 | status, note, value ? velocity : 0);
    return 1;
}

int hindsight_surface(unsigned kind, unsigned id, int value, unsigned fader_bank)
{
    if ((kind == SMK37_ENCODER && value) ||
        (value > 0 && (kind == SMK37_KEY || kind == SMK37_PAD ||
                       (kind == SMK37_BUTTON && id != 2 && id != 12))))
        ++touches;
    if ((kind == SMK37_PAD || kind == SMK37_PRESSURE) && id < 16) {
        if (kind == SMK37_PAD)
            return held(&held_pads, 1U << id, value, HS_PAD_CHANNEL, HS_PAD_NOTE + id, (unsigned)value);
        if (!(held_pads & (1U << id))) return 0;
        surface_send(0xa0 | HS_PAD_CHANNEL, HS_PAD_NOTE + id, (unsigned)value);
        return 1;
    }
    if (kind == SMK37_BUTTON && id < 18 && id != 1 && id != 2 && id != 12)
        return held(&held_buttons, 1U << id, value, HS_SURFACE_CHANNEL, id, 127);
    if (!active) return 0;
    if (kind == SMK37_ENCODER && id < HS_KNOBS) {
        int next = knobs[id] + value;
        next = next < 0 ? 0 : next > 127 ? 127 : next;
        if (next != knobs[id]) {
            knobs[id] = (uint8_t)next;
            surface_send(0xb0 | HS_SURFACE_CHANNEL, HS_KNOB_CC + id, (unsigned)next);
        }
        return 1;
    }
    if (kind == SMK37_FADER && id < 4) {
        surface_send(0xb0 | HS_SURFACE_CHANNEL, HS_FADER_CC + id + (fader_bank ? 4 : 0), (unsigned)value);
        return 1;
    }
    return 0;
}

/* A message of the keyboard's own, seq 0, to the port that took the screen. */
static void tell(unsigned command)
{
    const uint8_t m[] = {0xf0, 0x7d, 'H', 'S', HS_VERSION, 0, (uint8_t)command, 0xf7};
    queue(owner, m, sizeof m);
}

void hindsight_release(void)
{
    if (!active) return;
    active = 0;
    ++revision;
    tell(HS_RELEASE);
}

void hindsight_offer(void)
{
    if (!active) tell(HS_OFFER);
}

unsigned hindsight_active(void) { return active; }
unsigned hindsight_touches(void) { return touches; }
unsigned hindsight_revision(void) { return revision; }
unsigned hindsight_damage(unsigned band)
{
    unsigned d = damage_left[band] | (unsigned)damage_right[band] << 8;
    damage_right[band] = 0;
    return d >> 8 ? d : 0;
}
unsigned hindsight_background(void) { return background; }
const struct hs_item *hindsight_item(unsigned index) { return index < count ? items + index : NULL; }
