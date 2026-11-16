#ifndef BOOTHUSH_LIVE_WINDOW_H
#define BOOTHUSH_LIVE_WINDOW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Shared optional host surface. The legacy Mac API remains source compatible. */
typedef struct mac_live_window bh_live_window;
typedef enum { BH_LIVE_MAC128K, BH_LIVE_PC286 } bh_live_profile;
typedef struct {
    uint8_t code;
    bool extended, pressed, repeated, focus_release;
} bh_live_key;
bh_live_window *bh_live_window_create(bh_live_profile profile,unsigned scale,bool visible);
/* RGB input is exactly 640x350x3; copied and converted for the Windows DIB. */
void bh_live_window_present_rgb(bh_live_window *window,const uint8_t *rgb);
bool bh_live_window_next_key(bh_live_window *window,bh_live_key *key);
size_t bh_live_window_pending_keys(const bh_live_window *window);
unsigned bh_live_window_focus_losses(const bh_live_window *window);
#ifdef __cplusplus
}
#endif
#endif
