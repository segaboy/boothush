# BootHush project direction

BootHush aims to be a headless development and testing platform for software that must run across different machines, operating systems, or hardware generations. It should help a developer observe how a program behaves on one supported platform, adapt it for another, and run repeatable checks of the resulting port. ClasSICK is the immediate project driving development and remains a priority; BootHush is intended to serve other projects too.

For example, a future developer might use BootHush to investigate software written for a Super Nintendo and test a port targeting a Panasonic 3DO. **Neither machine is currently implemented.** This example defines the breadth of the goal, not a promise that BootHush currently translates binaries, builds a 3DO port, or verifies either machine's behavior. A port can require changes to source, graphics, sound, timing, controls, storage, and platform-specific interfaces. BootHush's job is to make that work observable and testable; automatic conversion is not assumed.

## Intended development workflow

The long-term workflow should let a developer:

1. Identify a project, its source and target platforms, build artifacts, and the exact versions and settings used. Integrate with build tools where useful; distinguish a build failure from an emulator or guest failure.
2. Run supported source and target guests headlessly with bounded execution, supplied images, reproducible input, and clear stop or fault results. An explicitly requested interactive session can complement automated testing.
3. Capture the evidence needed to understand a behavior: guest state, memory and device activity, screenshots, recordings, and timing information within documented limits.
4. Express comparable scenarios across machines using platform-appropriate input and observations. Comparisons may need adapters for different resolutions, coordinate systems, pixel formats, byte order, clocks, or data layouts. They must retain the original evidence and identify transformations.
5. Compare expected behavior, investigate differences, change the software, and rerun the checks. A matching result should report exactly what was compared and under which backend and timing assumptions; it is not automatic proof of equivalence or physical-hardware compatibility.

This is the intended direction, not a description of all current commands. Supporting a new machine requires a real execution and device profile, original or appropriately licensed test materials, and tests demonstrating the advertised behavior. The availability of source code, tools, firmware, and media depends on the project; BootHush should give actionable errors when required inputs or capabilities are absent.

## Design priorities

- Keep unattended work headless, bounded and compatible with an available desktop. Do not take control of the user's physical input devices for automated runs. Make visible or host-interactive modes explicit.
- Keep source and target platform details in their machine and device profiles. Share orchestration, input scheduling, artifacts, diagnostics, capture and comparison where real cases justify a stable interface; do not assume all machines share a CPU, address space, timing model or display.
- Treat images and recordings as inputs, preserve original media by default, and write snapshots and captures to explicit new destinations. Identify the emulator build, machine profile, inputs and relevant settings in reproducible reports.
- Make trace limits, dropped events, incomplete input, timeouts, and unsupported features visible. Distinguish direct device input, window messages, host input and replay. State when replay or timing guarantees are unknown.
- Support comparison at a useful semantic level as well as byte or pixel level. Different hardware can produce equivalent behavior with different internal representations, so comparison rules must be explicit and testable.
- Use original generated fixtures and appropriately licensed project materials. Keep third-party dependencies and their provenance visible. Do not silently obtain firmware or substitute another emulator for a missing BootHush feature.
- Evolve from working vertical slices and preserve existing interfaces unless a justified change is documented and tested. Avoid designing a universal abstraction ahead of demonstrated needs.

## Current implementation and immediate work

The current Windows implementation provides `mac128k` and `pc286` machine profiles and an experimental [NES CPU-only target](nes.md), along with profile-specific bounded runs, scheduled input, inspections, witnesses, headless screenshots and video capture, and selected record/replay. The [README](../README.md) and individual profile guides define their actual scope. The NES target is not a complete console. Observed timing and replay results do not establish physical-hardware fidelity.

ClasSICK remains an immediate priority: investigate the Mac mouse behavior and build an x86-64 UEFI guest capable of running its actual disk headlessly. The x64 software probe interprets a small set of instructions from the real EFI image, but it supplies no UEFI context and **does not boot the guest**. Media inspection, PE mapping, the PS/2 component, and boot-services state model remain partial foundations. See the [ClasSICK integration guide](classick-integration.md) and [x64 notes](classick-x64.md).

Future support for additional source and target platforms, including the Super Nintendo and 3DO example, will need its own scoped machine profiles, test fixtures, toolchain integration, and comparison rules. Windows is the current host; support for other host operating systems is a future goal rather than a current capability.
