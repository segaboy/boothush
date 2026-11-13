#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video_capture.h"
#include "mac128k.h"
#include "pc286.h"

static bool write_all(HANDLE file, const void *data, DWORD size)
{
    DWORD written = 0;
    return WriteFile(file, data, size, &written, NULL) && written == size;
}

static bool open_sized(video_capture *video, const wchar_t *path, bool mac,
                       uint32_t width, uint32_t height, uint32_t clock_hz,
                       uint32_t fps, uint32_t limit, uint64_t start)
{
    video->file = INVALID_HANDLE_VALUE;
    video->width = width;
    video->height = height;
    video->fps = fps;
    video->limit = limit;
    video->start = start;
    video->clock_hz = clock_hz;
    const size_t pixels = (size_t)video->width * video->height;
    video->planes = malloc(pixels * 3);
    video->rgb = mac ? NULL : malloc(pixels * 3);
    if (!video->planes || (!mac && !video->rgb)) {
        free(video->planes); free(video->rgb);
        video->planes = NULL; video->rgb = NULL;
        return false;
    }
    video->file = CreateFileW(path, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (video->file == INVALID_HANDLE_VALUE) {
        free(video->planes); free(video->rgb);
        video->planes = NULL; video->rgb = NULL;
        return false;
    }
    char header[128];
    const int length = snprintf(header, sizeof(header), "YUV4MPEG2 W%u H%u F%u:1 Ip A1:1 C444\n",
                                video->width, video->height, video->fps);
    if (length <= 0 || (size_t)length >= sizeof(header) || !write_all(video->file, header, (DWORD)length)) {
        video_finish(video, true);
        return false;
    }
    return true;
}

bool video_open(video_capture *video, const wchar_t *path, bool mac,
                uint32_t fps, uint32_t limit, uint64_t start)
{
    return open_sized(video,path,mac,mac?MAC128K_FRAME_WIDTH:PC286_FRAME_WIDTH,
                      mac?MAC128K_FRAME_HEIGHT:PC286_FRAME_HEIGHT,mac?7833600:6000000,fps,limit,start);
}

bool video_open_rgb(video_capture *video, const wchar_t *path, uint32_t width, uint32_t height,
                    uint32_t clock_hz, uint32_t fps, uint32_t limit, uint64_t start)
{
    return open_sized(video,path,false,width,height,clock_hz,fps,limit,start);
}

uint64_t video_next_cycle(const video_capture *video)
{
    return video->start + ((uint64_t)video->count * video->clock_hz + video->fps - 1) / video->fps;
}

static uint8_t clamp(int value)
{
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

bool video_append(video_capture *video, const uint8_t *mac_bitmap,
                  const uint8_t *pc_rgb, uint64_t cycle)
{
    const size_t pixels = (size_t)video->width * video->height;
    uint8_t *y = video->planes, *u = y + pixels, *v = u + pixels;
    if (mac_bitmap) {
        for (size_t i = 0; i < pixels; ++i)
            y[i] = (mac_bitmap[i / 8] & (0x80u >> (i % 8))) ? 16 : 235;
        memset(u, 128, pixels); memset(v, 128, pixels);
    } else {
        for (size_t i = 0; i < pixels; ++i) {
            int r = pc_rgb[i * 3], g = pc_rgb[i * 3 + 1], b = pc_rgb[i * 3 + 2];
            y[i] = clamp(16 + ((66 * r + 129 * g + 25 * b + 128) >> 8));
            u[i] = clamp(128 + ((-38 * r - 74 * g + 112 * b + 128) >> 8));
            v[i] = clamp(128 + ((112 * r - 94 * g - 18 * b + 128) >> 8));
        }
    }
    if (!write_all(video->file, "FRAME\n", 6) ||
        !write_all(video->file, video->planes, (DWORD)(pixels * 3))) return false;
    ++video->count;
    video->last_cycle = cycle;
    return true;
}

bool video_finish(video_capture *video, bool discard)
{
    bool ok = true;
    if (video->file != INVALID_HANDLE_VALUE) {
        if (discard) {
            FILE_DISPOSITION_INFO disposition = {TRUE};
            ok = SetFileInformationByHandle(video->file, FileDispositionInfo, &disposition, sizeof(disposition)) != 0;
        }
        if (!CloseHandle(video->file)) ok = false;
        video->file = INVALID_HANDLE_VALUE;
    }
    free(video->planes); free(video->rgb);
    video->planes = NULL; video->rgb = NULL;
    return ok;
}
