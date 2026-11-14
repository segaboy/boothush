# Experimental NES library

BootHush exposes its NES core to other Windows applications through a C ABI, without launching the CLI, requiring a window, or taking host input. This is version **0.1.1**, an experimental integration interface; compatibility across future minor versions is not promised. `bh_nes_api_version()` returns `BH_NES_API_VERSION` (`0x000101` for 0.1.1). The machine is still experimental, with the cartridge and accuracy limits documented in [NES target](nes.md). A usable library is not a claim that the emulator is complete.

The indexed [frame series](headless-capture.md#frame-series), controller witness, filtered execution witness and cycle-based scenario scheduling are available through the CLI process interface. They are not exposed as C-library calls in 0.1.1; an embedding application can still step the core and copy its latest frame and state itself, within the contracts below. The execution witness additions do not change the public C ABI or its version.

## Install and consume

Build BootHush normally, then install to a writable directory. For example, from the checkout, with the same CMake and toolchain used for the build:

```powershell
cmake --build out/windows
cmake --install out/windows --prefix C:/Repos/boothush-sdk
cmake -S examples/nes-consumer -B out/nes-consumer -G Ninja `
  -DCMAKE_C_COMPILER=clang -DCMAKE_PREFIX_PATH=C:/Repos/boothush-sdk
cmake --build out/nes-consumer
out/nes-consumer/nes_consumer.exe C:/path/to/cartridge.nes
```

The install contains `include/boothush/nes.h`, `bin/boothush_nes.dll`, the toolchain's import library, and a CMake config. CMake projects can use `find_package(BootHushNES 0.1 CONFIG REQUIRED)` and `target_link_libraries(app PRIVATE BootHush::nes)`. The [standalone example](../examples/nes-consumer/) demonstrates this without including private BootHush headers. The DLL must be deployed alongside the application or otherwise be on its DLL search path. Use a compatible Windows architecture, calling convention, and toolchain/import library. The verified build uses 64-bit LLVM-MinGW; MSVC and other architectures have not been verified. MinGW consumers receive a GNU-format import library; MSVC users may need to generate a compatible import library. No ROM is distributed with the SDK.

## Calling contract

See the public header for signatures and result codes. A typical caller reads an appropriately licensed cartridge into memory, calls `bh_nes_create`, optionally sets a controller state, repeatedly calls `bh_nes_run`, copies completed frames and drains audio, and finally calls `bh_nes_destroy`. Check each result. The cartridge is copied during creation and the caller may immediately release it. `bh_nes_destroy(NULL)` is safe. Each machine is independent; calls on the same machine must be serialized by the caller, including reads and destruction. No API call creates threads or touches physical controllers.

- `bh_nes_run` executes 1 through `BH_NES_MAX_STEPS` (1,000,000) instructions per call. `executed` reports successfully completed instructions, including on fault. Run in smaller chunks for cancellation, input scheduling, and frame/audio polling. There is no internal wall-clock deadline or blocking run-until-frame call. A fault is sticky; examine `bh_nes_get_state` and destroy the instance to restart.
- For `bh_nes_state`, initialize `size` to `sizeof(bh_nes_state)` before each call. Its counters, registers, error name, fault address and opcode are copied to caller memory. A faulting instruction is not counted; modeled interrupt or DMA time may already have advanced. There is no save-state or reset API.
- Controller ports 0 and 1 accept the `BH_NES_*` bit mask. The value is set immediately; to reproduce an input sequence, set it between instruction-bounded calls. Both ports are emulated and independent of host input devices.
- `bh_nes_create` chooses the timing model from the header; multi-region defaults to NTSC. `bh_nes_create_region` can explicitly select `BH_NES_REGION_NTSC` or `BH_NES_REGION_PAL`. Initialize `bh_nes_cartridge_info.size` before calling `bh_nes_get_cartridge_info` to obtain declared and selected region, mapper/submapper, current bank, ROM/RAM sizes and the raw battery flag. A flag alone does not allocate persistent memory; actual NVRAM is unsupported and rejected.
- `bh_nes_frame` copies the latest completed 256 by 240 packed RGB image, top to bottom, three bytes per pixel. Supply at least `BH_NES_FRAME_BYTES` bytes. `BH_NES_NO_FRAME` means the first frame has not completed. The caller owns the output; no internal framebuffer pointer is exposed.
- Enable audio at creation with `audio_enabled=1`. Read signed 16-bit native-endian mono PCM at `BH_NES_AUDIO_RATE` (44,100 Hz) with `bh_nes_audio_read`. A NULL buffer and zero capacity query the number of pending samples; a non-NULL buffer consumes up to capacity samples and reports the number copied. Drain regularly: at most 30 seconds are buffered, samples produced while full are dropped, and `audio_truncated` remains set. When disabled, the pending sample count is zero. Audio synthesis is approximate.
- `bh_nes_read` copies CPU RAM, its mirrors, or cartridge PRG ROM without side effects. Device and unmapped regions return `BH_NES_UNMAPPED`; on failure the destination is unchanged. It cannot read PPU registers or modify guest memory. A zero-byte read may use a NULL output.
- `bh_nes_read_ppu` copies from the modeled PPU address space, including CHR RAM, nametables and palette aliases. `bh_nes_read_oam` copies the modeled 256-byte OAM array. These are observational reads, not device-register reads, and do not advance time or change guest state. Serialize them with execution to capture one stopped state.
- `bh_nes_result_string` returns a static English diagnostic; state fault strings are copied into the state structure. The API does not expose internal storage pointers.

## Independent reference

The build's test suite includes the MIT-licensed [Agnès](https://github.com/kgabis/agnes) core, pinned with its license and provenance under `third_party/agnes`. A bounded, generated NROM fixture verifies that both cores produce a completed frame. If `BOOTHUSH_NES_ROM` is set, the same test also runs the caller-supplied supported NROM for 400,000 instructions in each core and checks that both complete frames. Agnès is a test-only independent reference, not a selectable production backend and not included in the NES DLL. This smoke comparison does not establish timing, audio, game compatibility or pixel accuracy; upstream Agnès does not implement audio. No external executable or proprietary firmware is required.
