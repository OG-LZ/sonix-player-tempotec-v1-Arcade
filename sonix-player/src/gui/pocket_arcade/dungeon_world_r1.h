#ifndef DUNGEON_WORLD_R1_H
#define DUNGEON_WORLD_R1_H
#define DG_WORLD_PAGES 24u
typedef struct {const char *title;const char *text;unsigned requires_testimony;} dg_world_page;
const dg_world_page *dg_world_page_at(unsigned id);
#endif
