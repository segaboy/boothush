# ClasSICK x86-64 enablement

The objective is a Windows 11, headless BootHush test environment for ClasSICK's original x86-64 UEFI port, using its real disk image and automated guest input and diagnostics. The current implementation **does not boot or execute x86-64 code**. `pc286` is a different 16-bit machine. Media inspection below is a foundation and preflight check, not a replacement for VirtualBox or proof of guest behavior.

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

## Remaining implementation

The planned machine needs a suitable x86-64 CPU execution engine, a defined platform and original UEFI implementation, loading and executing the EFI application from the actual image, and correct memory-map and `ExitBootServices` behavior. It also needs the graphics, timer, keyboard and mouse interfaces used by the guest; deterministic, bounded headless input and replay; guest-state and CWP1 diagnostics; and original synthetic tests followed by the supplied ClasSICK image. These are future work, not available flags. Firmware behavior, instruction coverage, timing and compatibility must be demonstrated by tests, not inferred from media validation.

The execution engine is not yet selected. The Windows Hypervisor Platform is a possible host API to investigate; in the development session a capability query returned access denied (`0x80070005`), so availability on the host remains unknown. A hypervisor backend would also need a separately justified determinism and isolation design. BootHush does not silently launch another emulator, download firmware, or require a visible desktop for this preflight. Any external firmware or additional package requires the owner's review under the x64 handoff.
