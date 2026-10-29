# SMK37 OPEN, for Hindsight

This fork of [m-vave/smk37_open](https://github.com/m-vave/smk37_open)
turns the SMK-37 Elite into Hindsight's real-time control surface: the Pi
talks to it over USB MIDI SysEx. The plan is step 7 of
`docs/plan/the-instrument.md` in
[gabeduke/hindsight](https://github.com/gabeduke/hindsight). M-VAVE's
`README.md`, licence and `THIRD_PARTY.md` stand as they are; this file says
what the fork adds.

## The rules

From Gabe, 2026-10-26:

- Never write the unit's own regions: the Bluetooth pairing at 0xC0000,
  the factory calibration at 0xC1000, and the chip key and MAC at 0xFF000.
- Back up the whole 1 MiB flash twice, and check the dumps match, before
  anything is flashed. Dumps never go in a repo: `tests/run.sh` fails on a
  1 MiB file.
- Install m-bootloader before any build of ours, so there is a way back.
- Flash nothing without Gabe at the keyboard, confirming each step.

And from what the research found:

- **Every write is by M-Flasher.** Never use the workbench or M-UPGRADE.
  M-VAVE's update loader, which they use, erased 0xC0000–0xC1FFF on a
  Skiff unit, and it puts the stock bootloader back over m-bootloader.

## What differs from upstream

- `app/unit_regions.h`. The SDK's user partition starts at 0x7C000
  (`sdk/Package.cs`), so Open's sample bank lay over the Bluetooth pairing
  and the factory calibration. New sample transfers now stop at 0xC0000:
  about 200 KiB, or 9 s, instead of 19.5 s. Each area the app writes (the
  projects, the samples, the DX7 banks and the tuning sector) asserts at
  compile time that it keeps clear of `[0xC0000, 0xC2000)` and
  `[0xFF000, …)`. `tests/run.sh` fails on a Flash write in any other file.

  M-VAVE's workbench learns the limit from the device's transfer
  protocol, which is gone (see `app/hindsight.c` below). So the stock
  workbench can't connect to this firmware, which suits the rule below.
  Banks written by M-VAVE's firmware are still measured against its limit
  (`GROOVE_SAMPLE_REACH`).

  The checks are per file: `tests/run.sh` allows Flash writes in
  `samples.c`, `project.c` and `fm_library.c`, whose areas are asserted.
  A new write in one of those files has to be checked by its author.

  The check is at compile time, not a runtime guard, for lack of room. V01's
  Elite image is 453,892 bytes, and `Package.cs` allows 453,921, so a
  96-byte runtime guard didn't fit. Anything Hindsight adds has to make
  room first (see *Room for code* below).

  The SDK's own writes are beyond these checks. The SDK answers the
  workbench's update messages before the app sees any SysEx
  (`fm1_app_sysex` in `sdk/include/fm1.h`), and an update runs M-VAVE's
  loader. That loader is the one that erased 0xC0000–0xC1FFF on a Skiff
  unit. Only the rule above keeps it away: never point the workbench at the
  unit.
- `sdk/Build.ps1`. Off Windows, it runs the toolchain through Wine, and its
  path checks use the platform's separator. Its hash in `sdk/manifest.json`
  is updated to match.
- `tools/container/`: the Linux build, which is also the build a Mac runs.
- `tools/flash-diff.py`, for after every write.
- `app/hindsight.c` and `app/hindsight.h`: the Pi's SysEx protocol (below).
  `transfer.c` hands it our frames, in place of the workbench's project
  transfer protocol. MMC transport and DX7 voice dumps work as before.
  `screen.c` draws its model, instead of a page, while the Pi has the
  screen.
- `tools/midi.swift`: a MIDI monitor and sender for the Mac, on CoreMIDI
  (`swiftc -O -o build/midi tools/midi.swift`). It covers `build/midi
  list`, `listen 10 SMK` and `send SMK F0 … F7`. `hs SMK hello` sends
  Hindsight's messages and times their acks.
- `tools/screen/`: the keyboard's screen rendered on the host, from Open's
  simulator and this `app/` (below).
- `tests/`: the host tests.
- `.github/workflows/build.yml`: CI.

## Building, on a Mac or on Linux

You need Docker; on Apple silicon the container runs as linux/amd64 under
Rosetta.

```sh
tools/container/build.sh elite
```

The first run does three things, in order:

1. It downloads JieLi's 2.5.3 installer (53 MB) to
   `~/.cache/smk37-hindsight` and checks its SHA-256.
2. It builds the image.
3. It unpacks the installer with innoextract, in the container. The
   installer itself is never run.

`Build.ps1` then checks the 22 toolchain files against `sdk/manifest.json`.
The package lands in `build/elite/generated/SMK37_ELITE_OPEN.fwsc`.

Two more modes build Open's workbench, with WASI SDK 27 (fetched and checked
the same way):

- `tools/container/build.sh elite web` builds the simulator alone, in about
  40 s, with no JieLi toolchain: this `app/` running in a browser on a whole
  SMK-37 panel, at `build/preview/index.html`. Serve that directory over
  HTTP to play it.
- `tools/container/build.sh elite package` builds the firmware and the
  workbench with it inside: the installer page, at
  `build/official-web/elite/SMK37-Open-Source.html`. The page links this
  commit's archive as its source, so build it from a pushed, clean tree.

CI builds the Elite firmware twice: on `windows-latest` with Windows
PowerShell, as `build.bat` does, and under Wine on Linux. It fails unless the two packages
are the same bytes. That match is what shows the Mac build is right.

## The Pi's protocol

The Pi drives the keyboard: it draws on the screen and sets the panel's
lights, and while it has the screen the panel speaks to it. This is
version 2; `app/hindsight.h` has the full definition.

```
F0 7D 48 53 <v> <seq> <cmd> <payload> F7      48 53 is "HS"; <v> 01 or 02
00 HELLO                                       is this our firmware?
01 CLEAR   <colour>                            take the screen, fill it
02 TEXT    <x lo> <x hi> <y lo> <y hi> <scale> <colour> [<back>] <ASCII...>
                                               <back> in version 2 only
03 RELEASE                                     give the screen back
04 KNOB    <knob 0-6> <value 0-127>            where an encoder stands
           [<x lo> <x hi> <y lo> <y hi> <bar x lo> <bar x hi> <bar y lo> <bar y hi>]
                                               and where its value is drawn
version 2 only:
06 BOX     <x lo> <x hi> <y lo> <y hi> <w lo> <w hi> <h lo> <h hi> <colour>
07 BAR     <x lo> <x hi> <y lo> <y hi> <w lo> <w hi> <h lo> <h hi>
           <value 0-127> <colour> <back>
08 LED     <pad 0-15> <red> <green> <blue>     each 0-127
08 LED     <16 + button id> <state>            0 off, 1 on, 2 blinking
09 BATCH   <cmd> <n> <n bytes> <cmd> <n> ...   01-08, as if each sent alone
ack: F0 7D 48 53 <v> <seq> 7F <status> F7     in the version it was sent in
     HELLO adds 02 60 07 (version 2, 96 items, the features below);
     BATCH adds <done lo> <done hi>
     status 0 ok, 1 unknown, 2 malformed, 3 full, 4 released
the keyboard's own: F0 7D 48 53 01 00 03 F7   RELEASE: HOME gave the screen back
                    F0 7D 48 53 01 00 05 F7   OFFER: HOME held, the Pi may take it
the Pi answers neither; sent to the keyboard, OFFER is answered unknown
```

- **Version 1 still works as it did.** A version 1 frame is read as
  before: TEXT has no background (it is CLEAR's colour), the palette is
  the first seven colours, and BOX, BAR, LED and BATCH are answered
  unknown. Two things a version 1 Pi can't tell apart from before: the
  list has room for 96 items, not 16, and HELLO's ack says 02 60, which it
  doesn't read. The keyboard's own RELEASE and OFFER still say 01.
- **Only CLEAR takes the screen.** Anything that draws or lights while
  the screen is the keyboard's is answered *released* and changes
  nothing. So a Pi still updating a counter can't undo a HOME.
- **The screen is a list of items**, drawn in the order they were first
  sent: a later one covers an earlier one. An item is found by its kind
  and its place: TEXT replaces the TEXT at (x, y), BOX the BOX there and
  BAR the BAR, and a replaced item keeps its place in the order. TEXT with
  no text removes it, and a BOX or a BAR with a width or a height of 0.
  At least one glyph has to fit; a box or a bar has to fit whole. CLEAR
  empties the list.
- **BAR** is a meter or a fader's place: the left (w × value + 63) / 127
  columns in its colour, the rest in its back.
- **A change repaints only its bands:** the 16-row bands it covers,
  between the columns it covers. An item repaints where it was and where
  it is; one sent again as it is repaints nothing; a CLEAR repaints the
  whole screen. A counter at scale 4 is two or three bands of the
  fifteen, so it no longer keeps the LCD busy.
- **BATCH** is a whole screen, or a whole change, in one frame and one
  ack. It applies its commands in turn and stops at the first that isn't
  OK, answering that one's status and how many were done before it,
  which stay done. A command whose length runs past the frame is
  malformed; a HELLO or a BATCH inside one is unknown. A frame is up to
  1,024 bytes, F0 to F7, which holds the design's busiest screen (84
  items) in one. Send what goes before what comes, so the list has room.
- **LED** lights a pad (its RGB, each 0–127 from off to full) or a button (on, off, or blinking 250 ms on and
  250 ms off), by the surface's button ids. HOME and FADER BANK stay the
  keyboard's. A pad's full is Open's brightest: the LEDs get
  (level × 48 + 63) / 127, because all sixteen pads at the LEDs' own 127
  would draw about 2.6 times the current of Open's, enough to risk a
  brown-out. A light the Pi hasn't set stays the keyboard's, a CLEAR
  keeps them, and when the screen goes back they all do.
- **Limits:** up to 96 items, a TEXT of 40 characters, in sixteen colours:
  0 black, 1 panel, 2 text, 3 dim, 4 accent, 5 green, 6 amber, 7–10 the
  tracks T1–T4, 11 red, 12 orange, 13 violet, 14 white, 15 deep. Lowercase
  is drawn as capitals, which is the font Open has.
- **HOME** gives the screen back, and the keyboard sends its own RELEASE
  to the port that sent the CLEAR. Pressed alone, HOME does nothing else,
  so the page is the one that was left. With a button held that Open still
  has, one pressed before the Pi took the screen, it does its usual job
  too: PLAY+HOME then stops everything.
- **HOME held for a second** on the keyboard's own screen sends OFFER as
  it is let go, to the port that last sent a CLEAR (USB, before any). The
  Pi takes the screen with a CLEAR if it wants it.
  - It offers nothing when HOME is pressed with SHIFT or another button,
    or when anything is pressed or turned while it is held: a key, a pad,
    a button, an encoder or the master knob. HOME stays Open's modifier,
    as it always was, however long it is held (HOME+PRESETS, HOME+SEQ).
  - The press still does what HOME does on Open's page (PRESETS, or back),
    so the screen goes back to that page later.
- **Local echo** (HELLO's features: 1 pads, 2 knobs, 4 the quiet clock), so
  a touch answers on the keyboard before the Pi can:
  - a pad pressed shows at once at three times the light the Pi set it,
    each colour at most full: its rest (0.18 of its colour) becomes its
    half (0.55). It lasts until the Pi sets that pad again or 300 ms
    pass (`HS_ECHO_MS`), so the Pi confirms it, or puts it back, by
    setting the pad after every press;
  - KNOB with ten bytes also says where the knob's value is drawn: the
    TEXT at (x, y) and the BAR at (bar x, bar y). A turn then writes the
    value into both, as decimal digits and the bar's value, in their own
    style, and the Pi draws the cell again after. Send the places only
    when the cell shows the knob's value as its number (version 2 only); a
    KNOB of two bytes withdraws them, and a CLEAR or giving the screen
    back forgets them all.
- **The quiet clock:** while the Pi has the screen, the port it took it
  from gets none of Open's own clock or transport (F8, FA, FB, FC, F2);
  what Thru routes there from another input still goes. The clock's
  tempo goes too, so a Pi following the keyboard's clock
  (`MIDI_CLOCK_DEVICE`) loses it while it has the screen. A
  busy link made `midi_router.c`'s recovery send MIDI Stop at every screen
  take, which stopped the Pi's tape.
- **The pads' chain** shares SPI1 with the LCD, and a pad's light is sent
  only when it changes, so one sent wrong stayed wrong. While the Pi has
  the pads, the whole chain is sent again every 500 ms
  (`app/board/panel.c`).
- **Acks:**
  - every message is acked on its port with its seq, so the Pi can time
    Pi → screen;
  - acks are never answered, so an echo can't loop;
  - send one message at a time and wait for its ack: the mailbox holds
    one frame, and a second arriving meanwhile is lost. Resend on a
    timeout; every command can be repeated safely.
- **The manufacturer ID:** 0x7D is MIDI's non-commercial ID, fine for our
  own kit but not for a product.

### The surface

While the Pi has the screen, the panel sends it plain MIDI, on the port
that sent the CLEAR, instead of acting on Open's hidden page:

| Control | Sends |
|---|---|
| Pads 1–16 | notes 36–51 on channel 10 with their velocity, and poly aftertouch while held |
| Encoders 1–7 | CC 21–27 on channel 16: the knob's value, 0–127 |
| Faders 1–4 | CC 28–31 on channel 16; 32–35 with FADER BANK lit |
| Buttons | note *id* on channel 16: 127 when pressed, note off when let go |

- **Pads** are what Hindsight's SMK-37 profile already binds: Play mode's
  pads, then CATCH and GRAB. CC 21–24 are its tape mods.
- **Encoders send absolute values.** An encoder moves its knob's value by
  its detents, clamped at 0 and 127. KNOB sets that value, so the Pi can
  put it where its own is and a turn never jumps. The plan said relative:
  absolute is what Hindsight's knob bindings take.
- **Button ids** are the panel's (`smk37_controls.c`): 0 SHIFT, 3 SAVE,
  4 OCT−, 5 OCT+, 6 ARP, 7 PAD/STEP, 8 SCL, 9 ENV, 10 GLO, 11 BLUETOOTH,
  13 EDIT, 14 FX, 15 SEQ, 16 PLAY, 17 REC. HOME (2 and 12) and FADER BANK
  (1) stay the keyboard's.
- **What stays the synth's:** the keys, the wheels, the pedal and the
  master knob play the keyboard's synth as always.
- **Who gets a release:** a pad or button belongs to whoever had its
  press, and its release goes there too. A pad held through HOME is let go
  to the Pi, so nothing sticks.
- **Full queue:** a full MIDI queue drops a message, as Open's own MIDI
  out does.

What isn't done yet:
- The Pi's layout (step 7's build order, 4 and 5 in Hindsight): this
  firmware draws and lights what it is sent, and nothing sends version 2
  yet.
- The lights on a unit. `app/board/panel.c` puts the Pi's lights on the
  panel, and the simulator doesn't run the board's code: the host tests
  and `light` check what the Pi set, not the LEDs themselves.

## Seeing the screen without a keyboard

```sh
tools/screen/run.sh                         # tools/screen/pass1.txt
tools/screen/run.sh tools/screen/surface.txt
tools/screen/run.sh tools/screen/repaint.txt
tools/screen/run.sh tools/screen/v2.txt
tools/screen/run.sh tools/screen/prototype.txt
```

`prototype.txt` is generated from the layout of the prototype the surface
was designed on (step 7 of `docs/plan/the-instrument.md` in hindsight,
*The surface, designed*): its six screens, each drawn whole and then as
only what changed from the one before. The pixels it checks are the
prototype's own drawing of each; all eleven shots matched it, pixel for
pixel, when it was made.

It compiles this `app/` code on Open's simulator (`web/engine`) with the
host's C compiler and plays a script. A script sends SysEx, waits, presses
pads, keys, encoders, faders and buttons, expects what the keyboard sends
(or that it sent nothing of the kind), and takes shots, which land in `build/screen/<script>/`. CI plays both
scripts and uploads the pictures. It fails unless:
- every expect held;
- the Pi drew;
- HOME put back exactly the page that was left;
- a TEXT after HOME changed nothing;
- HOME held a second and let go offered the screen, once and only on the
  release; HOME let go sooner, with SHIFT, with PLAY, a key or an encoder
  meanwhile, or pressed while the Pi had the screen offered nothing;
- a change wrote only the LCD bands it covers (`bands` in a script), and
  the screen it left is the same picture as one drawn from a CLEAR;
- version 2 drew every colour, box, bar and background where it should
  (`pixel`), set and gave back the lights (`light`), and acked every
  command OK (`strict`);
- the design's screens drawn as changes are the same pictures as drawn
  whole.

## Tests

```sh
tests/run.sh
```

It checks five things:

- the areas the app writes, by compiling `instrument.h`'s asserts and
  printing the areas;
- that no other file writes Flash;
- the Pi's protocol, both versions: every command, the acks, malformed
  frames, a full list, BATCH's stops and counts, the lights and their
  blinking, a busy port, what each change repaints, the surface (each
  control, ownership across HOME, the clamps) and OFFER;
- `flash-diff.py`;
- that no dump is in the tree.

## Room for code

The firmware image and the user partition share the flash below 0xFF000.
V01 left 29 bytes free in the image. Replacing the workbench's transfer
protocol with ours took the Elite's image from 453,892 bytes to 450,500,
which leaves 3,421 bytes. The surface, the link and the soft key took it
to 451,140 (2,781 free), partial repaint to 451,364, and protocol
version 2 to 452,228: 1,693 bytes free. Version 2 cost 864 bytes and the
two together 1,088, against the plan's estimate of about 900. More room
comes from one of two places:

- something Open has that Hindsight doesn't need. The plan's cost table
  lists it (step 7 of `docs/plan/the-instrument.md` in hindsight): mostly
  Open's sound-editing pages and projects;
- a smaller user partition. `Package.cs` fixes it at 536,576 bytes and the
  SDK validates it, so whether the SDK takes another size is unknown until
  a unit says so.

## After every write

Back up again and compare with the first dump:

```sh
tools/flash-diff.py first.bin after.bin --expect 0x4000-0x7C000
```

It fails if anything changed in the unit's own regions, and, given
`--expect`, if anything changed outside the ranges that write was meant to
change. `--accept-unit START-END` allows one exact range inside them. It
exists for m-bootloader's calibration copy, which Gabe accepted on
2026-10-26: after installing m-bootloader, pass
`--accept-unit 0xFF000-0xFF23C`.
