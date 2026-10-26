/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Added 2026-10-26 for Hindsight: the Pi's SysEx protocol, version 1.
 *
 *   F0 7D 48 53 01 <seq> <cmd> <payload> F7      48 53 is "HS"; 01 the version
 *
 *   00 HELLO                                       is this our firmware?
 *   01 CLEAR   <colour>                            take the screen, fill it
 *   02 TEXT    <x lo> <x hi> <y lo> <y hi> <scale> <colour> <ASCII...>
 *              puts text at (x, y), replacing what was there; no text removes it
 *   03 RELEASE                                     give the screen back
 *
 * Coordinates are pixels, x = lo | hi << 7. Scale is 1 to 4 (a glyph is
 * 6 by 7 pixels at 1). Colours index the keyboard's palette (HS_COLOURS).
 * Every message is answered on the port it came from:
 *
 *   F0 7D 48 53 01 <seq> 7F <status> F7           HELLO adds <version> <items>
 *
 * The seq comes back, so the Pi can time a message to its ack. The keyboard
 * draws from this model on its own control task (screen.c); nothing here
 * touches the screen or Flash. HOME on the keyboard gives the screen back. */
#ifndef HINDSIGHT_H
#define HINDSIGHT_H
#include <stddef.h>
#include <stdint.h>

enum { HS_VERSION = 1, HS_ITEMS = 16, HS_TEXT = 40, HS_COLOURS = 7 };
enum { HS_HELLO = 0, HS_CLEAR = 1, HS_TEXT_AT = 2, HS_RELEASE = 3, HS_ACK = 0x7f };
enum { HS_OK = 0, HS_UNKNOWN = 1, HS_MALFORMED = 2, HS_FULL = 3 };

struct hs_item {
    uint8_t x, y, scale, colour;
    char text[HS_TEXT + 1];
};

/* Whether a complete SysEx frame is one of ours: the mailbox takes it. */
int hindsight_frame(const uint8_t *message, size_t bytes);
/* Control task: apply a frame of ours and queue its ack to the port. */
void hindsight_message(const uint8_t *message, size_t bytes, unsigned port);
/* Control task: retry an ack the port was too busy for. */
void hindsight_poll(void);

/* The model screen.c draws. The revision changes whenever it does. */
unsigned hindsight_active(void);
void hindsight_release(void);
unsigned hindsight_revision(void);
unsigned hindsight_background(void);
const struct hs_item *hindsight_item(unsigned index);

#endif
