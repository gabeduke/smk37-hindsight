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

CI builds the Elite firmware twice: on `windows-latest`, the platform
M-VAVE supports, and under Wine on Linux. It fails unless the two packages
are the same bytes. That match is what shows the Mac build is right.

## Tests

```sh
tests/run.sh
```

It checks four things:

- the areas the app writes, by compiling `instrument.h`'s asserts and
  printing the areas;
- that no other file writes Flash;
- `flash-diff.py`;
- that no dump is in the tree.

## Room for code

The firmware image and the user partition share the flash below 0xFF000,
and V01 leaves 29 bytes free in the image. Our own SysEx handler, font and
drawing need space, which comes from one of two places:

- something Open has that Hindsight doesn't need: the sampler's code, an
  engine, or the factory DX7 voices;
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
exists for m-bootloader's calibration copy (`0xFF000-0xFF23C`), if Gabe
accepts that write.
