/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Host test for app/hindsight.c: the Pi's SysEx protocol. Built by tests/run.sh. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "hindsight.h"
#include "fm1.h"

static uint8_t sent[32];
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

/* Sends F0 7D 48 53 01 seq cmd payload... F7 on USB; returns the ack's status. */
static int message(unsigned seq, unsigned cmd, unsigned n, ...)
{
    uint8_t m[64] = {0xf0, 0x7d, 'H', 'S', HS_VERSION, (uint8_t)seq, (uint8_t)cmd};
    va_list args; va_start(args, n);
    for (unsigned i = 0; i < n; ++i) m[7 + i] = (uint8_t)va_arg(args, int);
    va_end(args);
    m[7 + n] = 0xf7;
    sent_length = 0;
    hindsight_message(m, 8 + n, FM1_MIDI_USB);
    return sent_length >= 9 && sent[6] == HS_ACK ? sent[7] : -1;
}
static int text(unsigned x, unsigned y, unsigned scale, unsigned colour, const char *s)
{
    uint8_t m[64] = {0xf0, 0x7d, 'H', 'S', HS_VERSION, 9, HS_TEXT_AT,
                     (uint8_t)(x & 127), (uint8_t)(x >> 7), (uint8_t)(y & 127), (uint8_t)(y >> 7),
                     (uint8_t)scale, (uint8_t)colour};
    unsigned n = (unsigned)strlen(s);
    memcpy(m + 13, s, n);
    m[13 + n] = 0xf7;
    sent_length = 0;
    hindsight_message(m, 14 + n, FM1_MIDI_USB);
    return sent_length >= 9 ? sent[7] : -1;
}

int main(void)
{
    /* Ours, and nobody else's. */
    const uint8_t hello[] = {0xf0, 0x7d, 'H', 'S', 1, 5, 0, 0xf7};
    const uint8_t open_transfer[] = {0xf0, 0x7d, 'F', 'M', 2, 22, 0, 0, 0xf7};
    const uint8_t version2[] = {0xf0, 0x7d, 'H', 'S', 2, 5, 0, 0xf7};
    const uint8_t unterminated[] = {0xf0, 0x7d, 'H', 'S', 1, 5, 0, 0};
    CHECK(hindsight_frame(hello, sizeof hello));
    CHECK(!hindsight_frame(open_transfer, sizeof open_transfer));
    CHECK(!hindsight_frame(version2, sizeof version2));
    CHECK(!hindsight_frame(unterminated, sizeof unterminated));
    CHECK(!hindsight_frame(hello, 7));

    /* HELLO is answered with the version and the room for items, its seq echoed. */
    hindsight_message(hello, sizeof hello, FM1_MIDI_USB);
    const uint8_t hello_ack[] = {0xf0, 0x7d, 'H', 'S', 1, 5, 0x7f, HS_OK, HS_VERSION, HS_ITEMS, 0xf7};
    CHECK(sent_length == sizeof hello_ack && !memcmp(sent, hello_ack, sizeof hello_ack) && sent_port == FM1_MIDI_USB);
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
    CHECK(text(0, 0, 1, HS_COLOURS, "x") == HS_MALFORMED);
    CHECK(text(0, 0, 1, 2, "\x7f") == HS_MALFORMED);
    CHECK(text(0, 0, 1, 2, "0123456789012345678901234567890123456789x") == HS_MALFORMED);
    CHECK(text(0, 0, 1, 2, "0123456789012345678901234567890123456789") == HS_OK);
    CHECK(text(0, 0, 1, 2, "") == HS_OK);
    revision = hindsight_revision();
    CHECK(message(2, HS_CLEAR, 0) == HS_MALFORMED);
    CHECK(message(2, HS_CLEAR, 1, HS_COLOURS) == HS_MALFORMED);
    CHECK(message(2, HS_CLEAR, 1, 0x80) == HS_MALFORMED);
    CHECK(message(2, HS_HELLO, 1, 0) == HS_MALFORMED);
    CHECK(message(2, HS_RELEASE, 1, 0) == HS_MALFORMED);
    CHECK(message(2, 0x10, 0) == HS_UNKNOWN);
    CHECK(hindsight_revision() == revision && hindsight_active());

    /* A bad seq still gets a valid ack. */
    CHECK(message(0x80, HS_HELLO, 0) == HS_MALFORMED && sent[5] < 0x80);

    /* Sixteen items, then the list is full. */
    CHECK(message(3, HS_CLEAR, 1, 0) == HS_OK && !hindsight_item(0));
    for (unsigned i = 0; i < HS_ITEMS; ++i) CHECK(text(0, 14 * i, 1, 2, "ROW") == HS_OK);
    CHECK(text(100, 0, 1, 2, "ONE MORE") == HS_FULL && !hindsight_item(HS_ITEMS));
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

    /* Only three ports. */
    sent_length = 0;
    hindsight_message(hello, sizeof hello, 3);
    CHECK(sent_length == 0);

    if (failures) { printf("hindsight: %d failed\n", failures); return 1; }
    printf("hindsight: ok\n");
    return 0;
}
