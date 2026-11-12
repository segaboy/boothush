# Macintosh 128K keyboard

BootHush models the M0110 GS536 keyboard with no external keypad. The guest
issues commands through the VIA shift register in external-clock mode.
Shift-out and shift-in completion each set the VIA's shift interrupt flag.
The model schedules byte completion on guest cycles. It omits individual
clock and data edges, keyboard firmware, electrical timing, and keypad traffic.

| Command | Value | Reply |
| --- | --- | --- |
| Inquiry | `0x10` | Oldest queued transition or `0x7b` after a quarter second |
| Instant | `0x14` | Oldest queued transition or `0x7b` without the Inquiry delay |
| Model Number | `0x16` | `0x03` |
| Test | `0x36` | `0x7d` |

Guest scan positions range from `0x00` to `0x39`. The device sends a position in
bits 1–6, sets bit 0, and sets bit 7 for releases. The queue holds 64
transitions in order and refuses further input until it has space. The Model
Number command preserves scheduled test transitions.

The protocol and wire timing come from Apple, *Inside Macintosh*, Volume III
(1985), pp. III-29–III-31 and III-41–III-42. [Apple documentation](https://puyoman.net/documentation/Inside_Macintosh.pdf)

External-clock mode and shift interrupt behavior follow Synertek, *SY6522 /
SY6522A Versatile Interface Adapter Preliminary Overview* (April 1979), pp.
17–18. [Synertek documentation](https://retronik.silicium.org/DOCUMENTS/Composants/SYNERTEK/Synertek%20SY6522%20VIA%20Preliminary%20Overview%28Synertek%201979.04%29.pdf)

The model identification byte comes from recorded M0110 device observations.
[TMK device notes](https://github.com/tmk/tmk_keyboard/wiki/Apple-M0110-Keyboard-Protocol#model-number)
