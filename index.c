// index.c — Staging area implementation

#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// external object API
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─────────────────────────────────────────────────────────────
// PROVIDED FUNCTIONS (UNCHANGED)
// ─────────────────────────────────────────────────────────────
// (keep your index_find, index_remove, index_status as-is)

// ─────────────────────────────────────────────────────────────
// LOAD INDEX
// ─────────────────────────────────────────────────────────────

int index_load(Index *index) {

    FILE *f = fopen(INDEX_FILE, "r");
    index->count = 0;

    if (!f)
        return 0; // empty index is valid

    char line[1024];

    while (fgets(line, sizeof(line), f)) {

        if (index->count >= MAX_INDEX_ENTRIES)
            break;

        IndexEntry *e = &index->entries[index->count];
        char hash_hex[HASH_HEX_SIZE + 1];

        if (sscanf(line, "%o %64s %lu %u %s",
                   &e->mode,
                   hash_hex,
                   &e->mtime_sec,
                   &e->size,
                   e->path) != 5) {
            continue;
        }

        hex_to_hash(hash_hex, &e->hash);
        index->count++;
    }

    fclose(f);
    return 0;
}

// ─────────────────────────────────────────────────────────────
// SAVE INDEX (ATOMIC)
// ─────────────────────────────────────────────────────────────

int index_save(const Index *index) {

    FILE *f = fopen(INDEX_FILE, "w");
    if (!f)
        return -1;

    for (int i = 0; i < index->count; i++) {

        char hash_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&index->entries[i].hash, hash_hex);

        fprintf(f, "%o %s %lu %u %s\n",
                index->entries[i].mode,
                hash_hex,
                index->entries[i].mtime_sec,
                index->entries[i].size,
                index->entries[i].path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return 0;
}

// ─────────────────────────────────────────────────────────────
// ADD FILE TO INDEX (STAGING)
// ─────────────────────────────────────────────────────────────

int index_add(Index *index, const char *path) {

    // 1. Load existing index
    if (index_load(index) != 0)
        return -1;

    // 2. Open file
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    struct stat st;
    if (stat(path, &st) != 0) {
        fclose(f);
        return -1;
    }

    // 3. Read file content
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *buf = malloc(size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    if (fread(buf, 1, size, f) != size) {
        free(buf);
        fclose(f);
        return -1;
    }

    fclose(f);

    // 4. Write blob object
    ObjectID id;
    if (object_write(OBJ_BLOB, buf, size, &id) != 0) {
        free(buf);
        return -1;
    }

    free(buf);

    // 5. Update or add entry
    IndexEntry *e = index_find(index, path);

    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES)
            return -1;

        e = &index->entries[index->count++];
    }

    e->mode = get_file_mode(path);
    e->hash = id;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;

    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';

    // 6. Save index
    return index_save(index);
}
