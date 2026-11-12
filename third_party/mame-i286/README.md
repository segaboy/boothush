# Intel 80286 core from MAME

The five C++ files here are adapted from
[MAME 0.289](https://github.com/mamedev/mame/tree/f34f02505e32c1993c6a782b6814232cbfc74e36/src/devices/cpu/i86)
(commit `f34f02505e32c1993c6a782b6814232cbfc74e36`). Each upstream file
identifies Carl as the copyright holder and specifies BSD-3-Clause. See
[LICENSE](LICENSE) for the notice and redistribution terms.

| Upstream file | Git blob ID |
| --- | --- |
| `i286.cpp` | `7b331335e4178a4a28af987f92b2e376f5c7feb0` |
| `i286.h` | `aacd4ed01c2af9240481e8761a55250f1df82946` |
| `i86.cpp` | `05d6a7a07185f2f48f71a1151e527a0c009b7e95` |
| `i86.h` | `1d58f44f375bfbcbb6accb965b3a8fe0a3da06d2` |
| `i86inline.h` | `d5baf48ea8dbad6df8f4c84035ab20a1277e2058` |

BootHush replaces the MAME device, debugger, and scheduler integration with a
per-machine memory and I/O bus. The extraction retains the Intel 80286
instruction and protection machinery and the common instruction routines it
uses. It removes the 8086/8088 device implementations and wait-state machinery,
adds bounded scheduling for halt and otherwise untimed operations, and routes
software interrupts through BootHush's minimal firmware when explicitly
booting disk media with the built-in ROM. BootHush supplies interrupt
acknowledgment, wakes a halted CPU on an eligible IRQ, and allows devices to
handle word I/O transfers. The core's timing is approximate. The separate
BootHush PC board code supplies memory, video and firmware; no IBM ROM is
included.
