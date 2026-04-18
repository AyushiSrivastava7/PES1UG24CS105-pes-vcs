#include "commit.h"
#include "index.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// external object system
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);

// ─────────────────────────────────────────────
// COMMIT CREATE (FINAL CLEAN VERSION)
// ─────────────────────────────────────────────

int commit_create(const char *message, ObjectID *commit_id_out) {

    if (!message || strlen(message) == 0) {
        printf("[ERROR] empty commit message\n");
        return -1;
    }

    printf("[DEBUG] commit start\n");

    Commit c;
    memset(&c, 0, sizeof(c));

    // 1. BUILD TREE FROM INDEX
    if (tree_from_index(&c.tree) != 0) {
        printf("[ERROR] tree_from_index failed\n");
        return -1;
    }

    // 2. READ HEAD (PARENT)
    ObjectID parent;
    if (head_read(&parent) == 0) {
        c.parent = parent;
        c.has_parent = 1;
    } else {
        c.has_parent = 0;
    }

    // 3. AUTHOR
    const char *author = pes_author();
    if (!author) {
        printf("[ERROR] pes_author missing\n");
        return -1;
    }

    snprintf(c.author, sizeof(c.author), "%s", author);
    c.timestamp = (uint64_t)time(NULL);

    // 4. MESSAGE
    snprintf(c.message, sizeof(c.message), "%s", message);

    // 5. SERIALIZE
    void *data = NULL;
    size_t len = 0;

    if (commit_serialize(&c, &data, &len) != 0) {
        printf("[ERROR] serialize failed\n");
        return -1;
    }

    // 6. WRITE OBJECT
    ObjectID new_id;
    if (object_write(OBJ_COMMIT, data, len, &new_id) != 0) {
        printf("[ERROR] object_write failed\n");
        free(data);
        return -1;
    }

    free(data);

    // 7. UPDATE HEAD
    if (head_update(&new_id) != 0) {
        printf("[ERROR] head_update failed\n");
        return -1;
    }

    if (commit_id_out) {
        *commit_id_out = new_id;
    }

    printf("[DEBUG] commit success\n");
    return 0;
}
