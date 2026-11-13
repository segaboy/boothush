#ifndef BOOTHUSH_X64_SOFT_PROBE_H
#define BOOTHUSH_X64_SOFT_PROBE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t steps, rip, rsp, rax, rbp, r8, rflags;
    uint64_t fault_address;
    uint8_t opcode;
    const char *stop;
} x64_soft_result;
/* Diagnostic interpreter only; no firmware, interrupts or real UEFI entry context. */
const char *x64_soft_probe(const uint8_t *disk, size_t size, uint32_t limit,
                           x64_soft_result *result, uint8_t disk_sha[32], uint8_t efi_sha[32]);
#ifdef __cplusplus
}
#endif
#endif
