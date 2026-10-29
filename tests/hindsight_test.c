/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Host test for app/hindsight.c: the Pi's SysEx protocol. Built by tests/run.sh. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "hindsight.h"
#include "smk37_controls.h"
#include "fm1.h"

static uint8_t sent[32];
static unsigned version = 1;
static unsigned sent_length, sent_port, sends, busy;
static uint32_t clock_ms;
int fm1_midi_send_port(unsigned port, const uint8_t *message, size_t bytes)
{
    ++sends;
    if (busy) return -2;
    sent_port = port; sent_length = (unsigned)bytes; memcpy(sent, message, bytes);
    return 0;
}
uint32_t fm1_millis(void) { return clock_ms; }

static int failures;
#define CHECK(cond) do { if (!(cond)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Sends F0 7D 48 53 <version> seq cmd payload... F7 on USB; returns the ack's status. */
static int message(unsigned seq, unsigned cmd, unsigned n, ...)
{
    uint8_t m[64] = {0xf0, 0x7d, 'H', 'S', (uint8_t)version, (uint8_t)seq, (uint8_t)cmd};
    va_list args; va_start(args, n);
    for (unsigned i = 0; i < n; ++i) m[7 + i] = (uint8_t)va_arg(args, int);
    va_end(args);
    m[7 + n] = 0xf7;
    sent_length = 0;
    hindsight_message(m, 8 + n, FM1_MIDI_USB);
    return sent_length >= 9 && sent[6] == HS_ACK ? sent[7] : -1;
}
/* Version 1's TEXT: no background. */
static int text(unsigned x, unsigned y, unsigned scale, unsigned colour, const char *s)
{
    uint8_t m[64] = {0xf0, 0x7d, 'H', 'S', 1, 9, HS_TEXT_AT,
                     (uint8_t)(x & 127), (uint8_t)(x >> 7), (uint8_t)(y & 127), (uint8_t)(y >> 7),
                     (uint8_t)scale, (uint8_t)colour};
    unsigned n = (unsigned)strlen(s);
    memcpy(m + 13, s, n);
    m[13 + n] = 0xf7;
    sent_length = 0;
    hindsight_message(m, 14 + n, FM1_MIDI_USB);
    return sent_length >= 9 ? sent[7] : -1;
}

/* Version 2: a frame from its payload, sent on USB; returns the ack's status. */
static uint8_t frame[HS_FRAME + 16];
static int send2(unsigned cmd, const uint8_t *payload, unsigned n)
{
    const uint8_t head[] = {0xf0, 0x7d, 'H', 'S', 2, 40, (uint8_t)cmd};
    memcpy(frame, head, sizeof head);
    memcpy(frame + 7, payload, n);
    frame[7 + n] = 0xf7;
    sent_length = 0;
    hindsight_message(frame, 8 + n, FM1_MIDI_USB);
    return sent_length >= 9 && sent[6] == HS_ACK ? sent[7] : -1;
}
/* A command's payload into p, as BATCH and the frames carry it. */
static unsigned rect(uint8_t *p, unsigned x, unsigned y, unsigned w, unsigned h)
{
    const uint8_t r[] = {x & 127, x >> 7, y & 127, y >> 7, w & 127, w >> 7, h & 127, h >> 7};
    memcpy(p, r, sizeof r);
    return sizeof r;
}
static unsigned box_payload(uint8_t *p, unsigned x, unsigned y, unsigned w, unsigned h, unsigned colour)
{
    unsigned n = rect(p, x, y, w, h);
    p[n++] = (uint8_t)colour;
    return n;
}
static unsigned bar_payload(uint8_t *p, unsigned x, unsigned y, unsigned w, unsigned h, unsigned value,
                            unsigned colour, unsigned back)
{
    unsigned n = rect(p, x, y, w, h);
    p[n++] = (uint8_t)value; p[n++] = (uint8_t)colour; p[n++] = (uint8_t)back;
    return n;
}
static unsigned text_payload(uint8_t *p, unsigned x, unsigned y, unsigned scale, unsigned colour, unsigned back,
                             const char *s)
{
    const uint8_t head[] = {x & 127, x >> 7, y & 127, y >> 7, scale, colour, back};
    memcpy(p, head, sizeof head);
    memcpy(p + sizeof head, s, strlen(s));
    return (unsigned)(sizeof head + strlen(s));
}
static int box(unsigned x, unsigned y, unsigned w, unsigned h, unsigned colour)
{
    uint8_t p[16];
    return send2(HS_BOX, p, box_payload(p, x, y, w, h, colour));
}
static int bar(unsigned x, unsigned y, unsigned w, unsigned h, unsigned value, unsigned colour, unsigned back)
{
    uint8_t p[16];
    return send2(HS_BAR, p, bar_payload(p, x, y, w, h, value, colour, back));
}
static int text2(unsigned x, unsigned y, unsigned scale, unsigned colour, unsigned back, const char *s)
{
    uint8_t p[64];
    return send2(HS_TEXT_AT, p, text_payload(p, x, y, scale, colour, back, s));
}
static int led(unsigned n, const uint8_t *p) { return send2(HS_LED, p, n); }
/* A BATCH being built: append <cmd> <n> <payload>. */
static uint8_t batch[HS_FRAME];
static unsigned batch_length;
static void add(unsigned cmd, const uint8_t *p, unsigned n)
{
    batch[batch_length++] = (uint8_t)cmd;
    batch[batch_length++] = (uint8_t)n;
    memcpy(batch + batch_length, p, n);
    batch_length += n;
}
static unsigned done(void) { return sent[8] | (unsigned)sent[9] << 7; }

int main(void)
{
    /* Ours, and nobody else's. */
    const uint8_t hello[] = {0xf0, 0x7d, 'H', 'S', 1, 5, 0, 0xf7};
    const uint8_t open_transfer[] = {0xf0, 0x7d, 'F', 'M', 2, 22, 0, 0, 0xf7};
    const uint8_t version2[] = {0xf0, 0x7d, 'H', 'S', 2, 5, 0, 0xf7};
    const uint8_t unterminated[] = {0xf0, 0x7d, 'H', 'S', 1, 5, 0, 0};
    CHECK(hindsight_frame(hello, sizeof hello));
    CHECK(!hindsight_frame(open_transfer, sizeof open_transfer));
    const uint8_t version3[] = {0xf0, 0x7d, 'H', 'S', 3, 5, 0, 0xf7};
    CHECK(hindsight_frame(version2, sizeof version2));
    CHECK(!hindsight_frame(version3, sizeof version3));
    CHECK(!hindsight_frame(unterminated, sizeof unterminated));
    CHECK(!hindsight_frame(hello, 7));

    /* HELLO is answered with the version and the room for items, its seq
     * echoed, in the version it was asked in: version 2, 96 and what it does
     * beyond, either way. */
    hindsight_message(hello, sizeof hello, FM1_MIDI_USB);
    const uint8_t hello_ack[] = {0xf0, 0x7d, 'H', 'S', 1, 5, 0x7f, HS_OK, 2, 96, HS_FEATURES, 0xf7};
    CHECK(HS_FEATURES == 7);
    CHECK(HS_VERSION == 2 && HS_ITEMS == 96);
    CHECK(sent_length == sizeof hello_ack && !memcmp(sent, hello_ack, sizeof hello_ack) && sent_port == FM1_MIDI_USB);
    hindsight_message(version2, sizeof version2, FM1_MIDI_USB);
    const uint8_t hello_ack2[] = {0xf0, 0x7d, 'H', 'S', 2, 5, 0x7f, HS_OK, 2, 96, HS_FEATURES, 0xf7};
    CHECK(sent_length == sizeof hello_ack2 && !memcmp(sent, hello_ack2, sizeof hello_ack2));
    CHECK(!hindsight_active());

    /* An ack is never answered: an echo can't become a loop. */
    sent_length = 0; sends = 0;
    hindsight_message(hello_ack, sizeof hello_ack, FM1_MIDI_USB);
    CHECK(sends == 0 && !hindsight_active());

    /* TEXT can't take the screen; only CLEAR can. */
    CHECK(text(8, 8, 1, 2, "EARLY") == HS_RELEASED && !hindsight_active() && !hindsight_item(0));

    /* CLEAR takes the screen. */
    unsigned revision = hindsight_revision();
    CHECK(message(1, HS_CLEAR, 1, 2) == HS_OK);
    CHECK(hindsight_active() && hindsight_background() == 2 && hindsight_revision() != revision);
    CHECK(!hindsight_item(0));

    /* TEXT adds, replaces at the same place, and removes with no text. */
    CHECK(text(200, 120, 2, 4, "Hello") == HS_OK);
    const struct hs_item *item = hindsight_item(0);
    CHECK(item && item->x == 200 && item->y == 120 && item->scale == 2 && item->colour == 4 && !strcmp(item->text, "Hello"));
    CHECK(text(200, 120, 1, 2, "Bye") == HS_OK && !strcmp(hindsight_item(0)->text, "Bye") && !hindsight_item(1));
    CHECK(text(8, 8, 1, 2, "TAPE 1") == HS_OK && hindsight_item(1));
    CHECK(text(200, 120, 1, 2, "") == HS_OK && !strcmp(hindsight_item(0)->text, "TAPE 1") && !hindsight_item(1));
    /* Removing from the middle keeps the order of the rest. */
    CHECK(text(8, 20, 1, 2, "B") == HS_OK && text(8, 32, 1, 2, "C") == HS_OK);
    CHECK(text(8, 20, 1, 2, "") == HS_OK && !strcmp(hindsight_item(0)->text, "TAPE 1") &&
          !strcmp(hindsight_item(1)->text, "C") && !hindsight_item(2));
    /* One glyph has to fit: 234 + 6 and 233 + 7 are 240; at scale 4, 212 + 28. */
    CHECK(text(234, 233, 1, 2, "x") == HS_OK);
    CHECK(text(0, 212, 4, 2, "x") == HS_OK);

    /* What doesn't fit the screen, the scales or the palette is refused, and changes nothing. */
    revision = hindsight_revision();
    CHECK(text(240, 0, 1, 2, "x") == HS_MALFORMED);
    CHECK(text(0, 240, 1, 2, "x") == HS_MALFORMED);
    CHECK(text(235, 0, 1, 2, "x") == HS_MALFORMED);
    CHECK(text(0, 234, 1, 2, "x") == HS_MALFORMED);
    CHECK(text(0, 213, 4, 2, "x") == HS_MALFORMED);
    CHECK(text(0, 0, 0, 2, "x") == HS_MALFORMED);
    CHECK(text(0, 0, 5, 2, "x") == HS_MALFORMED);
    CHECK(text(0, 0, 1, HS_V1_COLOURS, "x") == HS_MALFORMED); /* version 1's seven */
    CHECK(text(0, 0, 1, 2, "\x7f") == HS_MALFORMED);
    CHECK(text(0, 0, 1, 2, "0123456789012345678901234567890123456789x") == HS_MALFORMED);
    CHECK(text(0, 0, 1, 2, "0123456789012345678901234567890123456789") == HS_OK);
    CHECK(text(0, 0, 1, 2, "") == HS_OK);
    revision = hindsight_revision();
    CHECK(message(2, HS_CLEAR, 0) == HS_MALFORMED);
    CHECK(message(2, HS_CLEAR, 1, HS_V1_COLOURS) == HS_MALFORMED);
    CHECK(message(2, HS_CLEAR, 1, 0x80) == HS_MALFORMED);
    CHECK(message(2, HS_HELLO, 1, 0) == HS_MALFORMED);
    CHECK(message(2, HS_RELEASE, 1, 0) == HS_MALFORMED);
    CHECK(message(2, 0x10, 0) == HS_UNKNOWN);
    /* Version 2's commands are unknown to a version 1 frame. */
    CHECK(message(2, HS_BOX, 9, 0, 0, 0, 0, 1, 0, 1, 0, 2) == HS_UNKNOWN);
    CHECK(message(2, HS_BAR, 11, 0, 0, 0, 0, 1, 0, 1, 0, 64, 2, 0) == HS_UNKNOWN);
    CHECK(message(2, HS_LED, 4, 0, 1, 2, 3) == HS_UNKNOWN);
    CHECK(message(2, HS_BATCH, 0) == HS_UNKNOWN && sent_length == 9);
    CHECK(hindsight_revision() == revision && hindsight_active());

    /* A bad seq still gets a valid ack. */
    CHECK(message(0x80, HS_HELLO, 0) == HS_MALFORMED && sent[5] < 0x80);

    /* Damage: a change repaints the bands it covers, between its columns. */
    CHECK(message(10, HS_CLEAR, 1, 0) == HS_OK);
    for (unsigned b = 0; b < HS_BANDS; ++b) CHECK(hindsight_damage(b) == (240U << 8));
    for (unsigned b = 0; b < HS_BANDS; ++b) CHECK(hindsight_damage(b) == 0); /* asking forgets */
    /* Scale 2 at (30, 40): rows 40-53, band 2 then 3; 3 glyphs of 12 columns. */
    CHECK(text(30, 40, 2, 2, "ABC") == HS_OK);
    CHECK(hindsight_damage(1) == 0 && hindsight_damage(2) == (30U | 66U << 8) &&
          hindsight_damage(3) == (30U | 66U << 8) && hindsight_damage(4) == 0);
    /* The same again changes nothing, so nothing is repainted. */
    revision = hindsight_revision();
    CHECK(text(30, 40, 2, 2, "ABC") == HS_OK && hindsight_revision() == revision);
    for (unsigned b = 0; b < HS_BANDS; ++b) CHECK(hindsight_damage(b) == 0);
    /* Shorter, then longer: the old text's place and the new one's. */
    CHECK(text(30, 40, 2, 2, "A") == HS_OK && hindsight_damage(2) == (30U | 66U << 8));
    CHECK(text(30, 40, 2, 2, "ABCD") == HS_OK && hindsight_damage(3) == (30U | 78U << 8));
    /* A new colour alone is a change. */
    CHECK(text(30, 40, 2, 3, "ABCD") == HS_OK && hindsight_damage(2) == (30U | 78U << 8));
    /* Two changes between asks: their union. */
    (void)hindsight_damage(3);
    CHECK(text(200, 32, 1, 2, "Z") == HS_OK && text(0, 34, 1, 2, "Y") == HS_OK);
    CHECK(hindsight_damage(2) == (0U | 206U << 8));
    /* Removing repaints where it was; removing nothing repaints nothing. */
    CHECK(text(30, 40, 2, 3, "") == HS_OK && hindsight_damage(2) == (30U | 78U << 8) &&
          hindsight_damage(3) == (30U | 78U << 8));
    CHECK(text(31, 40, 2, 3, "") == HS_OK);
    for (unsigned b = 0; b < HS_BANDS; ++b) CHECK(hindsight_damage(b) == 0);
    /* Text that runs off the right edge repaints only the glyphs that fit;
     * the bottom band is the last. */
    CHECK(text(228, 226, 2, 2, "WIDE") == HS_OK && hindsight_damage(14) == (228U | 240U << 8));
    /* What is refused, or released, repaints nothing. */
    CHECK(text(0, 0, 9, 2, "x") == HS_MALFORMED);
    CHECK(message(11, HS_RELEASE, 0) == HS_OK && text(0, 0, 1, 2, "x") == HS_RELEASED);
    for (unsigned b = 0; b < HS_BANDS; ++b) CHECK(hindsight_damage(b) == 0);

    /* 96 items, then the list is full. */
    CHECK(message(3, HS_CLEAR, 1, 0) == HS_OK && !hindsight_item(0));
    for (unsigned i = 0; i < HS_ITEMS; ++i) CHECK(text(30 * (i % 8), 14 * (i / 8), 1, 2, "ROW") == HS_OK);
    CHECK(text(100, 200, 1, 2, "ONE MORE") == HS_FULL && !hindsight_item(HS_ITEMS));
    CHECK(text(0, 0, 1, 2, "REPLACED") == HS_OK);

    /* RELEASE gives the screen back, and then TEXT changes nothing. */
    revision = hindsight_revision();
    CHECK(message(4, HS_RELEASE, 0) == HS_OK && !hindsight_active() && hindsight_revision() != revision);
    revision = hindsight_revision();
    CHECK(text(0, 0, 1, 2, "LATE") == HS_RELEASED && hindsight_revision() == revision &&
          !strcmp(hindsight_item(0)->text, "REPLACED"));

    /* HOME gives the screen back and tells the port that sent the CLEAR, once. */
    uint8_t clear_ble[] = {0xf0, 0x7d, 'H', 'S', 1, 8, HS_CLEAR, 0, 0xf7};
    hindsight_message(clear_ble, sizeof clear_ble, FM1_MIDI_BLE);
    CHECK(hindsight_active());
    sent_length = 0;
    hindsight_release();
    const uint8_t release[] = {0xf0, 0x7d, 'H', 'S', 1, 0, HS_RELEASE, 0xf7};
    CHECK(!hindsight_active() && sent_port == FM1_MIDI_BLE && sent_length == sizeof release &&
          !memcmp(sent, release, sizeof release));
    sent_length = 0;
    hindsight_release();
    CHECK(sent_length == 0);
    /* The keyboard's RELEASE, echoed back, is answered once and ends there. */
    CHECK(message(0, HS_RELEASE, 0) == HS_OK && !hindsight_active());

    /* HOME held offers the screen to the port that last took it, only while
     * the screen is the keyboard's; an OFFER sent to the keyboard is UNKNOWN. */
    sent_length = 0;
    hindsight_offer();
    const uint8_t offer[] = {0xf0, 0x7d, 'H', 'S', 1, 0, HS_OFFER, 0xf7};
    CHECK(sent_port == FM1_MIDI_BLE && sent_length == sizeof offer && !memcmp(sent, offer, sizeof offer));
    CHECK(!hindsight_active());
    hindsight_message(clear_ble, sizeof clear_ble, FM1_MIDI_BLE);
    sent_length = 0;
    hindsight_offer();
    CHECK(sent_length == 0 && hindsight_active());
    hindsight_release();
    CHECK(message(0, HS_OFFER, 0) == HS_UNKNOWN && !hindsight_active());

    /* A busy port is retried, then given up on. */
    busy = 1; sends = 0; clock_ms = 1000;
    CHECK(message(6, HS_HELLO, 0) == -1 && sends == 1);
    hindsight_poll();
    CHECK(sends == 2);
    busy = 0; clock_ms = 1100;
    hindsight_poll();
    CHECK(sends == 3 && sent[5] == 6);
    hindsight_poll();
    CHECK(sends == 3);
    busy = 1; clock_ms = 2000;
    (void)message(7, HS_HELLO, 0);
    clock_ms = 2600; hindsight_poll();
    clock_ms = 2700; hindsight_poll();
    CHECK(sends == 5);
    busy = 0;

    /* The surface. Before the Pi has the screen, everything is Open's. */
    CHECK(!hindsight_active());
    CHECK(hindsight_surface(SMK37_PAD, 0, 100, 0) == 0);           /* Open's press... */
    CHECK(hindsight_surface(SMK37_ENCODER, 0, 1, 0) == 0);
    uint8_t clear_usb[] = {0xf0, 0x7d, 'H', 'S', 1, 20, HS_CLEAR, 0, 0xf7};
    hindsight_message(clear_usb, sizeof clear_usb, FM1_MIDI_USB);
    CHECK(hindsight_surface(SMK37_PAD, 0, 0, 0) == 0);             /* ...and Open's release */
    CHECK(hindsight_surface(SMK37_PRESSURE, 0, 50, 0) == 0);

    /* Pads: notes 36-51 on channel 10, poly aftertouch while held. */
    sent_length = 0;
    CHECK(hindsight_surface(SMK37_PAD, 3, 100, 0) == 1);
    CHECK(sent_length == 3 && sent[0] == 0x99 && sent[1] == 39 && sent[2] == 100 && sent_port == FM1_MIDI_USB);
    CHECK(hindsight_surface(SMK37_PRESSURE, 3, 64, 0) == 1 && sent[0] == 0xa9 && sent[1] == 39 && sent[2] == 64);
    CHECK(hindsight_surface(SMK37_PAD, 15, 200, 0) == 1 && sent[1] == 51 && sent[2] == 127); /* clamped */

    /* Buttons: note <id> on channel 16; HOME and FADER BANK stay the keyboard's. */
    CHECK(hindsight_surface(SMK37_BUTTON, 16, 1, 0) == 1 && sent[0] == 0x9f && sent[1] == 16 && sent[2] == 127);
    CHECK(hindsight_surface(SMK37_BUTTON, 16, 0, 0) == 1 && sent[0] == 0x8f && sent[1] == 16 && sent[2] == 0);
    CHECK(hindsight_surface(SMK37_BUTTON, 2, 1, 0) == 0 && hindsight_surface(SMK37_BUTTON, 12, 1, 0) == 0);
    CHECK(hindsight_surface(SMK37_BUTTON, 1, 1, 0) == 0);

    /* Encoders: CC 21-27 on channel 16, the knob's value, which KNOB sets. */
    CHECK(message(21, HS_KNOB, 2, 0, 120) == HS_OK);
    CHECK(hindsight_surface(SMK37_ENCODER, 0, 3, 0) == 1 && sent[0] == 0xbf && sent[1] == 21 && sent[2] == 123);
    CHECK(hindsight_surface(SMK37_ENCODER, 0, 10, 0) == 1 && sent[2] == 127);
    sent_length = 0;
    CHECK(hindsight_surface(SMK37_ENCODER, 0, 1, 0) == 1 && sent_length == 0); /* at the end: nothing to say */
    CHECK(message(23, HS_KNOB, 2, 6, 3) == HS_OK);
    CHECK(hindsight_surface(SMK37_ENCODER, 6, -5, 0) == 1 && sent[1] == 27 && sent[2] == 0);
    CHECK(message(22, HS_KNOB, 2, HS_KNOBS, 0) == HS_MALFORMED);
    CHECK(message(22, HS_KNOB, 1, 0) == HS_MALFORMED);

    /* Faders: CC 28-31 on channel 16, 32-35 in the second bank. */
    CHECK(hindsight_surface(SMK37_FADER, 2, 90, 0) == 1 && sent[0] == 0xbf && sent[1] == 30 && sent[2] == 90);
    CHECK(hindsight_surface(SMK37_FADER, 2, 90, 1) == 1 && sent[1] == 34);

    /* The keys, the wheels, the pedal and the master knob play the synth. */
    CHECK(hindsight_surface(SMK37_KEY, 10, 100, 0) == 0);
    CHECK(hindsight_surface(SMK37_FADER, 4, 70, 0) == 0);
    CHECK(hindsight_surface(SMK37_PEDAL, 0, 127, 0) == 0);
    CHECK(hindsight_surface(SMK37_ENCODER, 7, 1, 0) == 0);

    /* HOME: what the Pi was pressing is still let go to the Pi; new presses are Open's. */
    hindsight_release();
    CHECK(hindsight_surface(SMK37_PRESSURE, 3, 30, 0) == 1 && sent[0] == 0xa9);
    CHECK(hindsight_surface(SMK37_PAD, 3, 0, 0) == 1 && sent[0] == 0x89 && sent[1] == 39 && sent[2] == 0);
    CHECK(hindsight_surface(SMK37_PAD, 15, 0, 0) == 1 && sent[1] == 51);
    CHECK(hindsight_surface(SMK37_PAD, 3, 100, 0) == 0);
    CHECK(hindsight_surface(SMK37_ENCODER, 0, 1, 0) == 0 && hindsight_surface(SMK37_FADER, 0, 1, 0) == 0);

    /* A held HOME's touches: presses and turns, HOME's own and releases aside. */
    unsigned touches = hindsight_touches();
    hindsight_surface(SMK37_BUTTON, 2, 1, 0);
    hindsight_surface(SMK37_BUTTON, 12, 1, 0);
    hindsight_surface(SMK37_KEY, 3, 0, 0);
    hindsight_surface(SMK37_FADER, 1, 90, 0);
    hindsight_surface(SMK37_PRESSURE, 3, 40, 0);
    CHECK(hindsight_touches() == touches);
    hindsight_surface(SMK37_KEY, 3, 100, 0);
    hindsight_surface(SMK37_PAD, 4, 100, 0);
    hindsight_surface(SMK37_BUTTON, 0, 1, 0); /* SHIFT */
    hindsight_surface(SMK37_ENCODER, 7, -1, 0); /* the master knob */
    CHECK(hindsight_touches() == touches + 4);

    /* Version 2. CLEAR and TEXT take the whole palette, and TEXT a background. */
    version = 2;
    CHECK(message(50, HS_CLEAR, 1, 15) == HS_OK && hindsight_background() == 15);
    CHECK(message(50, HS_CLEAR, 1, HS_COLOURS) == HS_MALFORMED);
    CHECK(message(51, HS_CLEAR, 1, 0) == HS_OK);
    CHECK(text2(8, 8, 2, 14, 11, "PUNCH IN") == HS_OK);
    item = hindsight_item(0);
    CHECK(item && item->kind == HS_TEXT_AT && item->colour == 14 && item->back == 11 &&
          item->w == 96 && item->h == 14 && !strcmp(item->text, "PUNCH IN"));
    CHECK(text2(8, 8, 2, 14, HS_COLOURS, "X") == HS_MALFORMED);
    CHECK(text2(8, 8, 2, HS_COLOURS, 0, "X") == HS_MALFORMED);
    {
        const uint8_t no_back[] = {8, 0, 8, 0, 2, 14};
        CHECK(send2(HS_TEXT_AT, no_back, sizeof no_back) == HS_MALFORMED);
    }
    /* Version 1's TEXT is drawn on CLEAR's colour. */
    CHECK(message(52, HS_CLEAR, 1, 3) == HS_OK && text(0, 0, 1, 2, "V1") == HS_OK &&
          hindsight_item(0)->back == 3);

    /* BOX fills; the same place again replaces it; w or h 0 removes it. */
    CHECK(message(53, HS_CLEAR, 1, 0) == HS_OK);
    for (unsigned b = 0; b < HS_BANDS; ++b) (void)hindsight_damage(b);
    CHECK(box(162, 34, 74, 28, 5) == HS_OK);
    item = hindsight_item(0);
    CHECK(item && item->kind == HS_BOX && item->x == 162 && item->y == 34 && item->w == 74 && item->h == 28 &&
          item->colour == 5 && item->back == 5 && item->value == 127);
    CHECK(hindsight_damage(1) == 0 && hindsight_damage(2) == (162U | 236U << 8) &&
          hindsight_damage(3) == (162U | 236U << 8) && hindsight_damage(4) == 0);
    revision = hindsight_revision();
    CHECK(box(162, 34, 74, 28, 5) == HS_OK && hindsight_revision() == revision && !hindsight_damage(2));
    CHECK(box(162, 34, 74, 28, 11) == HS_OK && hindsight_item(0)->colour == 11 && !hindsight_item(1));
    /* A TEXT at a BOX's place is an item of its own, drawn over it. */
    CHECK(text2(162, 34, 1, 2, 11, "REC") == HS_OK && hindsight_item(1) && hindsight_item(1)->kind == HS_TEXT_AT);
    CHECK(box(162, 34, 0, 28, 11) == HS_OK && hindsight_item(0)->kind == HS_TEXT_AT && !hindsight_item(1));
    CHECK(box(0, 0, 240, 240, 1) == HS_OK); /* the whole screen fits */
    CHECK(box(0, 0, 241, 1, 1) == HS_MALFORMED);
    CHECK(box(239, 0, 2, 1, 1) == HS_MALFORMED);
    CHECK(box(0, 239, 1, 2, 1) == HS_MALFORMED);
    CHECK(box(240, 0, 0, 0, 1) == HS_MALFORMED);
    CHECK(box(0, 0, 1, 1, HS_COLOURS) == HS_MALFORMED);
    {
        uint8_t p[16]; unsigned n = box_payload(p, 0, 0, 1, 1, 1);
        CHECK(send2(HS_BOX, p, n - 1) == HS_MALFORMED && send2(HS_BOX, p, n) == HS_OK);
    }

    /* BAR: value 0-127 of its width in colour, the rest in back. */
    CHECK(message(54, HS_CLEAR, 1, 0) == HS_OK);
    CHECK(bar(16, 137, 180, 5, 64, 7, 1) == HS_OK);
    item = hindsight_item(0);
    CHECK(item && item->kind == HS_BAR && item->value == 64 && item->colour == 7 && item->back == 1 && item->w == 180);
    for (unsigned b = 0; b < HS_BANDS; ++b) (void)hindsight_damage(b);
    CHECK(bar(16, 137, 180, 5, 100, 7, 1) == HS_OK && hindsight_item(0)->value == 100 && !hindsight_item(1));
    CHECK(hindsight_damage(8) == (16U | 196U << 8) && hindsight_damage(9) == 0);
    /* A BAR and a BOX at one place are two items. */
    CHECK(box(16, 137, 2, 7, 3) == HS_OK && hindsight_item(1) && hindsight_item(1)->kind == HS_BOX);
    /* A replaced item keeps its place in the order. */
    CHECK(bar(16, 137, 180, 5, 0, 7, 1) == HS_OK && hindsight_item(0)->kind == HS_BAR &&
          hindsight_item(0)->value == 0);
    CHECK(bar(16, 137, 180, 0, 0, 7, 1) == HS_OK && hindsight_item(0)->kind == HS_BOX && !hindsight_item(1));
    CHECK(bar(0, 0, 10, 1, 64, HS_COLOURS, 0) == HS_MALFORMED && bar(0, 0, 10, 1, 64, 0, HS_COLOURS) == HS_MALFORMED);

    /* LED: a pad's RGB and a button's light, while the Pi has the screen. */
    CHECK(hindsight_pad(4, 0x040804) == 0x040804 && hindsight_button(16) == -1);
    {
        const uint8_t pad[] = {4, 0x7c, 0x3e, 0x14}, play[] = {16 + 16, 1}, rec[] = {16 + 17, 2},
                      shift[] = {16 + 0, 1}, shift_off[] = {16 + 0, 0};
        CHECK(led(sizeof pad, pad) == HS_OK && hindsight_pad(4, 0x040804) == 0x2f1708);
        CHECK(hindsight_pad(5, 0x040804) == 0x040804);
        /* 0-127 is off to full, and full is Open's brightest, 48. */
        const uint8_t full[] = {5, 127, 64, 0}, one[] = {5, 1, 2, 3};
        CHECK(led(sizeof full, full) == HS_OK && hindsight_pad(5, 0) == 0x301800);
        CHECK(led(sizeof one, one) == HS_OK && hindsight_pad(5, 0) == 0x000101);
        CHECK(led(sizeof play, play) == HS_OK && hindsight_button(16) == 1);
        CHECK(led(sizeof shift, shift) == HS_OK && hindsight_button(0) == 1);
        CHECK(led(sizeof shift_off, shift_off) == HS_OK && hindsight_button(0) == 0);
        /* Blinking: on 250 ms, off 250 ms. */
        clock_ms = 250; CHECK(led(sizeof rec, rec) == HS_OK && hindsight_button(17) == 1);
        clock_ms = 499; CHECK(hindsight_button(17) == 1);
        clock_ms = 500; CHECK(hindsight_button(17) == 0);
        clock_ms = 750; CHECK(hindsight_button(17) == 1);
        CHECK(hindsight_button(16) == 1); /* on stays on */
        /* HOME and FADER BANK are the keyboard's; there is no state 3, no pad 16. */
        const uint8_t home[] = {16 + 2, 1}, home2[] = {16 + 12, 1}, bank[] = {16 + 1, 1}, three[] = {16 + 16, 3},
                      button_rgb[] = {16, 1, 1, 1}, pad_state[] = {4, 1}, past[] = {16 + 18, 1};
        CHECK(led(sizeof home, home) == HS_MALFORMED && led(sizeof home2, home2) == HS_MALFORMED);
        CHECK(led(sizeof bank, bank) == HS_MALFORMED && led(sizeof three, three) == HS_MALFORMED);
        CHECK(led(sizeof button_rgb, button_rgb) == HS_MALFORMED && led(sizeof pad_state, pad_state) == HS_MALFORMED);
        CHECK(led(sizeof past, past) == HS_MALFORMED && led(0, pad) == HS_MALFORMED);
        /* A CLEAR keeps the lights; giving the screen back gives them back. */
        CHECK(message(55, HS_CLEAR, 1, 0) == HS_OK && hindsight_button(16) == 1);
        hindsight_release();
        CHECK(hindsight_pad(4, 0x040804) == 0x040804 && hindsight_button(16) == -1 && hindsight_button(17) == -1);
        CHECK(led(sizeof pad, pad) == HS_RELEASED && hindsight_pad(4, 1) == 1);
        CHECK(box(0, 0, 1, 1, 1) == HS_RELEASED && bar(0, 0, 1, 1, 1, 1, 1) == HS_RELEASED);
        CHECK(message(56, HS_CLEAR, 1, 0) == HS_OK && hindsight_pad(4, 1) == 1 && hindsight_button(16) == -1);
        CHECK(led(sizeof play, play) == HS_OK && message(57, HS_RELEASE, 0) == HS_OK && hindsight_button(16) == -1);
    }

    /* Local echo: a pad pressed is half-lit at once, until the Pi sets it or
     * HS_ECHO_MS passes; a knob turned writes its value where KNOB said. */
    {
        const uint8_t rest[] = {2, 23, 12, 0}, half[] = {2, 70, 35, 0}, white[] = {3, 127, 127, 127};
        clock_ms = 1000;
        CHECK(message(58, HS_CLEAR, 1, 0) == HS_OK && led(sizeof rest, rest) == HS_OK && led(sizeof white, white) == HS_OK);
        CHECK(hindsight_pad(2, 0) == 0x090500);
        CHECK(hindsight_surface(SMK37_PAD, 2, 100, 0) == 1 && hindsight_pad(2, 0) == 0x1b0f00); /* three times */
        CHECK(hindsight_surface(SMK37_PAD, 3, 100, 0) == 1 && hindsight_pad(3, 0) == 0x303030); /* at most full */
        CHECK(hindsight_pad(5, 7) == 7);                                    /* not the Pi's: Open's */
        clock_ms = 1000 + HS_ECHO_MS - 1; CHECK(hindsight_pad(2, 0) == 0x1b0f00);
        CHECK(led(sizeof half, half) == HS_OK && hindsight_pad(2, 0) == 0x1a0d00); /* the Pi's answer */
        CHECK(hindsight_surface(SMK37_PAD, 2, 0, 0) == 1 && hindsight_surface(SMK37_PAD, 3, 0, 0) == 1);
        clock_ms = 2000;
        CHECK(hindsight_surface(SMK37_PAD, 2, 90, 0) == 1 && hindsight_pad(2, 0) == 0x302700);
        clock_ms = 2000 + HS_ECHO_MS; CHECK(hindsight_pad(2, 0) == 0x1a0d00); /* no answer: put back */
        CHECK(hindsight_surface(SMK37_PAD, 2, 0, 0) == 1);
        hindsight_release();
        CHECK(hindsight_surface(SMK37_PAD, 2, 90, 0) == 0 && hindsight_pad(2, 4) == 4);
        CHECK(hindsight_surface(SMK37_PAD, 2, 0, 0) == 0);

        /* The knob: its value's TEXT at (64, 186) and its BAR at (64, 202). */
        CHECK(message(59, HS_CLEAR, 1, 0) == HS_OK);
        CHECK(text2(64, 186, 2, 2, 0, "40") == HS_OK && bar(64, 202, 52, 2, 40, 3, 1) == HS_OK);
        for (unsigned b = 0; b < HS_BANDS; ++b) (void)hindsight_damage(b);
        CHECK(message(60, HS_KNOB, 10, 1, 40, 64, 0, 58, 1, 64, 0, 74, 1) == HS_OK);
        unsigned before = hindsight_revision();
        CHECK(hindsight_surface(SMK37_ENCODER, 1, 60, 0) == 1 && sent[1] == 22 && sent[2] == 100);
        CHECK(!strcmp(hindsight_item(0)->text, "100") && hindsight_item(0)->w == 36 && hindsight_item(1)->value == 100);
        CHECK(hindsight_revision() != before && hindsight_damage(11) == (64 | (64 + 36) << 8) &&
              hindsight_damage(12) == (64 | (64 + 52) << 8) && !hindsight_damage(10));
        CHECK(hindsight_surface(SMK37_ENCODER, 1, -95, 0) == 1 && !strcmp(hindsight_item(0)->text, "5") &&
              hindsight_item(0)->w == 12 && hindsight_item(1)->value == 5);
        /* Only where it said: a knob with no places echoes nothing. */
        CHECK(message(61, HS_KNOB, 2, 2, 9) == HS_OK);
        before = hindsight_revision();
        CHECK(hindsight_surface(SMK37_ENCODER, 2, 1, 0) == 1 && hindsight_revision() == before);
        CHECK(message(62, HS_KNOB, 10, 1, 40, 112, 1, 0, 0, 0, 0) == HS_MALFORMED);   /* x 240 */
        CHECK(message(62, HS_KNOB, 9, 1, 40, 64, 0, 58, 1, 64, 0, 74) == HS_MALFORMED);
        /* A KNOB without places withdraws them; a CLEAR forgets them. */
        CHECK(message(64, HS_KNOB, 2, 1, 5) == HS_OK);
        before = hindsight_revision();
        CHECK(hindsight_surface(SMK37_ENCODER, 1, 1, 0) == 1 && hindsight_revision() == before);
        CHECK(message(65, HS_KNOB, 10, 1, 40, 64, 0, 58, 1, 64, 0, 74, 1) == HS_OK);
        CHECK(message(66, HS_CLEAR, 1, 0) == HS_OK && text2(64, 186, 2, 2, 0, "40") == HS_OK);
        CHECK(hindsight_surface(SMK37_ENCODER, 1, 1, 0) == 1 && !strcmp(hindsight_item(0)->text, "40"));
        /* Places are version 2's. */
        version = 1;
        CHECK(message(67, HS_KNOB, 10, 1, 40, 64, 0, 58, 1, 64, 0, 74, 1) == HS_MALFORMED);
        version = 2;
        /* Giving the screen back forgets the places. */
        hindsight_release();
        CHECK(message(63, HS_CLEAR, 1, 0) == HS_OK && text2(64, 186, 2, 2, 0, "40") == HS_OK);
        CHECK(hindsight_surface(SMK37_ENCODER, 1, 1, 0) == 1 && !strcmp(hindsight_item(0)->text, "40"));

        /* Open's clock and transport don't reach the port that has the screen. */
        CHECK(hindsight_quiet(FM1_MIDI_USB) && !hindsight_quiet(FM1_MIDI_USB + 1));
        hindsight_release();
        CHECK(!hindsight_quiet(FM1_MIDI_USB));
    }

    /* BATCH: several commands, one ack, which says how many were done. */
    batch_length = 0;
    {
        uint8_t p[64]; unsigned n;
        const uint8_t clear[] = {0}, pad[] = {0, 10, 20, 30};
        add(HS_CLEAR, clear, 1);
        n = box_payload(p, 0, 0, 240, 28, 11); add(HS_BOX, p, n);
        n = text_payload(p, 8, 7, 2, 14, 11, "PUNCH IN T2"); add(HS_TEXT_AT, p, n);
        n = bar_payload(p, 16, 137, 180, 5, 90, 7, 1); add(HS_BAR, p, n);
        add(HS_LED, pad, 4);
        CHECK(send2(HS_BATCH, batch, batch_length) == HS_OK && sent_length == 11 && done() == 5);
        CHECK(sent[4] == 2 && sent[5] == 40 && sent[10] == 0xf7);
        CHECK(hindsight_active() && hindsight_item(2) && !hindsight_item(3) && hindsight_item(1)->kind == HS_TEXT_AT &&
              hindsight_pad(0, 0) == 0x04080b);
        /* It stops at the first that isn't OK; those before it stay done. */
        batch_length = 0;
        n = box_payload(p, 0, 200, 10, 10, 2); add(HS_BOX, p, n);
        n = box_payload(p, 0, 0, 241, 10, 2); add(HS_BOX, p, n);
        n = box_payload(p, 20, 200, 10, 10, 2); add(HS_BOX, p, n);
        CHECK(send2(HS_BATCH, batch, batch_length) == HS_MALFORMED && done() == 1);
        CHECK(hindsight_item(3) && hindsight_item(3)->y == 200 && !hindsight_item(4));
        /* A command running past the frame, a HELLO and a BATCH inside one. */
        const uint8_t short_cmd[] = {HS_KNOB, 3, 0, 1}, one_byte[] = {HS_KNOB}, hello_in[] = {HS_HELLO, 0},
                      batch_in[] = {HS_BATCH, 0}, knob_then_bad[] = {HS_KNOB, 2, 0, 5, HS_KNOB, 2, HS_KNOBS, 5};
        CHECK(send2(HS_BATCH, short_cmd, sizeof short_cmd) == HS_MALFORMED && done() == 0);
        CHECK(send2(HS_BATCH, one_byte, sizeof one_byte) == HS_MALFORMED && done() == 0);
        /* A last command one byte short: without the bound it would take F7 as its value. */
        const uint8_t knob_short[] = {HS_KNOB, 2, 0};
        CHECK(send2(HS_BATCH, knob_short, sizeof knob_short) == HS_MALFORMED && done() == 0);
        const uint8_t knob_then_one[] = {HS_KNOB, 2, 1, 9, HS_KNOB};
        CHECK(send2(HS_BATCH, knob_then_one, sizeof knob_then_one) == HS_MALFORMED && done() == 1);
        const uint8_t empty_clear[] = {HS_CLEAR, 0};  /* a command's own length rules are its own */
        CHECK(send2(HS_BATCH, empty_clear, sizeof empty_clear) == HS_MALFORMED && done() == 0);
        CHECK(send2(HS_BATCH, hello_in, sizeof hello_in) == HS_UNKNOWN);
        CHECK(send2(HS_BATCH, batch_in, sizeof batch_in) == HS_UNKNOWN);
        CHECK(send2(HS_BATCH, knob_then_bad, sizeof knob_then_bad) == HS_MALFORMED && done() == 1);
        CHECK(send2(HS_BATCH, NULL, 0) == HS_OK && done() == 0);
        /* RELEASED inside a BATCH, after HOME. */
        hindsight_release();
        batch_length = 0;
        n = box_payload(p, 0, 0, 1, 1, 1); add(HS_BOX, p, n);
        CHECK(send2(HS_BATCH, batch, batch_length) == HS_RELEASED && done() == 0);
        /* A whole screen in one frame: 90 boxes, 998 bytes F0 to F7. */
        batch_length = 0;
        add(HS_CLEAR, clear, 1);
        for (unsigned i = 0; i < 89; ++i) { n = box_payload(p, (i % 8) * 30, (i / 8) * 16, 28, 14, 1 + i % 15); add(HS_BOX, p, n); }
        CHECK(8 + batch_length == 8 + 3 + 89 * 11 && 8 + batch_length <= HS_FRAME);
        CHECK(send2(HS_BATCH, batch, batch_length) == HS_OK && done() == 90 && hindsight_item(88) && !hindsight_item(89));
        /* Past the 96th item, FULL; done counts past 127 in two bytes. */
        batch_length = 0;
        for (unsigned i = 0; i < 8; ++i) { n = box_payload(p, 200, 200 + i, 1, 1, 1); add(HS_BOX, p, n); }
        CHECK(send2(HS_BATCH, batch, batch_length) == HS_FULL && done() == 7 && hindsight_item(95) && !hindsight_item(96));
        batch_length = 0;
        for (unsigned i = 0; i < 200; ++i) add(HS_KNOB, (const uint8_t[]){0, (uint8_t)(i & 127)}, 2);
        CHECK(send2(HS_BATCH, batch, batch_length) == HS_OK && done() == 200 && sent[8] == (200 & 127) && sent[9] == 1);
        /* Longer than the SDK delivers: not ours. */
        CHECK(!hindsight_frame(frame, HS_FRAME + 1));
    }
    version = 1;

    /* Only three ports. */
    sent_length = 0;
    hindsight_message(hello, sizeof hello, 3);
    CHECK(sent_length == 0);

    if (failures) { printf("hindsight: %d failed\n", failures); return 1; }
    printf("hindsight: ok\n");
    return 0;
}
