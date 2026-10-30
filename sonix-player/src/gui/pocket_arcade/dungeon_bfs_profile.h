/* GPL-3.0-or-later. Opt-in native BFS instrumentation. */
#ifndef DUNGEON_BFS_PROFILE_H
#define DUNGEON_BFS_PROFILE_H
#include <stdint.h>
typedef struct {uint64_t searches, expansions, enqueues, successes, failures;} dg_bfs_counters;
/* Available only when dungeon_deluxe.c is compiled with -DDG_PROFILE_BFS. */
void dg_bfs_reset(void);
dg_bfs_counters dg_bfs_read(void);
#endif
