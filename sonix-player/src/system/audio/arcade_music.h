/* R22.5: read-only rhythm telemetry for Pocket Arcade. GPL-3.0-or-later.
 * Does not change PCM, obtain an audio handle, or run DSP on the GUI thread.
 * Level/onsets are approximate, not sample-accurate rhythm quantization. */
#ifndef SONIX_ARCADE_MUSIC_H
#define SONIX_ARCADE_MUSIC_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    unsigned level;     /* current volume envelope, 0..100 */
    unsigned bass;      /* approximate low-frequency energy, 0..100 */
    uint32_t onset;     /* monotonically incrementing transient counter */
    bool valid;         /* recent decoded PCM was captured */
} arcade_music_snapshot_t;
void arcade_music_set_active(bool active);
/* Audio playback thread only. 0 allocations, bounded <=96 sample points.
 * Pass decoded PCM only. Never pass DoP/DSD bitstreams. */
void arcade_music_feed_pcm(const void *pcm, int frames, int channels, int bits);
/* LVGL thread only; safe without mutex. */
arcade_music_snapshot_t arcade_music_snapshot(void);
#endif
