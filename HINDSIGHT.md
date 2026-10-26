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

CI builds the Elite firmware twice: on `windows-latest` with Windows
PowerShell, as `build.bat` does, and under Wine on Linux. It fails unless the two packages
are the same bytes. That match is what shows the Mac build is right.

## The Pi's protocol

The Pi drives the keyboard: it draws text on the screen, and while it has
the screen the panel speaks to it. `app/hindsight.h` has the full
definition.

```
F0 7D 48 53 01 <seq> <cmd> <payload> F7       48 53 is "HS"; 01 the version
00 HELLO                                       is this our firmware?
01 CLEAR   <colour>                            take the screen, fill it
02 TEXT    <x lo> <x hi> <y lo> <y hi> <scale> <colour> <ASCII...>
03 RELEASE                                     give the screen back
04 KNOB    <knob 0-6> <value 0-127>            where an encoder stands
ack: F0 7D 48 53 01 <seq> 7F <status> F7      HELLO adds <version> <items>
     status 0 ok, 1 unknown, 2 malformed, 3 full, 4 released
the keyboard's own: F0 7D 48 53 01 00 03 F7   HOME gave the screen back
```

- **Only CLEAR takes the screen.** TEXT while the screen is the keyboard's
  is answered *released* and changes nothing. So a Pi still updating a
  counter can't undo a HOME.
- **TEXT works by place.** TEXT where there's already text replaces it,
  and TEXT with no text removes it. At least one glyph has to fit.
- **Limits:** up to 16 items of 40 characters, in seven colours (black,
  panel, text, dim, accent, green and amber). Lowercase is drawn as
  capitals, which is the font Open has.
- **HOME** gives the screen back, and the keyboard sends its own RELEASE
  to the port that sent the CLEAR. Pressed alone, HOME does nothing else,
  so the page is the one that was left. With another button held it does
  its usual job too: PLAY+HOME still stops everything.
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
- The pad and button LEDs keep showing Open's state.
- Any change repaints the whole screen. That is bounded at 4 ms of each
  poll, so it's safe, but a fast counter keeps the LCD busy. Repainting
  only the changed text is the next step.

## Seeing the screen without a keyboard

```sh
tools/screen/run.sh                         # tools/screen/pass1.txt
tools/screen/run.sh tools/screen/surface.txt
```

It compiles this `app/` code on Open's simulator (`web/engine`) with the
host's C compiler and plays a script. A script sends SysEx, waits, presses
pads, keys, encoders, faders and buttons, expects what the keyboard sends,
and takes shots, which land in `build/screen/<script>/`. CI plays both
scripts and uploads the pictures. It fails unless:
- every expect held;
- the Pi drew;
- HOME put back exactly the page that was left;
- a TEXT after HOME changed nothing.

## Tests

```sh
tests/run.sh
```

It checks five things:

- the areas the app writes, by compiling `instrument.h`'s asserts and
  printing the areas;
- that no other file writes Flash;
- the Pi's protocol: every command, the acks, malformed frames, a full
  list, a busy port, and the surface (each control, ownership across HOME,
  the clamps);
- `flash-diff.py`;
- that no dump is in the tree.

## Room for code

The firmware image and the user partition share the flash below 0xFF000.
V01 left 29 bytes free in the image. Replacing the workbench's transfer
protocol with ours took the Elite's image from 453,892 bytes to 450,500,
which leaves 3,421 bytes. More room comes from one of two places:

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
