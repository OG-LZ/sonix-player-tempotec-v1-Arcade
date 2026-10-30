/* R22.5 Beat Raiders audio feature bridge: no playback transport calls.
 * A cheap envelope and onset detector, NEVER a full FFT or decoder.
 * This translation unit has no dependencies on the GUI or ALSA. */
#define _POSIX_C_SOURCE 200809L
#include "arcade_music.h"
#include <stddef.h>
#include <time.h>

static int active;
static unsigned published_level, published_bass;
static uint32_t published_onset, published_time;
/* The following state belongs ONLY to the single playback thread. */
static int32_t lowpass;
static uint32_t energy_floor, bass_floor, last_onset_ms;
static uint32_t now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint32_t)t.tv_nsec / 1000000u);
}
void arcade_music_set_active(bool enabled) {
    __atomic_store_n(&active, enabled ? 1 : 0, __ATOMIC_RELEASE);
}
static int32_t mono_sample(const void *pcm, size_t at, int channels, int bits) {
    int32_t sample;
    if (bits == 16) {
        const int16_t *p = pcm;
        sample = p[at * (size_t)channels];
        if (channels == 2) sample = (sample + p[at * 2u + 1u]) / 2;
    } else {
        const int32_t *p = pcm;
        sample = p[at * (size_t)channels] >> 16;
        if (channels == 2) sample = (sample + (p[at * 2u + 1u] >> 16)) / 2;
    }
    return sample;
}
void arcade_music_feed_pcm(const void *pcm, int frames, int channels, int bits) {
    if (!__atomic_load_n(&active, __ATOMIC_ACQUIRE) || !pcm || frames <= 0 ||
        (channels != 1 && channels != 2) || (bits != 16 && bits != 32)) return;
    /* A fixed upper bound. This is insignificant beside audio decoding, even
     * on single-core V1; nothing blocks, allocates, or touches the samples. */
    size_t stride = (size_t)frames / 96u;
    if (stride < 1u) stride = 1u;
    uint32_t amplitude = 0, low_energy = 0, sampled = 0;
    for (size_t i = 0; i < (size_t)frames && sampled < 96u; i += stride) {
        int32_t x = mono_sample(pcm, i, channels, bits);
        int32_t absx = x < 0 ? -x : x;
        lowpass += (x - lowpass) / 4;
        int32_t absl = lowpass < 0 ? -lowpass : lowpass;
        amplitude += (uint32_t)absx;
        low_energy += (uint32_t)absl;
        sampled++;
    }
    if (!sampled) return;
    unsigned mean = amplitude / sampled, low = low_energy / sampled;
    uint32_t t = now_ms();
    unsigned level = mean / 85u, bass = low / 55u;
    if (level > 100u) level = 100u;
    if (bass > 100u) bass = 100u;
    /* Floor is a moving envelope baseline, not an arbitrary fixed threshold.
     * Apply the onset check before adaptation so short kicks stand out. */
    if (!energy_floor) energy_floor = mean > 180u ? mean : 180u;
    if (!bass_floor) bass_floor = low > 120u ? low : 120u;
    bool strength_jump = mean > 500u && mean > energy_floor + energy_floor / 3u;
    bool bass_jump = low > 250u && low > bass_floor + bass_floor / 2u;
    bool transient = (strength_jump || bass_jump) &&
                     (uint32_t)(t - last_onset_ms) >= 240u;
    energy_floor = (energy_floor * 15u + mean + 8u) / 16u;
    bass_floor = (bass_floor * 15u + low + 8u) / 16u;
    if (transient) {
        last_onset_ms = t;
        __atomic_add_fetch(&published_onset, 1u, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&published_level, level, __ATOMIC_RELAXED);
    __atomic_store_n(&published_bass, bass, __ATOMIC_RELAXED);
    __atomic_store_n(&published_time, t, __ATOMIC_RELEASE);
}
arcade_music_snapshot_t arcade_music_snapshot(void) {
    arcade_music_snapshot_t out = {0};
    uint32_t t = __atomic_load_n(&published_time, __ATOMIC_ACQUIRE);
    if (!__atomic_load_n(&active, __ATOMIC_ACQUIRE) || !t ||
        (uint32_t)(now_ms() - t) > 900u) return out;
    out.level = __atomic_load_n(&published_level, __ATOMIC_RELAXED);
    out.bass = __atomic_load_n(&published_bass, __ATOMIC_RELAXED);
    out.onset = __atomic_load_n(&published_onset, __ATOMIC_RELAXED);
    out.valid = true;
    return out;
}
