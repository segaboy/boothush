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

## Opt-in keyboard witness

`--keyboard-witness NEW_PATH` writes a bounded guest-keyboard trace without using physical input. Optional `--keyboard-witness-range FIRST:LAST` filters absolute modeled CPU cycles inclusively. It retains at most 8,192 rows, never overwrites an existing file, and reports `events`, trace `dropped`, queue `rejected`, and scheduled/accepted/pending **key** inputs in the JSON `keyboard_witness` object. The input schedule may include non-key events; those are excluded from these key counts. A full keyboard queue rejects the transition and stops the run with an input fault; the witness includes the rejection when in range.

The text header is `BHMACKEY1 FIRST LAST STOP COUNT DROPPED REJECTED`, followed by `cycle pc kind value queue due`. Values are decimal and `pc` is the CPU PC sampled by the model when the event was recorded; it is context, not a guaranteed exact bus-instruction address. `input_due` reports a scheduled transition at its application boundary, with its requested absolute cycle in `due`; `queued` and `rejected` show the encoded transition and queue depth after the operation. `mode` and `shift_write` show VIA shift control activity; `command_complete`, `reply_begin` and `reply_complete` show the modeled transfer; `key_delivered` means a queued transition was removed when its reply completed. `shift_read` is an actual guest read of the shift register, which may be performed by an interrupt handler before another guest read. Other rows have `due` zero. The value of queue depth is sampled at each event. An empty or truncated trace is not evidence that no activity occurred outside the selected range or after the cap.

The model queues transitions, including overlapping holds and releases, but does not establish physical keyboard rollover, per-key electrical behavior or hardware timing. Synthetic integration tests cover overlapping holds/releases, queue capacity, range filtering, repeated traces, and cap reporting. The optional `BOOTHUSH_STB_MAC_PROBE` integration uses the independently generated port controls ROM with SHA-256 `31b2666be66ac7a02d576998cb9b46aec3a91245d16679877d593748fd581fc6`; it observes queued/delivered press and release replies and four VBL-indexed framebuffer snapshots, including an exact repeated run. This is not physical-Mac evidence or Super Tilt Bro game logic.

## Deterministic injected keyboard faults

`--keyboard-fault COMMAND:OCCURRENCE:KIND:ARG` selects one outgoing command byte and its positive, one-based matching occurrence. The one-shot fault is opt-in and may be used with scheduled keyboard and mouse input. The JSON `keyboard_fault` object reports the selection, whether it triggered, and its modeled cycle. A fault that has not triggered by the stop is not evidence of guest recovery. Kind and argument are:

| Kind | Mode | Argument and model behavior |
| --- | --- | --- |
| 1 | Stall outgoing command | `0`; transfer stays stalled until a shift-mode reset/new command. |
| 2 | Suppress reply | `0`; outgoing command completes, but no reply is produced. |
| 3 | Delay reply | Positive modeled-cycle delay added to reply completion. |
| 4 | Malformed reply | Replacement byte `0..255`; the original queued key is not consumed. |
| 5 | Disconnect/reconnect | Positive modeled-cycle disconnection duration; commands begun while disconnected remain stalled after reconnect until the guest resets the shift mode and starts a new command. |

The queue still accepts scheduled transitions subject to its normal capacity. A suppressed or malformed reply does not silently remove the queued key; a delayed reply consumes it only when the reply completes. `--keyboard-witness-limit N` optionally caps retained trace rows to 1..8192; its JSON includes `limit` and `range_reached`. Fault rows include `fault_start`, `reply_suppressed`, `disconnect`, and `reconnect`. Read queue rejections, trace drops, scheduled/accepted/pending input and guest status alongside the trace. These are deliberate model faults, not claims about physical M0110 failures, rollover or electrical timing.

The optional pinned ClasSICK candidate test (ROM SHA-256 `b3a50ad806b589fdc02a4de579e6d72700c9107fb273d9ca73026320bebed807`) selects the first `0x14` command, starting at modeled cycle 56844836 in that setup. Its KBD1 guest diagnostics report one timeout for stall, no-reply, and a 1,500,000-cycle disconnect, and one invalid reply for replacement byte zero. In that exact run, delays of 991993 and 991994 cycles fall on opposite sides of the guest's timeout boundary. These values depend on this guest and model and are not physical measurements. The guest latches the timeout; the synthetic device tests independently check queue preservation and resumed exchanges, so this is not a demonstration of guest recovery after timeout.

## Display witness

`--display-witness NEW_PATH` records modeled VBL and bus-fault events. Add `--display-witness-memory ADDRESS:LENGTH` to record bus writes overlapping a selected physical RAM range, such as the active framebuffer, and `--display-witness-range FIRST:LAST` to filter absolute modeled cycles. Both filters require the witness option. Up to 8,192 events are retained; JSON reports count and dropped rows. The file begins `BHMACDISPLAY1 FIRST LAST STOP COUNT DROPPED RAM_ADDRESS RAM_LENGTH`, then `cycle vbl pc kind address value width`. For a `write`, address is the physical RAM offset, value is the complete original guest write in big-endian significance, and width is 1, 2 or 4 bytes; a write may overlap only part of the selected range. For a `vbl`, value reports how many VBLs were advanced at that model tick. For a `fault`, address is the failing bus address; the run's JSON fault reports the operation. PC is the emulator's current instruction context. These are model-clock observations, not a pixel-latched display or independently verified hardware timing. The witness works without mouse input or mouse tracing.

The protocol and wire timing come from Apple, *Inside Macintosh*, Volume III
(1985), pp. III-29–III-31 and III-41–III-42. [Apple documentation](https://puyoman.net/documentation/Inside_Macintosh.pdf)

External-clock mode and shift interrupt behavior follow Synertek, *SY6522 /
SY6522A Versatile Interface Adapter Preliminary Overview* (April 1979), pp.
17–18. [Synertek documentation](https://retronik.silicium.org/DOCUMENTS/Composants/SYNERTEK/Synertek%20SY6522%20VIA%20Preliminary%20Overview%28Synertek%201979.04%29.pdf)

The model identification byte comes from recorded M0110 device observations.
[TMK device notes](https://github.com/tmk/tmk_keyboard/wiki/Apple-M0110-Keyboard-Protocol#model-number)
