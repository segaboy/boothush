# Moira CPU core

The files in this directory other than this notice are from the `Moira/`
directory of [dirkwhoffmann/Moira at ce28239](https://github.com/dirkwhoffmann/Moira/tree/ce28239b0507ebfe1bf2ad2b9c9252dc2f2cc031/Moira).
Each imported Git blob was verified against that revision. BootHush changes
`MOIRA_EMULATE_ADDRESS_ERROR` from `false` to `true` in `MoiraConfig.h` so
68000 word and longword accesses at odd addresses raise the guest exception.
The core is licensed under MIT. See [LICENSE](LICENSE) for Dirk W. Hoffmann's
copyright and license notice. BootHush selects the 68000 model. The upstream
build also compiles the other models and the disassembler. The Moira test
applications and their other dependencies are outside this import.
