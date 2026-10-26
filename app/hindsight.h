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
 *   04 KNOB    <knob 0-6> <value 0-127>            where an encoder stands
 *
 * Coordinates are pixels, x = lo | hi << 7. Scale is 1 to 4 (a glyph is
 * 6 by 7 pixels at 1); at least one glyph has to fit. Colours index the
 * keyboard's palette (HS_COLOURS). Only CLEAR takes the screen: TEXT while
 * the screen is the keyboard's answers RELEASED and changes nothing.
 * Every message is answered on the port it came from, and acks are never
 * answered:
 *
 *   F0 7D 48 53 01 <seq> 7F <status> F7           HELLO adds <version> <items>
 *
 * The seq comes back, so the Pi can time a message to its ack. Send one
 * message at a time and wait for its ack: the mailbox holds one frame, and
 * a second frame arriving meanwhile is lost. Resend on a timeout; every
 * command can be repeated safely. HOME on the keyboard gives the screen
 * back, and the keyboard says so to the port that sent the CLEAR; HOME held
 * alone for a second, on the keyboard's own screen, offers it to that port
 * (USB, before any CLEAR), which takes it with a CLEAR if it wants it:
 *
 *   F0 7D 48 53 01 00 03 F7                       RELEASE, from the keyboard
 *   F0 7D 48 53 01 00 05 F7                       OFFER, from the keyboard
 *
 * Neither is answered, and the keyboard answers neither: an OFFER sent to
 * it is UNKNOWN.
 *
 * The keyboard draws from this model on its own control task (screen.c);
 * nothing here touches the screen or Flash.
 *
 * The surface: while the Pi has the screen, the panel speaks to it instead
 * of Open's hidden page, in plain MIDI to the port that sent the CLEAR:
 *
 *   pads 1-16       note 36-51 on channel 10, its velocity; poly aftertouch
 *   encoders 1-7    CC 21-27 on channel 16, the knob's value, 0-127
 *   faders 1-4      CC 28-31 on channel 16; 32-35 with FADER BANK lit
 *   buttons         note <button id> on channel 16: 127 pressed, off let go
 *
 * Pads are what Hindsight's SMK-37 profile binds already (Play mode's pads,
 * CATCH, GRAB), and CC 21-24 its tape mods. An encoder moves its knob's
 * value by its detents, clamped; KNOB sets the value, so the Pi can put it
 * where its own is and a turn never jumps. Button ids are the panel's
 * (smk37_controls.c): 0 SHIFT, 3 SAVE, 4 OCT-, 5 OCT+, 6 ARP, 7 PAD/STEP,
 * 8 SCL, 9 ENV, 10 GLO, 11 BLUETOOTH, 13 EDIT, 14 FX, 15 SEQ, 16 PLAY,
 * 17 REC. HOME (2, 12) gives the screen back and FADER BANK (1) switches
 * the faders; neither is sent. The keys, the wheels, the pedal and the
 * master knob play the keyboard's synth as always. A pad or button belongs
 * to whoever had its press: its release goes there too, so nothing sticks
 * when the screen changes hands. */
#ifndef HINDSIGHT_H
#define HINDSIGHT_H
#include <stddef.h>
#include <stdint.h>

enum { HS_VERSION = 1, HS_ITEMS = 16, HS_TEXT = 40, HS_COLOURS = 7, HS_KNOBS = 7 };
enum { HS_HELLO = 0, HS_CLEAR = 1, HS_TEXT_AT = 2, HS_RELEASE = 3, HS_KNOB = 4, HS_OFFER = 5, HS_ACK = 0x7f };
enum { HS_PAD_CHANNEL = 9, HS_PAD_NOTE = 36, HS_SURFACE_CHANNEL = 15, HS_KNOB_CC = 21, HS_FADER_CC = 28 };
enum { HS_OK = 0, HS_UNKNOWN = 1, HS_MALFORMED = 2, HS_FULL = 3, HS_RELEASED = 4 };

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
/* Control task, from smk37_control_event: 1 when the surface took the
 * panel's event (kind is smk37_controls.h's), 0 when it is Open's. */
int hindsight_surface(unsigned kind, unsigned id, int value, unsigned fader_bank);

/* The model screen.c draws. The revision changes whenever it does. */
unsigned hindsight_active(void);
/* HOME: give the screen back, and tell the Pi. */
void hindsight_release(void);
/* HOME held: offer the screen to the Pi, if it hasn't got it. */
void hindsight_offer(void);
unsigned hindsight_revision(void);
unsigned hindsight_background(void);
const struct hs_item *hindsight_item(unsigned index);

#endif
