/* DBPFX - C ABI (consumed by the .NET layer through P/Invoke)
 *
 * Conventions
 *   - Functions returning int: 0 = success, <= -2 = failure; call dbpfx_last_error()
 *     (thread-local, UTF-8) for the message.
 *   - Paths are UTF-8.
 *   - Pointers handed out by dbpfx_raw / dbpfx_direct / dbpfx_header_bytes point into the
 *     memory-mapped file and stay valid until dbpfx_close() of that package handle.
 */
#ifndef DBPFX_H
#define DBPFX_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(DBPFX_BUILD)
#define DBPFX_API __declspec(dllexport)
#else
#define DBPFX_API __declspec(dllimport)
#endif
#else
#define DBPFX_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dbpfx_package dbpfx_package;
typedef struct dbpfx_editor dbpfx_editor;

/* -1 is reserved for "not found" / "rejected" results of the int64_t functions, so errors start at -2. */
enum { DBPFX_OK = 0, DBPFX_NOT_FOUND = -1, DBPFX_E_ARG = -2, DBPFX_E_IO = -3, DBPFX_E_FORMAT = -4, DBPFX_E_RANGE = -5, DBPFX_E_OTHER = -6 };
enum { DBPFX_GAME_AUTO = -1, DBPFX_GAME_SIMS3 = 0, DBPFX_GAME_MEDIEVAL = 1, DBPFX_GAME_SIMS4 = 2 };
enum { DBPFX_POLICY_KEEP = 0, DBPFX_POLICY_ALWAYS = 1, DBPFX_POLICY_NEVER = 2 };
enum { DBPFX_COMPRESS_NONE = 0x0000, DBPFX_COMPRESS_ZLIB = 0x5A42, DBPFX_COMPRESS_REFPACK = 0xFFFF };

/* 40 bytes, identical layout in C, C++ and C# (StructLayout.Sequential). */
typedef struct dbpfx_entry {
    uint32_t type;
    uint32_t group;
    uint64_t instance;
    uint32_t offset;      /* position of the stored bytes in the file */
    uint32_t file_size;   /* stored size, without the 0x80000000 flag */
    uint32_t mem_size;    /* uncompressed size */
    uint16_t compression; /* 0x0000 none, 0x5A42 zlib, 0xFFFE streamable, 0xFFFF RefPack, 0xFFE0 deleted marker */
    uint16_t committed;
    uint32_t index;       /* position of this entry in the package index */
    uint32_t reserved;
} dbpfx_entry;

typedef struct dbpfx_header {
    uint32_t major, minor;
    uint32_t user_major, user_minor;
    uint32_t unused1, created, updated, unused2;
    uint32_t index_count, index_position_low, index_size;
    uint32_t index_version, index_position;
    uint32_t index_flags;
} dbpfx_header;

DBPFX_API const char* dbpfx_version(void);
DBPFX_API const char* dbpfx_last_error(void);

/* ---- packages (read-only, memory-mapped, lazy) */
DBPFX_API int dbpfx_open(const char* path, dbpfx_package** out);
DBPFX_API int dbpfx_open_memory(const void* data, uint64_t len, dbpfx_package** out); /* copies data */
DBPFX_API void dbpfx_close(dbpfx_package* pkg);

DBPFX_API int dbpfx_header_get(const dbpfx_package* pkg, dbpfx_header* out);
DBPFX_API const uint8_t* dbpfx_header_bytes(const dbpfx_package* pkg); /* 96 bytes */
DBPFX_API int dbpfx_detected_game(const dbpfx_package* pkg);
DBPFX_API uint32_t dbpfx_count(const dbpfx_package* pkg);

/* Bulk-decode `n` index entries starting at `first`. Returns the number written or < 0. */
DBPFX_API int64_t dbpfx_entries(const dbpfx_package* pkg, uint32_t first, uint32_t n, dbpfx_entry* out);
DBPFX_API int dbpfx_get_entry(const dbpfx_package* pkg, uint32_t i, dbpfx_entry* out);
DBPFX_API int64_t dbpfx_find(const dbpfx_package* pkg, uint32_t type, uint32_t group, uint64_t instance);
/* Writes up to `cap` matching positions; returns the total number of matches. */
DBPFX_API int64_t dbpfx_find_type(const dbpfx_package* pkg, uint32_t type, uint32_t* out, uint32_t cap);

/* Stored bytes (maybe compressed), zero-copy. */
DBPFX_API int dbpfx_raw(const dbpfx_package* pkg, uint32_t i, const uint8_t** data, uint64_t* len);
/* Uncompressed bytes without copying, if the resource is stored uncompressed.
 * Returns 1 and sets data/len when possible, 0 when the caller must use dbpfx_read_into. */
DBPFX_API int dbpfx_direct(const dbpfx_package* pkg, uint32_t i, const uint8_t** data, uint64_t* len);
/* Decompress into caller memory; `len` must equal the entry's mem_size. */
DBPFX_API int dbpfx_read_into(const dbpfx_package* pkg, uint32_t i, void* dst, uint64_t len);
DBPFX_API void dbpfx_prefetch(const dbpfx_package* pkg, uint32_t i);

/* ---- editing */
DBPFX_API int dbpfx_editor_new(int game, dbpfx_editor** out);
DBPFX_API int dbpfx_editor_open(const dbpfx_package* base, int game, dbpfx_editor** out);
DBPFX_API void dbpfx_editor_free(dbpfx_editor* ed);
DBPFX_API int dbpfx_editor_set_game(dbpfx_editor* ed, int game);
DBPFX_API int dbpfx_editor_get_game(const dbpfx_editor* ed);
DBPFX_API int dbpfx_editor_set_policy(dbpfx_editor* ed, int policy);
DBPFX_API uint32_t dbpfx_editor_count(const dbpfx_editor* ed);
DBPFX_API int dbpfx_editor_key(const dbpfx_editor* ed, uint32_t i, uint32_t* type, uint32_t* group, uint64_t* instance);
DBPFX_API int dbpfx_editor_is_removed(const dbpfx_editor* ed, uint32_t i);
/* 0 = live resource, 1 = removed, 2 = TS4 deleted-record marker (kept, but not a resource). */
DBPFX_API int dbpfx_editor_slot_state(const dbpfx_editor* ed, uint32_t i);
DBPFX_API uint32_t dbpfx_editor_live_count(const dbpfx_editor* ed);
DBPFX_API int64_t dbpfx_editor_find(const dbpfx_editor* ed, uint32_t type, uint32_t group, uint64_t instance);
/* compress: -1 = editor policy, 0 = no, 1 = yes. Returns the new slot, -1 for rejected dup, < -1 error. */
DBPFX_API int64_t dbpfx_editor_add(dbpfx_editor* ed, uint32_t type, uint32_t group, uint64_t instance,
                                       const void* data, uint64_t len, int reject_dups, int compress);
DBPFX_API int dbpfx_editor_replace(dbpfx_editor* ed, uint32_t i, const void* data, uint64_t len);
DBPFX_API int dbpfx_editor_remove(dbpfx_editor* ed, uint32_t i);
DBPFX_API int dbpfx_editor_restore(dbpfx_editor* ed, uint32_t i);
DBPFX_API int dbpfx_editor_set_key(dbpfx_editor* ed, uint32_t i, uint32_t type, uint32_t group, uint64_t instance);
DBPFX_API int dbpfx_editor_set_compression(dbpfx_editor* ed, uint32_t i, int compress);
DBPFX_API int dbpfx_editor_set_user_version(dbpfx_editor* ed, uint32_t major, uint32_t minor);
DBPFX_API int64_t dbpfx_editor_data_size(const dbpfx_editor* ed, uint32_t i);
DBPFX_API int dbpfx_editor_read(const dbpfx_editor* ed, uint32_t i, void* dst, uint64_t len);
DBPFX_API int dbpfx_editor_save_as(const dbpfx_editor* ed, const char* path);
DBPFX_API int dbpfx_editor_save_in_place(dbpfx_editor* ed);
/* Allocates *out with dbpfx_free-able memory. */
DBPFX_API int dbpfx_editor_save_memory(const dbpfx_editor* ed, uint8_t** out, uint64_t* len);

/* ---- codecs */
DBPFX_API int dbpfx_decompress(const void* in, uint64_t in_len, void* out, uint64_t out_len);
/* kind = DBPFX_COMPRESS_REFPACK or _ZLIB. Returns 1 and allocates *out if smaller, 0 if not worth it. */
DBPFX_API int dbpfx_compress(int kind, const void* in, uint64_t in_len, uint8_t** out, uint64_t* out_len);
DBPFX_API void dbpfx_free(void* p);

#ifdef __cplusplus
}
#endif
#endif
