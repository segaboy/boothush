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

The tests generate an original 68000 ROM and floppy payloads. The ROM reads a
sector through the IWM, decodes its data and tags, verifies the rolling GCR
checksum, and executes the loaded sector in guest RAM. The raw fixture seeks
from track 0 to track 64 first. The Disk Copy fixture verifies nonzero tags.
ClasSICK's replacement System and original Apple ROM/System interoperability
still require their own boot images and trials.

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
