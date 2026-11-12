# Macintosh 128K clock and parameter RAM

The original 343-0040 clock uses VIA PB0 for bidirectional data, PB1 for the
serial clock and PB2 for active-low chip enable. Guest writes are sampled on
rising clock edges and device responses appear on falling edges. Each byte
travels most significant bit first. The clock's once-per-second signal is
wired to VIA CA2.

BootHush starts the 32-bit seconds counter at the Macintosh epoch, January 1,
1904. It increments at 7,833,600 guest cycles per second. The 20 bytes of PRAM
start zeroed and changes last for the current emulator instance. The guest can
write the time or PRAM and can protect both with the clock's write-protect
register. Factory test modes and battery-backed persistence are unimplemented.

Hardware references are Apple's [*Inside Macintosh*, volume III, pp. III-25,
III-37–38, and III-42–43](https://puyoman.net/documentation/Inside_Macintosh.pdf)
and the [MAME 343-0040 register-decoding notes](https://github.com/mamedev/mame/blob/master/src/mame/apple/macrtc.cpp)
for the seconds-command aliases.
