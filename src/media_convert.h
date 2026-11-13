#ifndef BOOTHUSH_MEDIA_CONVERT_H
#define BOOTHUSH_MEDIA_CONVERT_H

#include <wchar.h>

/* Uses only the FFmpeg beside this executable. NULL means success. */
const char *convert_capture(const wchar_t *input, const wchar_t *output, const char **format);

#endif
