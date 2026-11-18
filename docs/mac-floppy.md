# Macintosh 128K floppy

The internal single-sided Sony drive accepts 400 KiB raw sector
images and 400 KiB Disk Copy 4.2 images with or without the 12-byte sector
tags. Disk Copy headers, lengths and checksums are validated before insertion.
Raw images supply zero tags. Media is read-only by default. Mounting never
edits the source file; `--floppy-out NEW_PATH` opts into private-copy writes.

The guest controls the drive through IWM soft switches and VIA PA5. The
controller provides drive sensing, track seeking, motor and eject commands,
and a rotating GCR data stream with 80 tracks in five speed zones. The model
delivers already synchronized nibbles at CPU-cycle boundaries and simulates
nominal zone speeds. Magnetic flux, PWM speed control, analog seek and spin-up
delays, IWM bit-level modes and arbitrary write-track/format operations are
outside this profile.

The opt-in writer retains a private source-container copy and accepts complete,
validated GCR data records through the existing soft switches. It updates the
logical sector and encoded readback together, then exports the original format
with recalculated DC42 checksums. This is a bounded synchronized sector-record
model, not physical write-gate or magnetic-flux fidelity. See
[private-copy write qualification](floppy-writes-20261118.md).

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
| `media_present`, `write_protected` | Internal drive media state. Inserted media is protected unless private writes were explicitly enabled; both are false after eject or with no disk. |
| `motor_on`, `disk_changed`, `step_outward`, `track` | Internal drive state; track is 0-79. These remain internal-drive fields when the external drive is selected. |
| `external_selected`, `controller_enabled` | Raw external-drive selection and effective enable at stop, including the modeled delayed disable. No external drive is present. |
| `select_line`, `control`, `mode` | VIA PA5 pin level, IWM soft-switch bit field and mode byte. |
| `step_commands` | Internal-drive step commands, including attempts clamped at track 0 or 79. |
| `nibbles_delivered` | Encoded track bytes returned by ready data reads, including sync/gap bytes. This is not a decoded sector count. |
| `disabled_data_reads` | Data-mode reads while the controller is disabled, which return `ff`. These are not delivered media bytes. |
| `not_ready_data_reads` | Enabled data-mode reads returning zero: absent/external drive, motor off, no media, or a second read in the same nibble slot. |
| `ignored_data_writes` | Rejected writes to an odd IWM register with Q6 and Q7 high while enabled. All such writes are ignored by default; an opt-in writable copy accepts ready strobes. Mode writes while disabled are excluded. |
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

## Synthetic sector checksum failures

`--mac-floppy-bad-checksum TRACK:SECTOR` exercises a guest's checksum-error
path through the IWM data stream. It requires `mac128k` and a valid inserted
floppy, and accepts one selection per run. Both numbers are zero-based.
Track must be 0-79; sectors are 0-11 on tracks 0-15, 0-10 on 16-31, 0-9 on
32-47, 0-8 on 48-63, and 0-7 on 64-79. Invalid options or source images are
rejected before execution. DC42 container checksums must still be valid.

```powershell
& $exe --machine mac128k --rom $originalRom --floppy $originalDisk `
  --cycles 4000000 --timeout-ms 5000 --mac-floppy-bad-checksum 64:7 `
  --mac-floppy-state 1
```

When the selected sector's checksum-A low-six-bit nibble is returned by a
ready read, BootHush toggles bit 0 of its decoded GCR symbol and returns the
corresponding valid GCR byte. Only that checksum byte differs. Sector headers,
data, tags, epilogues and modeled nibble timing remain unchanged. The source
file and private encoded tracks are not modified. The fault persists on subsequent rotations
and guest retries; it does not synthesize a BIOS/ROM error code or a physical
disk defect. The guest must detect and handle the incorrect checksum.

Every run with the option reports `mac_floppy_checksum_fault`, even without
`--mac-floppy-state`:

```json
{"kind":"synthetic_gcr_data_checksum","mode":"persistent","track":64,"sector":7,"altered_nibbles":2}
```

`altered_nibbles` counts actual modified return values, not sectors decoded,
guest retries, completed reads or errors noticed by the guest. Zero means the
selected checksum nibble was never returned before stop; merely passing its
rotational position does not count. The object is absent when injection is
not requested. Use guest state and the stop result to assess error handling.

The internal C API configures the fault only on inserted media. Successful
reconfiguration resets its count; successful media insertion clears the
fault. Failed configuration or insertion preserves the previous media and
fault state. Eject retains the report but no further data is returned until
another insertion. Reading the fault report is side-effect-free.

Original raw and tagged-DC42 fixtures reject the selected sector twice via a
guest retry, while retaining the exact decoded payload and tags. Their
captured GCR buffers differ from normal reads at exactly the selected checksum
byte. Repeated faulty runs match; fresh runs with injection removed load and
execute the sector again with unchanged source hashes. This is recovery by
removing a synthetic fault between runs, not a successful retry while the
persistent fault is active. Additional fixtures cover each zone's final
sector, track 79, nonmatching selections, unused injection and validation
refusals. No historical System Disk is used or qualified by these tests.

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
