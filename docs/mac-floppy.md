# Macintosh 128K floppy

The internal single-sided Sony drive accepts read-only 400 KiB raw sector
images and 400 KiB Disk Copy 4.2 images with or without the 12-byte sector
tags. Disk Copy headers, lengths and checksums are validated before insertion.
Raw images supply zero tags. Mounting never edits the source file.

The guest controls the drive through IWM soft switches and VIA PA5. The
controller provides drive sensing, track seeking, motor and eject commands,
and a rotating GCR data stream with 80 tracks in five speed zones. The model
delivers already synchronized nibbles at CPU-cycle boundaries and simulates
nominal zone speeds. Magnetic flux, PWM speed control, analog seek and spin-up
delays, IWM bit-level modes and disk writes are outside this profile.

Writable media remains open work. The current insertion path converts the validated source into private encoded GCR tracks and discards the sector-image representation. A complete write path needs bounded write-gate and nibble handling, GCR sector and tag decoding with integrity checks, and explicit raw/DC42 serialization with recalculated checksums. Until these pieces exist there is no modified-media output or guest write persistence. The source image remains unchanged.

The tests generate an original 68000 ROM and floppy payloads. The ROM reads a
sector through the IWM, decodes its data and tags, verifies the rolling GCR
checksum, and executes the loaded sector in guest RAM. The raw fixture seeks
from track 0 to track 64 first. The Disk Copy fixture verifies nonzero tags.
ClasSICK's replacement System and original Apple ROM/System interoperability
still require their own boot images and trials.

## Stopped drive observations

Add `--mac-floppy-state 1` to a bounded `mac128k` run to include a fixed-size
`mac_floppy` JSON object with `observation: "modeled_iwm"`. The snapshot reads
stored state at the stopped CPU cycle, without accessing a guest register,
changing soft switches, consuming a nibble or advancing the guest. The option
works with or without a floppy and can accompany `--mac-cpu-state 1`,
`--inspect`, `--until` or `--until-pc`. It accepts only `1`, once, on Mac runs.
The default result has no `mac_floppy` object.

```powershell
& $exe --machine mac128k --rom $originalRom --floppy $originalDisk `
  --cycles 4000000 --timeout-ms 5000 --mac-floppy-state 1 --mac-cpu-state 1
```

The caller supplies the paths and a cycle budget suitable for that guest.
This command makes an observation; reaching its cycle limit is not a boot pass.

| Fields | Meaning |
| --- | --- |
| `media_present`, `write_protected` | Internal drive media state. All inserted media is protected; both are false after eject or with no disk. |
| `motor_on`, `disk_changed`, `step_outward`, `track` | Internal drive state; track is 0-79. These remain internal-drive fields when the external drive is selected. |
| `external_selected`, `controller_enabled` | Raw external-drive selection and effective enable at stop, including the modeled delayed disable. No external drive is present. |
| `select_line`, `control`, `mode` | VIA PA5 pin level, IWM soft-switch bit field and mode byte. |
| `step_commands` | Internal-drive step commands, including attempts clamped at track 0 or 79. |
| `nibbles_delivered` | Encoded track bytes returned by ready data reads, including sync/gap bytes. This is not a decoded sector count. |
| `disabled_data_reads` | Data-mode reads while the controller is disabled, which return `ff`. These are not delivered media bytes. |
| `not_ready_data_reads` | Enabled data-mode reads returning zero: absent/external drive, motor off, no media, or a second read in the same nibble slot. |
| `ignored_data_writes` | Writes to an odd IWM register with Q6 and Q7 high while enabled. Counted even with no media or external selection; no media is modified. Mode writes while disabled are excluded. |
| `eject_commands` | Internal-drive eject commands, including commands with no disk present. |

The six counters are unsigned 64-bit totals from machine creation; reads of
the snapshot do not reset them. Counters accrue independently of the CLI flag,
which controls only reporting. The snapshot contains no disk bytes and no
event history. It cannot show that the guest decoded or used a sector,
completed a boot, or matched physical hardware behavior. Pair it with guest
RAM/register/frame observations and a guest-specific stop condition.

The original floppy tests compare observed and unobserved runs through a
track-64 sector load: cycles, PC, registers, loaded bytes and framebuffer must
match. They also check tagged DC42, no-media polling, disabled reads,
external selection, ignored writes, eject and CLI refusals. Source media is
checked unchanged. These tests do not use or qualify a historical System Disk.

## Source and license

The GCR table, rolling checksum algorithm, interleave and Disk Copy layout in
`src/iwm.cpp` are adapted from MAME at
[`c2caa8969adbf3d599a655a162a6eac163fa63bc`](https://github.com/mamedev/mame/tree/c2caa8969adbf3d599a655a162a6eac163fa63bc):

- [`src/lib/formats/flopimg.cpp`](https://github.com/mamedev/mame/blob/c2caa8969adbf3d599a655a162a6eac163fa63bc/src/lib/formats/flopimg.cpp)
- [`src/lib/formats/ap_dsk35.cpp`](https://github.com/mamedev/mame/blob/c2caa8969adbf3d599a655a162a6eac163fa63bc/src/lib/formats/ap_dsk35.cpp)

The IWM and drive interface were checked against the same revision's
[`iwm.cpp`](https://github.com/mamedev/mame/blob/c2caa8969adbf3d599a655a162a6eac163fa63bc/src/devices/machine/iwm.cpp),
[`floppy.cpp`](https://github.com/mamedev/mame/blob/c2caa8969adbf3d599a655a162a6eac163fa63bc/src/devices/imagedev/floppy.cpp),
and [`mac128.cpp`](https://github.com/mamedev/mame/blob/c2caa8969adbf3d599a655a162a6eac163fa63bc/src/mame/apple/mac128.cpp).

Copyright Olivier Galibert and R. Belmont for the adapted format code.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
