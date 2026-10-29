# Probes

Send the Pi's protocol to the unit by hand, one frame at a time, and print
every answer. They found, on 2026-10-29, that over the Pi's USB MIDI the
keyboard loses a BATCH of 764 bytes or more (624 is acked in 2 ms), where a
Mac got 953 through.

Each script reads a file of lines: hex bytes for a frame, `wait <seconds>`,
or `# a note`, printed as it is reached.

- `piprobe.py`, on the Pi, with Hindsight stopped so the port is free
  (`systemctl --user stop hindsight.service`):
  `python3 -I piprobe.py /dev/snd/midiC<card>D0 steps.txt`
- `hsprobe.swift`, on a Mac: `swiftc -O -o build/hsprobe tools/probe/hsprobe.swift`,
  then `build/hsprobe "smk37 open" tools/probe/steps.txt`. CoreMIDI paces
  SysEx, about 250 ms for a whole screen, so time the link from the Pi.

`steps.txt` is HELLO, CLEAR, each version 2 command alone, a small BATCH
and a 953-byte one; `sweep.txt` is BATCHes from 64 to 764 bytes.
