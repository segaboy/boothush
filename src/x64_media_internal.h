#ifndef BOOTHUSH_X64_MEDIA_INTERNAL_H
#define BOOTHUSH_X64_MEDIA_INTERNAL_H

#include "x64_media.h"
#include <vector>

// Caller owns the extracted immutable payload. This does not map or execute it.
const char *x64_media_extract_efi(const uint8_t *image, size_t size,
                                  x64_media_info &info, std::vector<uint8_t> &efi);
#endif
