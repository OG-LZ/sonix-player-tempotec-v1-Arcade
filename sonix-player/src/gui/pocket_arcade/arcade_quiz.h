#ifndef POCKET_ARCADE_QUIZ_H
#define POCKET_ARCADE_QUIZ_H
#include <stdint.h>
#include <stdbool.h>
#define PA_QUIZ_OPTIONS 4
#define PA_QUIZ_TITLE 256
typedef struct {
    char options[PA_QUIZ_OPTIONS][PA_QUIZ_TITLE];
    int correct, selected;
    uint32_t points, streak, best_streak, questions;
} pa_quiz_t;
/* Add a distinct nonempty library title to a bounded pool. Duplicate song names
 * must NOT consume capacity (compilations frequently repeat titles). Returns
 * true while space remains; false on full/invalid input. */
bool pa_quiz_pool_add(char (*catalog)[PA_QUIZ_TITLE], int *count, int capacity,
                      const char *title);
/* Four choices from currently playing title and scanned titles. All titles are
 * copied; caller retains ownership of the catalog. False when fewer than 3
 * distinct decoys exist. Never starts, pauses, or seeks audio. */
bool pa_quiz_build(pa_quiz_t *q, const char *playing,
                   const char (*catalog)[PA_QUIZ_TITLE], int count,uint32_t seed);
bool pa_quiz_select(pa_quiz_t *q,int choice);
/* Retains run stats across a new question, unlike pa_quiz_build(). */
bool pa_quiz_next(pa_quiz_t *q,const char *playing,
                  const char (*catalog)[PA_QUIZ_TITLE],int count,uint32_t seed);
#endif
