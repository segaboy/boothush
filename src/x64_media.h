#ifndef BOOTHUSH_X64_MEDIA_H
#define BOOTHUSH_X64_MEDIA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t partition_lba;
    uint32_t partition_sectors, efi_size, entry_rva;
    uint8_t image_sha256[32], efi_sha256[32];
} x64_media_info;

/* Validates a fixed 64 MiB GPT/FAT32 image without executing its contents. */
const char *x64_media_verify(const uint8_t *image, size_t size,
                             const uint8_t *expected_efi, size_t expected_size,
                             x64_media_info *info);

#ifdef __cplusplus
}
#endif
#endif
