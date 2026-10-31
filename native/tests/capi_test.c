/* Exercises the C ABI exactly as the .NET layer uses it. usage: dbpfx-capi-test [ts3.package] */
#include "dbpfx/dbpfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { ++fails; printf("  FAIL line %d: %s (%s)\n", __LINE__, #c, dbpfx_last_error()); } } while (0)

int main(int argc, char** argv) {
    printf("%s\n", dbpfx_version());
    if (dbpfx_platform_check() != 0) {
        printf("unsupported platform: %s\n", dbpfx_last_error());
        return 1;
    }
    CHECK(dbpfx_platform_advisory() != NULL);

    /* build a package in memory through the editor */
    dbpfx_editor* ed = NULL;
    CHECK(dbpfx_editor_new(DBPFX_GAME_SIMS4, &ed) == 0);
    char text[4000];
    for (int i = 0; i < (int)sizeof text; ++i) text[i] = "plumbob "[i % 8];
    CHECK(dbpfx_editor_add(ed, 0x220557DA, 0, 1, text, sizeof text, 0, 1) == 0);
    CHECK(dbpfx_editor_add(ed, 0x00B2D882, 0, 2, "raw", 3, 0, 0) == 1);
    CHECK(dbpfx_editor_add(ed, 0x00B2D882, 0, 2, "dup", 3, 1, 0) == -1);
    uint8_t* mem = NULL;
    uint64_t mem_len = 0;
    CHECK(dbpfx_editor_save_memory(ed, &mem, &mem_len) == 0);
    dbpfx_editor_free(ed);

    dbpfx_package* p = NULL;
    CHECK(dbpfx_open_memory(mem, mem_len, &p) == 0);
    dbpfx_free(mem);
    CHECK(dbpfx_count(p) == 2);
    CHECK(dbpfx_detected_game(p) == DBPFX_GAME_SIMS4);
    dbpfx_header h;
    CHECK(dbpfx_header_get(p, &h) == 0 && h.major == 2 && h.minor == 1 && h.index_count == 2);
    dbpfx_entry es[4];
    CHECK(dbpfx_entries(p, 0, 4, es) == 2);
    CHECK(es[0].compression == DBPFX_COMPRESS_ZLIB && es[0].mem_size == sizeof text && es[0].file_size < sizeof text);
    CHECK(es[1].compression == 0 && es[1].mem_size == 3);
    CHECK(sizeof(dbpfx_entry) == 40 && es[0].index == 0 && es[1].index == 1);

    int64_t i = dbpfx_find(p, 0x00B2D882, 0, 2);
    CHECK(i == 1);
    const uint8_t* d = NULL;
    uint64_t n = 0;
    CHECK(dbpfx_direct(p, (uint32_t)i, &d, &n) == 1 && n == 3 && memcmp(d, "raw", 3) == 0);
    CHECK(dbpfx_direct(p, 0, &d, &n) == 0); /* compressed: needs read_into */
    char back[4000];
    CHECK(dbpfx_read_into(p, 0, back, sizeof back) == 0 && memcmp(back, text, sizeof text) == 0);
    CHECK(dbpfx_read_into(p, 0, back, 10) < 0); /* wrong size is an error, not a crash */
    CHECK(dbpfx_find(p, 1, 2, 3) == -1);
    CHECK(dbpfx_get_entry(p, 99, es) == DBPFX_E_RANGE);
    uint32_t hits[4];
    CHECK(dbpfx_find_type(p, 0x00B2D882, hits, 4) == 1 && hits[0] == 1);

    /* edit an opened package and save it to disk */
    CHECK(dbpfx_editor_open(p, DBPFX_GAME_SIMS3, &ed) == 0);
    CHECK(dbpfx_editor_replace(ed, 1, "changed!", 8) == 0);
    CHECK(dbpfx_editor_live_count(ed) == 2 && dbpfx_editor_slot_state(ed, 0) == 0);
    CHECK(dbpfx_editor_remove(ed, 0) == 0 && dbpfx_editor_slot_state(ed, 0) == 1 && dbpfx_editor_live_count(ed) == 1);
    CHECK(dbpfx_editor_restore(ed, 0) == 0 && dbpfx_editor_find(ed, 0x220557DA, 0, 1) == 0);
    CHECK(dbpfx_editor_save_as(ed, "capi_out.package") == 0);
    dbpfx_editor_free(ed);
    dbpfx_close(p);

    CHECK(dbpfx_open("capi_out.package", &p) == 0);
    CHECK(dbpfx_detected_game(p) == DBPFX_GAME_SIMS3);
    CHECK(dbpfx_direct(p, 1, &d, &n) == 1 && n == 8 && memcmp(d, "changed!", 8) == 0);
    CHECK(dbpfx_read_into(p, 0, back, sizeof back) == 0 && memcmp(back, text, sizeof text) == 0);
    dbpfx_close(p);
    remove("capi_out.package");

    CHECK(dbpfx_open("does-not-exist.package", &p) == DBPFX_E_IO);
    if (argc > 1) {
        CHECK(dbpfx_open(argv[1], &p) == 0);
        printf("  %s: %u entries\n", argv[1], dbpfx_count(p));
        dbpfx_close(p);
    }
    printf(fails ? "C ABI: %d FAILED\n" : "C ABI: all checks passed\n", fails);
    return fails ? 1 : 0;
}
