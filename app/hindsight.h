/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Added 2026-10-26 for Hindsight: the Pi's SysEx protocol, version 2.
 *
 *   F0 7D 48 53 <v> <seq> <cmd> <payload> F7     48 53 is "HS"; <v> 01 or 02
 *
 *   00 HELLO                                       is this our firmware?
 *   01 CLEAR   <colour>                            take the screen, fill it
 *   02 TEXT    <x lo> <x hi> <y lo> <y hi> <scale> <colour> [<back>] <ASCII...>
 *              puts text at (x, y), replacing what was there; no text removes it
 *   03 RELEASE                                     give the screen back
 *   04 KNOB    <knob 0-6> <value 0-127>            where an encoder stands
 *   version 2 only:
 *   06 BOX     <x lo> <x hi> <y lo> <y hi> <w lo> <w hi> <h lo> <h hi> <colour>
 *              fills w by h at (x, y), replacing the box there; w or h 0 removes it
 *   07 BAR     <x lo> <x hi> <y lo> <y hi> <w lo> <w hi> <h lo> <h hi> <value 0-127>
 *              <colour> <back>: the left (w * value + 63) / 127 columns in colour,
 *              the rest in back, replacing the bar there; w or h 0 removes it
 *   08 LED     <pad 0-15> <red> <green> <blue>     a pad's light, each 0-127
 *   08 LED     <16 + button id> <0 off, 1 on, 2 blinking>   a button's light
 *   09 BATCH   { <cmd> <n> <n bytes of its payload> } ...
 *              any of 01-08, in order, as if each were sent alone, one ack
 *
 * Coordinates and sizes are pixels, x = lo | hi << 7. Scale is 1 to 4 (a
 * glyph is 6 by 7 pixels at 1); at least one glyph has to fit, and a box or
 * a bar has to fit whole. Colours index the keyboard's palette
 * (HS_COLOURS): 0 black, 1 panel, 2 text, 3 dim, 4 accent, 5 green,
 * 6 amber, then in version 2 7-10 the tracks T1-T4, 11 red, 12 orange,
 * 13 violet, 14 white and 15 deep. TEXT's <back> is version 2's: the colour
 * behind its glyphs. In version 1 there is none, and it is CLEAR's colour.
 *
 * The screen is a list of items, drawn in the order they were first put
 * there: a later one covers an earlier one. An item is found by its kind
 * and its place: TEXT replaces the TEXT at (x, y), BOX the BOX and BAR the
 * BAR, and a replaced item keeps its place in the order. CLEAR empties the
 * list. Only CLEAR takes the screen: anything else that draws or lights
 * while the screen is the keyboard's answers RELEASED and changes nothing.
 *
 * LED sets a light while the Pi has the screen; a light it hasn't set stays
 * the keyboard's, and when the screen goes back so do they all. A pad's
 * 0-127 is off to full, and full is Open's brightest: the LEDs get
 * (level * 48 + 63) / 127, as all sixteen at the LEDs' own 127 would draw
 * about 2.6 times the current Open's pads do. A
 * blinking button is on for 250 ms and off for 250 ms. The buttons are the
 * surface's ids, below, but for HOME (2, 12) and FADER BANK (1), which stay
 * the keyboard's.
 *
 * Version 1 frames are read as they always were: TEXT has no <back>, the
 * palette is the first seven colours, and BOX, BAR, LED and BATCH are
 * UNKNOWN. A version 2 Pi sends version 2 frames. Every message is answered
 * on the port it came from with its own version byte, and acks are never
 * answered:
 *
 *   F0 7D 48 53 <v> <seq> 7F <status> F7          HELLO adds <version> <items>,
 *                                                  02 60: version 2, 96 items
 *                                                  BATCH adds <done lo> <done hi>
 *
 * A BATCH stops at its first command that isn't OK, and answers that
 * command's status and how many came before it (done); those stay done. A
 * command whose <n> runs past the frame is MALFORMED; a HELLO or a BATCH
 * inside a BATCH is UNKNOWN. A frame is up to 1,024 bytes, F0 to F7, so a whole screen fits
 * one BATCH.
 *
 * The seq comes back, so the Pi can time a message to its ack. Send one
 * message at a time and wait for its ack: the mailbox holds one frame, and
 * a second frame arriving meanwhile is lost. Resend on a timeout; every
 * command can be repeated safely. HOME on the keyboard gives the screen
 * back, and the keyboard says so to the port that sent the CLEAR. HOME held
 * for a second on the keyboard's own screen, and let go with nothing else
 * pressed or turned meanwhile, offers it to that port (USB, before any
 * CLEAR), which takes it with a CLEAR if it wants it:
 *
 *   F0 7D 48 53 01 00 03 F7                       RELEASE, from the keyboard
 *   F0 7D 48 53 01 00 05 F7                       OFFER, from the keyboard
 *
 * The keyboard's own say 01, which a Pi of either version reads. The Pi
 * answers neither. Sent to the keyboard, a RELEASE gives the screen back
 * like any other, and an OFFER is answered UNKNOWN.
 *
 * The keyboard draws from this model on its own control task (screen.c),
 * and lights the panel from it (app/board/panel.c); nothing here touches
 * the screen, the lights or Flash. A change repaints only the 16-row bands
 * it covers, between the columns it covers: an item the place it took and
 * the place it takes, a CLEAR the whole screen. An item sent again as it
 * is repaints nothing.
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

enum { HS_VERSION = 2, HS_ITEMS = 96, HS_TEXT = 40, HS_COLOURS = 16, HS_KNOBS = 7, HS_FRAME = 1024 };
enum { HS_HELLO = 0, HS_CLEAR = 1, HS_TEXT_AT = 2, HS_RELEASE = 3, HS_KNOB = 4, HS_OFFER = 5,
       HS_BOX = 6, HS_BAR = 7, HS_LED = 8, HS_BATCH = 9, HS_ACK = 0x7f };
/* Version 1's palette was the first seven colours. */
enum { HS_V1_COLOURS = 7, HS_PADS = 16, HS_LED_BUTTON = 16, HS_BUTTONS = 18 };
/* A pad's full level on the LEDs: Open's brightest (smk37_pad_color). */
enum { HS_PAD_FULL = 48 };
enum { HS_BANDS = 15, HS_BAND_ROWS = 16 };
enum { HS_PAD_CHANNEL = 9, HS_PAD_NOTE = 36, HS_SURFACE_CHANNEL = 15, HS_KNOB_CC = 21, HS_FADER_CC = 28 };
enum { HS_OK = 0, HS_UNKNOWN = 1, HS_MALFORMED = 2, HS_FULL = 3, HS_RELEASED = 4 };

/* An item: kind is HS_TEXT_AT, HS_BOX or HS_BAR. (w, h) is what it covers:
 * a TEXT's glyphs that fit. A BOX is a BAR at value 127. */
struct hs_item {
    uint8_t kind, x, y, w, h, scale, colour, back, value;
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
/* M-Flasher's soft key, F0 22 24 35 7D F7, answered as M-VAVE's stock
 * firmware answers it: restart into the ROM's USB download mode
 * (4C4A:8057), where M-Flasher reads and writes the flash. The board's
 * (app/board/panel.c); on the unit it never returns. */
void hindsight_download_mode(void);
/* How many presses and turns the panel has had, HOME's own aside: keys,
 * pads, buttons and encoders (the master knob too). A held HOME offers the
 * screen only if this didn't change while it was held. */
unsigned hindsight_touches(void);
unsigned hindsight_revision(void);
/* Control task, screen.c: where band b (rows 16b to 16b + 15) changed since
 * it was last asked, as left | right << 8, columns [left, right), or 0 if it
 * didn't; asking forgets it. */
unsigned hindsight_damage(unsigned band);
unsigned hindsight_background(void);
/* The panel's lights (app/board/panel.c): a pad's RGB, 0xRRGGBB, or open
 * (the keyboard's own) if the Pi hasn't set it; a button's light now,
 * 0 or 1, or -1 if the Pi hasn't set it. */
uint32_t hindsight_pad(unsigned pad, uint32_t open);
int hindsight_button(unsigned id);
const struct hs_item *hindsight_item(unsigned index);

#endif
