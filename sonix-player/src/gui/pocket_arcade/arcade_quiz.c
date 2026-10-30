#include "arcade_quiz.h"

#include <limits.h>
#include <string.h>
#include <ctype.h>

static uint32_t rng(uint32_t *state) {
    uint32_t value = *state ? *state : 123u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return *state = value;
}

/* Bound quiz text by both bytes and complete UTF-8 code points.
 * Music-library metadata is never changed; only display text is shortened. */
static void title_copy(char out[PA_QUIZ_TITLE], const char *title) {
    size_t length = 0;
    while (length < PA_QUIZ_TITLE - 1 && title[length]) {
        length++;
    }
    if (title[length]) {
        /* A continuation byte means the truncation split a code point. */
        while (length && (((unsigned char)title[length] & 0xc0u) == 0x80u)) {
            length--;
        }
    }
    memcpy(out, title, length);
    out[length] = '\0';
}

bool pa_quiz_pool_add(char (*catalog)[PA_QUIZ_TITLE], int *count, int capacity,
                      const char *title) {
    if (!catalog || !count || capacity <= 0 || *count < 0 || *count > capacity || !title) {
        return false;
    }
    /* Some players have empty/space-only metadata: never offer a blank
     * multiple-choice answer, even when the source contains whitespace. */
    bool has_visible = false;
    for (const unsigned char *s = (const unsigned char *)title; *s; ++s) {
        if (!isspace(*s)) { has_visible = true; break; }
    }
    if (!has_visible) return *count < capacity;

    /* Compare stored representations, not full source titles: long names may
     * otherwise produce two identical choices after truncation. */
    char clipped[PA_QUIZ_TITLE] = {0};
    title_copy(clipped, title);
    for (int i = 0; i < *count; ++i) {
        if (strcmp(catalog[i], clipped) == 0) {
            return *count < capacity;
        }
    }
    if (*count == capacity) {
        return false;
    }
    memcpy(catalog[*count], clipped, sizeof clipped);
    ++*count;
    return *count < capacity;
}

bool pa_quiz_build(pa_quiz_t *quiz, const char *playing,
                   const char (*catalog)[PA_QUIZ_TITLE], int count, uint32_t seed) {
    if (!quiz) {
        return false;
    }
    memset(quiz, 0, sizeof(*quiz));
    quiz->correct = -1;
    quiz->selected = -1;
    if (!playing || !playing[0] || !catalog || count <= 0) {
        return false;
    }
    bool valid_title = false;
    for (const unsigned char *s = (const unsigned char *)playing; *s; ++s) {
        if (!isspace(*s)) { valid_title = true; break; }
    }
    if (!valid_title) return false;

    char answer[PA_QUIZ_TITLE];
    title_copy(answer, playing);
    int indices[3] = {-1, -1, -1};
    int found = 0;
    uint32_t state = seed ? seed : 1;

    /* Reservoir sample unique decoy titles. This is intentionally bounded in
     * memory and performs no heap allocations. Catalog deduplication is O(n^2). */
    int total = 0;
    for (int i = 0; i < count; i++) {
        if (!catalog[i][0] || strcmp(catalog[i], answer) == 0) {
            continue;
        }
        bool duplicate = false;
        for (int k = 0; k < i; k++) {
            if (strcmp(catalog[k], catalog[i]) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        total++;
        if (found < 3) {
            indices[found++] = i;
        } else {
            int selected = (int)(rng(&state) % (unsigned)total);
            if (selected < 3) {
                indices[selected] = i;
            }
        }
    }
    if (found < 3) {
        return false;
    }

    quiz->correct = (int)(rng(&state) % 4u);
    int decoy = 0;
    for (int i = 0; i < 4; i++) {
        const char *title = i == quiz->correct ? answer : catalog[indices[decoy++]];
        title_copy(quiz->options[i], title);
    }
    return true;
}

bool pa_quiz_select(pa_quiz_t *quiz, int choice) {
    if (!quiz || quiz->correct < 0 || quiz->selected >= 0 || choice < 0 || choice > 3) {
        return false;
    }
    quiz->selected = choice;
    if (quiz->questions < UINT32_MAX) {
        quiz->questions++;
    }
    if (choice == quiz->correct) {
        if (quiz->streak < UINT32_MAX) {
            quiz->streak++;
        }
        if (quiz->streak > quiz->best_streak) {
            quiz->best_streak = quiz->streak;
        }
        uint32_t gain = (uint32_t)(100 + (quiz->streak > 10 ? 10 : quiz->streak) * 20);
        quiz->points = UINT32_MAX - quiz->points < gain ? UINT32_MAX : quiz->points + gain;
    } else {
        quiz->streak = 0;
    }
    return choice == quiz->correct;
}

bool pa_quiz_next(pa_quiz_t *quiz, const char *playing,
                  const char (*catalog)[PA_QUIZ_TITLE], int count, uint32_t seed) {
    if (!quiz) {
        return false;
    }
    uint32_t points = quiz->points;
    uint32_t streak = quiz->streak;
    uint32_t best = quiz->best_streak;
    uint32_t questions = quiz->questions;
    pa_quiz_t next;
    if (!pa_quiz_build(&next, playing, catalog, count, seed)) {
        return false;
    }
    *quiz = next;
    quiz->points = points;
    quiz->streak = streak;
    quiz->best_streak = best;
    quiz->questions = questions;
    return true;
}
