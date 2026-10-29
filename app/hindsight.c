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
/* The lights the Pi has set: pads by RGB, buttons on and blinking. */
static uint32_t pads[HS_PADS], pads_set, buttons_set, buttons_on, buttons_blink;
/* Local echo (hindsight.h): the pads pressed since the Pi last set them, and
 * when; where each knob's value is drawn, if the Pi said. */
static uint32_t echoes, echo_ms[HS_PADS];
static struct { uint8_t on, x, y, bar_x, bar_y; } knob_at[HS_KNOBS];
/* One message per port waits while that port's queue is full; a newer one
 * replaces it, as only the latest answer is worth sending late. */
static struct { uint8_t bytes[12], length, pending; uint32_t since; } outgoing[3];

int hindsight_frame(const uint8_t *m, size_t n)
{
    return m && n >= 8 && n <= HS_FRAME && m[0] == 0xf0 && m[1] == 0x7d && m[2] == 'H' && m[3] == 'S' &&
           (m[4] == 1 || m[4] == HS_VERSION) && m[n - 1] == 0xf7;
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
static void item_damage(const struct hs_item *item) { damage(item->x, item->y, item->w, item->h); }

/* Put an item where its kind and place say, or with remove take it away. */
static unsigned place(const struct hs_item *next, unsigned remove)
{
    if (!active) return HS_RELEASED;
    unsigned at = 0;
    while (at < count && (items[at].kind != next->kind || items[at].x != next->x || items[at].y != next->y)) ++at;
    if (remove) {
        if (at == count) return HS_OK;
        item_damage(items + at);
        memmove(items + at, items + at + 1, (--count - at) * sizeof *items);
    } else {
        if (at == count) {
            if (count == HS_ITEMS) return HS_FULL;
            ++count;
        } else if (!memcmp(items + at, next, sizeof *next))
            return HS_OK;
        else
            item_damage(items + at);
        items[at] = *next;
        item_damage(next);
    }
    ++revision;
    return HS_OK;
}

/* The screen and the lights go back to the keyboard; 1 if they were the Pi's. */
static unsigned give_back(void)
{
    unsigned was = active;
    active = 0;
    pads_set = buttons_set = echoes = 0;
    memset(knob_at, 0, sizeof knob_at);
    revision += was;
    return was;
}

/* x, y, w, h as four 14-bit numbers; 0 if the rectangle doesn't fit. */
static unsigned rectangle(const uint8_t *p, struct hs_item *item)
{
    unsigned x = p[0] | (unsigned)p[1] << 7, y = p[2] | (unsigned)p[3] << 7,
             w = p[4] | (unsigned)p[5] << 7, h = p[6] | (unsigned)p[7] << 7;
    if (x >= FM1_SCREEN_WIDTH || y >= FM1_SCREEN_HEIGHT || w > FM1_SCREEN_WIDTH - x || h > FM1_SCREEN_HEIGHT - y)
        return 0;
    item->x = (uint8_t)x; item->y = (uint8_t)y; item->w = (uint8_t)w; item->h = (uint8_t)h;
    return 1;
}

static unsigned apply(unsigned version, unsigned command, const uint8_t *p, unsigned n, unsigned port)
{
    unsigned colours = version == 1 ? HS_V1_COLOURS : HS_COLOURS;
    struct hs_item next = {0};
    next.kind = (uint8_t)command;
    if (command == HS_CLEAR) {
        if (n != 1 || p[0] >= colours) return HS_MALFORMED;
        background = p[0]; count = 0; active = 1; owner = port; ++revision;
        memset(knob_at, 0, sizeof knob_at); /* the places were on the screen it clears */
        damage(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT);
        return HS_OK;
    }
    if (command == HS_TEXT_AT) {
        /* Version 2 adds the colour behind the glyphs. */
        unsigned head = version == 1 ? 6 : 7;
        if (n < head || n - head > HS_TEXT) return HS_MALFORMED;
        unsigned x = p[0] | (unsigned)p[1] << 7, y = p[2] | (unsigned)p[3] << 7, scale = p[4];
        unsigned back = version == 1 ? background : p[6];
        if (!scale || scale > 4 || p[5] >= colours || back >= colours ||
            x + 6U * scale > FM1_SCREEN_WIDTH || y + 7U * scale > FM1_SCREEN_HEIGHT)
            return HS_MALFORMED;
        for (unsigned i = head; i < n; ++i)
            if (p[i] < ' ' || p[i] > '~') return HS_MALFORMED;
        unsigned glyphs = n - head, fit = (FM1_SCREEN_WIDTH - x) / (6U * scale);
        next.x = (uint8_t)x; next.y = (uint8_t)y; next.scale = (uint8_t)scale;
        next.colour = p[5]; next.back = (uint8_t)back;
        next.w = (uint8_t)((glyphs < fit ? glyphs : fit) * 6U * scale);
        next.h = (uint8_t)(7U * scale);
        memcpy(next.text, p + head, glyphs);
        return place(&next, !glyphs);
    }
    if (command == HS_RELEASE) {
        if (n) return HS_MALFORMED;
        (void)give_back();
        return HS_OK;
    }
    if (command == HS_KNOB) {
        if ((n != 2 && (n != 10 || version == 1)) || p[0] >= HS_KNOBS) return HS_MALFORMED;
        knobs[p[0]] = p[1];
        knob_at[p[0]].on = 0; /* no places: no echo */
        if (n == 10) {
            /* Where its value is drawn, so a turn can echo it there. */
            unsigned x = p[2] | (unsigned)p[3] << 7, y = p[4] | (unsigned)p[5] << 7,
                     bar_x = p[6] | (unsigned)p[7] << 7, bar_y = p[8] | (unsigned)p[9] << 7;
            if (x >= FM1_SCREEN_WIDTH || y >= FM1_SCREEN_HEIGHT || bar_x >= FM1_SCREEN_WIDTH ||
                bar_y >= FM1_SCREEN_HEIGHT)
                return HS_MALFORMED;
            knob_at[p[0]].on = 1;
            knob_at[p[0]].x = (uint8_t)x; knob_at[p[0]].y = (uint8_t)y;
            knob_at[p[0]].bar_x = (uint8_t)bar_x; knob_at[p[0]].bar_y = (uint8_t)bar_y;
        }
        return HS_OK;
    }
    if (version == 1) return HS_UNKNOWN;
    if (command == HS_BOX || command == HS_BAR) {
        /* A BOX is a BAR full of its colour. */
        unsigned bar = command == HS_BAR;
        if (n != 9 + 2 * bar || !rectangle(p, &next)) return HS_MALFORMED;
        next.value = bar ? p[8] : 127;
        next.colour = p[8 + bar];
        next.back = p[8 + 2 * bar];
        if (next.colour >= HS_COLOURS || next.back >= HS_COLOURS) return HS_MALFORMED;
        return place(&next, !next.w || !next.h);
    }
    if (command == HS_LED) {
        unsigned target = n ? p[0] : 0, id = target - HS_LED_BUTTON;
        if (n == 4 && target < HS_PADS) {
            if (!active) return HS_RELEASED;
            /* 0-127 is off to full, and full is Open's brightest, 48: sixteen
             * pads at the LEDs' own 127 draw about 2.6 times Open's current. */
            uint32_t rgb = 0;
            for (unsigned i = 1; i < 4; ++i) rgb = rgb << 8 | (p[i] * HS_PAD_FULL + 63U) / 127U;
            pads[target] = rgb;
            pads_set |= 1U << target;
            echoes &= ~(1U << target); /* the Pi's answer to a press */
            return HS_OK;
        }
        if (n == 2 && target >= HS_LED_BUTTON && id < HS_BUTTONS && id != 1 && id != 2 && id != 12 && p[1] <= 2) {
            if (!active) return HS_RELEASED;
            uint32_t bit = 1U << id;
            buttons_set |= bit;
            buttons_on = p[1] == 1 ? buttons_on | bit : buttons_on & ~bit;
            buttons_blink = p[1] == 2 ? buttons_blink | bit : buttons_blink & ~bit;
            return HS_OK;
        }
        return HS_MALFORMED;
    }
    return HS_UNKNOWN;
}

void hindsight_message(const uint8_t *m, size_t n, unsigned port)
{
    /* An ack is never answered, so an echo can't become a loop. */
    if (!hindsight_frame(m, n) || port >= 3 || m[6] == HS_ACK) return;
    const uint8_t *payload = m + 7;
    unsigned length = (unsigned)n - 8, command = m[6], version = m[4], status = HS_OK, done = 0;
    for (unsigned i = 5; i + 1 < n; ++i)
        if (m[i] >= 0x80) status = HS_MALFORMED;
    if (status)
        ;
    else if (command == HS_HELLO)
        status = length ? HS_MALFORMED : HS_OK;
    else if (command == HS_BATCH && version > 1) {
        /* Each command in turn, as if alone, until one isn't OK. */
        for (unsigned at = 0; at < length && !status; ++done) {
            if (length - at < 2 || payload[at + 1] > length - at - 2) { status = HS_MALFORMED; break; }
            unsigned size = payload[at + 1];
            status = apply(version, payload[at], payload + at + 2, size, port);
            if (status) break;
            at += 2 + size;
        }
    } else
        status = apply(version, command, payload, length, port);

    uint8_t ack[12];
    memcpy(ack, m, 6); /* F0 7D 48 53 <v> seq */
    ack[5] &= 0x7f;
    unsigned at = 6;
    ack[at++] = HS_ACK;
    ack[at++] = (uint8_t)status;
    if (command == HS_HELLO && !status) { ack[at++] = HS_VERSION; ack[at++] = HS_ITEMS; ack[at++] = HS_FEATURES; }
    if (command == HS_BATCH && version > 1) { ack[at++] = done & 127; ack[at++] = (uint8_t)(done >> 7); }
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

/* An encoder turned: its value into the TEXT and the BAR its KNOB named,
 * as the Pi would draw them, before the Pi can. */
static void echo_knob(unsigned id, unsigned value)
{
    if (!knob_at[id].on) return;
    char digits[4];
    unsigned glyphs = value >= 100 ? 3 : value >= 10 ? 2 : 1;
    for (unsigned i = glyphs, v = value; i--; v /= 10) digits[i] = (char)('0' + v % 10);
    for (unsigned i = 0; i < count; ++i) {
        struct hs_item *item = items + i;
        if (item->kind == HS_TEXT_AT && item->x == knob_at[id].x && item->y == knob_at[id].y) {
            unsigned fit = (FM1_SCREEN_WIDTH - item->x) / (6U * item->scale);
            item_damage(item);
            memset(item->text, 0, sizeof item->text);
            memcpy(item->text, digits, glyphs);
            item->w = (uint8_t)((glyphs < fit ? glyphs : fit) * 6U * item->scale);
            item_damage(item);
        } else if (item->kind == HS_BAR && item->x == knob_at[id].bar_x && item->y == knob_at[id].bar_y) {
            item->value = (uint8_t)value;
            item_damage(item);
        }
    }
    ++revision;
}

int hindsight_surface(unsigned kind, unsigned id, int value, unsigned fader_bank)
{
    if ((kind == SMK37_ENCODER && value) ||
        (value > 0 && (kind == SMK37_KEY || kind == SMK37_PAD ||
                       (kind == SMK37_BUTTON && id != 2 && id != 12))))
        ++touches;
    if ((kind == SMK37_PAD || kind == SMK37_PRESSURE) && id < 16) {
        if (kind == SMK37_PAD) {
            if (value && active) { echoes |= 1U << id; echo_ms[id] = fm1_millis(); }
            return held(&held_pads, 1U << id, value, HS_PAD_CHANNEL, HS_PAD_NOTE + id, (unsigned)value);
        }
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
            echo_knob(id, (unsigned)next);
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
    /* Version 1's header, which a Pi of either version reads. */
    const uint8_t m[] = {0xf0, 0x7d, 'H', 'S', 1, 0, (uint8_t)command, 0xf7};
    queue(owner, m, sizeof m);
}

void hindsight_release(void)
{
    if (give_back()) tell(HS_RELEASE);
}

void hindsight_offer(void)
{
    if (!active) tell(HS_OFFER);
}

unsigned hindsight_active(void) { return active; }
unsigned hindsight_quiet(unsigned port) { return active && port == owner; }
unsigned hindsight_touches(void) { return touches; }
unsigned hindsight_revision(void) { return revision; }
unsigned hindsight_damage(unsigned band)
{
    unsigned d = damage_left[band] | (unsigned)damage_right[band] << 8;
    damage_right[band] = 0;
    return d >> 8 ? d : 0;
}
unsigned hindsight_background(void) { return background; }
uint32_t hindsight_pad(unsigned pad, uint32_t open)
{
    if (!active || pad >= HS_PADS || !(pads_set >> pad & 1U)) return open;
    uint32_t rgb = pads[pad];
    if (!(echoes >> pad & 1U)) return rgb;
    if (fm1_millis() - echo_ms[pad] >= HS_ECHO_MS) { echoes &= ~(1U << pad); return rgb; }
    /* Its half: three times its rest, each at most the pads' full. */
    uint32_t half = 0;
    for (unsigned shift = 24; shift;) {
        shift -= 8;
        unsigned c = (rgb >> shift & 255U) * 3U;
        half |= (uint32_t)(c > HS_PAD_FULL ? HS_PAD_FULL : c) << shift;
    }
    return half;
}
int hindsight_button(unsigned id)
{
    if (!active || id >= HS_BUTTONS || !(buttons_set >> id & 1U)) return -1;
    return (int)((buttons_on >> id & 1U) | (buttons_blink >> id & fm1_millis() / 250U & 1U));
}
const struct hs_item *hindsight_item(unsigned index) { return index < count ? items + index : NULL; }
