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

## Optional port title integration

`tests/test_mac_port_title_integration.py` adds a separate, optional black-box
test of three exact port mouse-title-to-keyboard-play ROM identities: the earlier
`d84d0ab3a1c981c2cbfb1a8ec039c189f3f83500ae90672f19b16ddb976df54e`
and its title-control-label successor
`1a367b8eceb9f032e20b679e9224cedf44cde1fc4615720e25710d23f0bd839e`, and
damage-saturation successor
`7121c504159e128d9ec7349213837c5d7b0dbdf8ead594e687334c63e2926ccd`.
The ROM is generated and retained by the port, not distributed in this
repository. Set `BOOTHUSH_STB_TITLE_ROM` to that exact image and `BOOTHUSH_EXE`
to the executable; otherwise the test skips if the fixture is not supplied.
It checks a scheduled mouse movement and click, scheduled key press/release,
title and gameplay state at three VBL captures, correct fixture and executable
hashes, and byte-identical guest reports, keyboard witnesses and frames in a
second run. It also verifies the ROM input remains unchanged. Both the guest
and inputs are experimental; this is not a physical or human input test.

```powershell
$env:BOOTHUSH_EXE = 'C:\Repos\boothush\out\windows\boothush.exe'
$env:BOOTHUSH_STB_TITLE_ROM = 'C:\Repos\Not-Super-Tilt-Bro\out\title-deferred-test-20261114-d\title.rom'
python -B -m unittest discover -v -s tests -p test_mac_port_title_integration.py
```

With BootHush executable SHA-256
`1f306c427d1a8c42b9d547b2b25a424e4b4e6a389a52ae5861d030e0fe43bc25`,
the focused fixture test passed and skipped cleanly without the variable.
The non-x64 CTest selection passed 13/13 targets; its Python discovery ran
143 tests with 17 optional skips and included this fixture test. The skipped
or paused x64 lane was not tested. The port's canonical reproduction and
limitations are in `C:/Repos/Not-Super-Tilt-Bro/docs/mouse-title-evidence.md`.

The optional integration additionally records the scheduled title mouse input
and replays it with the same keyboard transitions. It checks matching captured
frames, guest memory and keyboard witness, while allowing the replay-specific
metadata to differ. With executable SHA-256
`5d4f610e98cbb8b03476f28929d6e5c0f64b941aafac3c7d5b6ef12c76c7eeb3`
and the `1a367b8e...` ROM, this focused integration passed on 2026-11-14.
The port's `C:/Repos/.task-temp/nstb-handoff-replay-20261114-b` receipt
preserves a separate five-capture replay and exact commands. These scheduled
inputs do not establish real hardware or human behavior.
The same focused integration passed against the new `7121c504...` ROM; its
fixture and independent port results are in
`C:/Repos/.task-temp/nstb-damage-cap-title-20261114-a`.
It also passed with the opt-in native-C movement title ROM SHA-256
`71de2f909f27348e23c28cb8a18ab869c04f79ee190092d43d8887e3093d2974`;
its build and live-movement mutation are recorded in
`C:/Repos/Not-Super-Tilt-Bro/docs/windows-native-c-evidence.md`.

## Optional port native-C fixture

`tests/test_mac_port_native_c_integration.py` independently exercises the
port's separately generated native 68000 C movement diagnostic. Set
`BOOTHUSH_STB_NATIVE_C_ROM` to the exact fixture SHA-256
`97f44775d70f8fa83191018b8fc4faa19e5b6a4d9a18f1e866ffac36b3717110`
and `BOOTHUSH_EXE` to the executable. The test checks repeated guest results,
ten expected movement states, and a deliberately mutated acceleration. It
skips without the optional ROM. It passed with executable SHA-256
`5d4f610e98cbb8b03476f28929d6e5c0f64b941aafac3c7d5b6ef12c76c7eeb3`;
the port's source, toolchain, commands and limits are in
`C:/Repos/Not-Super-Tilt-Bro/docs/windows-native-c-evidence.md`. This is a
bounded software fixture, not a physical Mac or full-game test.

The subsequent two-direction component fixture is separately enabled by
`BOOTHUSH_STB_NATIVE_COMPONENT_ROM`, pinned to SHA-256
`4f60b4f9500318f56a6f8c23331f7bd61062bc67f6e651d24ee596eef7ac429a`.
The same test module checks its twenty expected position/velocity pairs,
repeatability, and an acceleration mutation. Both fixtures passed with the
executable above; each independently skips when its ROM is absent. The port
records the source and build inputs in its Windows-native C evidence. This
checks execution in the model, not physical behavior or a playable game.

The focused test also repeats title input at 1,024 and 256 modeled cycles per
mouse edge, verifying repeatable guest results and that all 64 modeled phase
steps are scheduled and consumed. The 1,024-cycle case must reach X=288; the
256-cycle result is characterized without requiring the current loss to remain.
Both integration tests pass for all four pinned ROMs using executable
`5d4f610e98cbb8b03476f28929d6e5c0f64b941aafac3c7d5b6ef12c76c7eeb3`,
and skip without the optional fixture. This is a model/guest integration
regression, not evidence of physical mouse speed or completeness.

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
