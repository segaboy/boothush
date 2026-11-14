# ClasSICK x86-64 enablement

The objective is a Windows 11, headless BootHush test environment for ClasSICK's original x86-64 UEFI port, using its real disk image and automated guest input and diagnostics. The current implementation **does not boot an x86-64 guest**. An experimental software probe can interpret a small subset of instructions in an EFI image without firmware. `pc286` is a different 16-bit machine. Media inspection and the probe are foundations, not proof of a UEFI boot or guest behavior.

## Experimental software instruction probe

The original BootHush interpreter below avoids the Windows Hypervisor Platform and requires no additional package or firmware. It is a deliberately small diagnostic path, not a complete x86-64 CPU, sandbox for arbitrary code, or machine profile. It extracts the EFI application from the validated disk, maps it at `0x10000000`, and interprets from its PE entry point using zeroed general registers, RFLAGS `2`, and a zero-filled 64 KiB stack at `0x20000000` with initial RSP `0x2000fff8`. In particular, RCX and RDX are zero; no valid UEFI image handle or system table is supplied. Only instruction fetches from the mapped image and 8-byte writes to the artificial stack are implemented. There is no instruction execution on the host, firmware, memory mapping services, timers, graphics, input, interrupts, or return-to-firmware path.

```powershell
& 'C:\Repos\boothush\out\windows\boothush.exe' --probe-x64-software `
  'C:\ClasSICK\pc-fast-drag-20261112-a\window-drawing.img' --steps 1000
```

The subset consists of `push r64`, relative `call`, `sub rsp, imm8/imm32`, `mov r64, imm64` for the first eight registers, `lea r8,[rsp+disp8]`, `lea rax,[rbp+disp8]`, register-direct `test` for the first eight or the upper eight registers, relative 32-bit `je`/`jne`, and the specifically encoded `mov [rsp+disp32],rax`, `mov [rsp+disp8],imm32`, `mov [r8],rax`, and `mov [r8+disp8],imm32` forms. Writes remain restricted to the artificial stack. Other encodings stop before execution. The probe is bounded to 1-1,000,000 instructions; its JSON reports disk and EFI SHA-256, stop reason, executed step count, RIP, selected registers and flags, first opcode byte, and memory fault address. `step_limit` exits 0; unsupported instructions and memory faults exit 1; invalid requests exit 2. A stop is not a successful EFI load or firmware exit. The disk is read-only. No timing or hardware fidelity is claimed.

For the pinned disk and EFI hashes below, the probe executed 30 instructions, then stopped at `0x10001cd4` on unsupported opcode `0x48`. This path is reached with zero RCX and RDX, so it is not the result of the ClasSICK loader operating under UEFI. Synthetic tests separately check calls, stack bytes, register and flag results, branches, unsupported instructions, stack bounds, truncated instruction fetch, step limits, and unchanged input media. As the interpreter gains instructions, this recorded stop will change.

## Check the current ClasSICK disk

Rebuild the local checkout first, then run:

```powershell
$exe = 'C:\Repos\boothush\out\windows\boothush.exe'
$disk = 'C:\ClasSICK\pc-fast-drag-20261112-a\window-drawing.img'
$efi = 'C:\ClasSICK\pc-fast-drag-20261112-a\BOOTX64.EFI'
& $exe --verify-x64-media $disk --expect-efi $efi
```

This opens the files read-only, creates no guest or display, checks the protective MBR and GPT header and entry-table CRCs and mirrors, locates the single EFI system partition, checks supported FAT32 geometry and both FAT copies, follows the short-name path `EFI/BOOT/BOOTX64.EFI`, and checks key PE32+ x86-64 EFI-application headers, section bounds, and executable entry point. With `--expect-efi`, it compares every extracted byte with the supplied file. Success exits 0 with JSON `status` `media_verified`, SHA-256 hashes, partition and payload details, and `executed: false`; invalid or unsupported input exits 2 with the usual JSON `error`. Without `--expect-efi` it reports the embedded payload hash but cannot establish identity with a separate file.

This is a narrow preflight, not a comprehensive GPT, FAT or PE conformance checker, an EFI loader, a security scanner, or a claim that firmware will boot the image. Currently it accepts exactly 67,108,864-byte raw disks with 512-byte sectors, one EFI partition, two identical FAT32 tables and the stated 8.3 path. The extracted EFI file and optional expected file must each fit within 16 MiB. Other valid layouts can be rejected. Files are never modified. Use a parent process with redirected streams and `CREATE_NO_WINDOW` for a hidden console, as in the integration guide.

For the supplied original image, the checked disk SHA-256 is `80c279a69264cb07f2042bf14503c0687fa32e71057ee1196fc8174afbeeae4f` and the embedded and standalone EFI payload SHA-256 is `df284097539ed6b7c90700cb27589900b20d2e4c7574b7637ba5f387abba9fe9`. These identify that particular input, not every ClasSICK build. The test suite builds an independent synthetic GPT/FAT32/PE fixture and exercises corruption and mismatched files. Set `BOOTHUSH_CLASSICK_X64_DISK` and `BOOTHUSH_CLASSICK_X64_EFI` to these original local files to include the optional pinned-media test. Media and generated fixtures stay outside Git.

## Map the disk's EFI image in host memory

The next independently testable step constructs the actual disk payload's PE32+ image at a requested aligned base. It copies PE headers and sections, zero-fills the image, and applies supported base relocations. It remains a host-memory operation, with no CPU, firmware, guest memory, protection enforcement, or execution:

```powershell
& 'C:\Repos\boothush\out\windows\boothush.exe' --map-x64-media `
  'C:\ClasSICK\pc-fast-drag-20261112-a\window-drawing.img' --load-base 0x20000000
```

The successful JSON has `status:image_mapped`, `source:gpt_fat32`, `executed:false`, input and mapped SHA-256 hashes, preferred and chosen bases, entry address, mapped image size and relocation count. The original candidate produced a 221,184-byte image with three relocations and SHA-256 `1511747e91c9533bd48d365d9e9a32fcf02ad1ea2d90c9177b0b1810874cf0e7` at `0x20000000`; the payload's original preferred base is `0x10000000`. The optional pinned-media test compares that hash with a separately constructed expected image. The map is discarded at process exit and input media is unchanged.

The current narrow loader accepts a 4 KiB-aligned 64-bit load address, a bounded PE32+ x86-64 EFI application, nonoverlapping sections within the declared image and file, and `ABSOLUTE` padding plus `DIR64` base relocations. It rejects malformed or unsupported relocation records and moving an image without relocations. It does not perform imports, protocol resolution, TLS, exception registration, page-permission enforcement or entry invocation; mapping success is not proof a firmware implementation will boot the image. Synthetic tests exercise section copies, zero fill, relocation, bounds, unsupported types, overlap and unchanged output after failure.

## Remaining implementation

The opt-in [PS/2 auxiliary controller component](ps2-auxiliary.md) now has synthetic tests for the ClasSICK startup commands, status routing, ordered mouse packets, keyboard interleaving and bounded queue handling. It is not connected to an x64 CPU, interrupt controller or CLI machine profile; these tests do not boot the supplied disk.

The independent `X64BootServices` host-side model in `src/x64_boot_services.*` implements a narrowly scoped page-allocation ledger, memory-map snapshot keys and an exit transition. Allocations use an inclusive maximum byte address, return whole aligned pages, and invalidate the prior map key. Exact allocation frees coalesce adjacent free ranges and also invalidate the key; unsuccessful changes do not. An exit with a stale key is rejected, and the model rejects map/allocation/free operations after a successful exit. Its synthetic test covers a 128-page allocation under `2^47` and stale-key retries. This is a first testable state machine only: it has no EFI ABI or status-code mapping, guest pointers, real guest memory, protocols, actual firmware service calls, or loader integration. Partial frees are deliberately unsupported. It is not evidence that any EFI application has executed or exited boot services.

The separate `X64EfiMemory` experiment adds a host-callable translation layer over that model. It validates offsets in a caller-supplied byte span, writes little-endian 40-byte version-1 descriptors and EFI-style status values, and tests AllocateMaxAddress/LoaderData, exact free, GetMemoryMap and image-handle/key checks for ExitBootServices. Short buffers return the needed size; invalid or overlapping output ranges are refused before publishing a map. A test reserves the mapped candidate's complete image span as LoaderCode in a synthetic ledger; it does not establish real section classifications or load the image into guest RAM. The ledger does not describe framebuffer, firmware or all physical memory, and is not coupled to allocated RAM. It has no callable EFI tables, x64 calling convention, CPU dispatch, or guest integration. Its output is not a complete UEFI memory map. The [development receipt](x64-efi-memory-evidence-20261114.md) records the synthetic test and source scope.

The planned machine needs a suitable x86-64 CPU execution engine, a defined platform and original UEFI implementation, loading and executing the EFI application from the actual image, and correct memory-map and `ExitBootServices` behavior. It also needs the graphics, timer, keyboard and mouse interfaces used by the guest; deterministic, bounded headless input and replay; guest-state and CWP1 diagnostics; and original synthetic tests followed by the supplied ClasSICK image. These are future work, not available flags. Firmware behavior, instruction coverage, timing and compatibility must be demonstrated by tests, not inferred from media validation.

The working design candidate is a single-vCPU Windows Hypervisor Platform (WHPX) backend paired with a separately implemented, narrow BootHush UEFI model. This is an investigation choice, not an implemented or selected shipping backend: capability access, partition creation, guest execution, instruction baseline, isolation and deterministic replay remain unverified. Firmware/model identity and services cannot be reported until implemented. The host OS `WinHvPlatform.dll` API would be a Windows dependency; no third-party CPU engine or firmware package is included by this plan. Any external firmware or additional package requires the owner's review under the x64 handoff.

Check the host query without making a partition or changing Windows settings:

```powershell
& 'C:\Repos\boothush\out\windows\boothush.exe' --x64-capability
```

The command queries the hypervisor-present capability. A successful query returns `x64_capability`, `hypervisor_present`, `implemented:false` and `guest_executed:false`; an API or DLL failure returns exit 1 and `x64_capability_error` with an HRESULT when available. A positive query does not establish that partition creation, this guest, or deterministic replay will work. The development session's query returned access denied (`0x80070005`), so host availability and partition creation remain unknown in that session. No Windows feature is enabled and no other emulator is launched.

The firmware candidate must load the EFI payload extracted from the supplied disk, map the image and actual GOP framebuffer, provide guest-callable tables and stateful services, and reject stale memory-map keys rather than bypassing `ExitBootServices`. ClasSICK SPEC-0007 calls for LoadedImage information and a 128-page bundle with a preallocated memory-map buffer; SPEC-0006 defines the handoff transaction. SPEC-0010 requires discovery of a 3,579,545 Hz ACPI PM timer, while SPEC-0040 describes the guest's 8042 auxiliary-device exchange. These are requirements to implement and test, not already supported services. A capability query or a synthetic service call is not evidence of a boot, and a hardware-accelerated backend would need its timing and replay guarantees measured separately. The later BIOS path remains a separate future profile.
