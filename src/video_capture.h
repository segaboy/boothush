#ifndef BOOTHUSH_VIDEO_CAPTURE_H
#define BOOTHUSH_VIDEO_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

typedef struct {
    HANDLE file;
    uint8_t *planes;
    uint8_t *rgb;
    uint32_t width, height, fps, limit, count, clock_hz;
    uint64_t start, last_cycle;
    bool truncated, failed;
} video_capture;

/* Opens a new file. Call finish on every successful open. */
bool video_open(video_capture *video, const wchar_t *path, bool mac,
                uint32_t fps, uint32_t limit, uint64_t start);
bool video_open_rgb(video_capture *video, const wchar_t *path, uint32_t width, uint32_t height,
                    uint32_t clock_hz, uint32_t fps, uint32_t limit, uint64_t start);
uint64_t video_next_cycle(const video_capture *video);
bool video_append(video_capture *video, const uint8_t *mac_bitmap,
                  const uint8_t *pc_rgb, uint64_t cycle);
bool video_finish(video_capture *video, bool discard);

#endif
