// commit.h — Commit object interface
//
// A commit ties together a tree snapshot, parent history, author info,
// and a human-readable message.

#ifndef COMMIT_H
#define COMMIT_H

#include "pes.h"

typedef struct {
    ObjectID tree;          // Root tree hash (the project snapshot)
    ObjectID parent;        // Parent commit hash
    int has_parent;         // 0 for the initial commit, 1 otherwise
    char author[256];       // Author string (from PES_AUTHOR env var)
    uint64_t timestamp;     // Unix timestamp of commit creation
    char message[4096];     // Commit message
} Commit;

// Create a commit from the current index.
//   1. Build a tree from the index (using tree_from_index)
//   2. Read current HEAD as the parent (may not exist for first commit)
//   3. Create the commit object and write it to the object store
//   4. Update HEAD/branch ref to point to the new commit
// Returns 0 on success, -1 on error.
int commit_create(const char *message, ObjectID *commit_id_out);

// Parse raw commit object data into a Commit struct.
int commit_parse(const void *data, size_t len, Commit *commit_out);

// Serialize a Commit struct into raw bytes for object_write(OBJ_COMMIT, ...).
// Caller must free(*data_out).
int commit_serialize(const Commit *commit, void **data_out, size_t *len_out);

// Walk commit history starting from HEAD, following parent pointers.
// Calls `callback` for each commit, from newest to oldest.
// Stops at the root commit (no parent).
typedef void (*commit_walk_fn)(const ObjectID *id, const Commit *commit, void *ctx);
int commit_walk(commit_walk_fn callback, void *ctx);

// ─── HEAD helpers ───────────────────────────────────────────────────────────

// Read the commit hash that HEAD currently points to.
// Follows symbolic refs: if HEAD contains "ref: refs/heads/main",
// reads .pes/refs/heads/main to get the actual commit hash.
// Returns 0 on success, -1 if no commits yet (empty repository).
int head_read(ObjectID *id_out);

// Update HEAD (or the branch it points to) to a new commit hash.
// Must use atomic write (temp file + rename).
int head_update(const ObjectID *new_commit);
int commit_create(const char *message, ObjectID *commit_id_out) {
    if (!message || strlen(message) == 0) return -1;

    Commit c;
    memset(&c, 0, sizeof(c));

    // ── 1. Build tree from index ─────────────────────────────
    if (tree_from_index(&c.tree) != 0) {
        return -1;
    }

    // ── 2. Read parent commit (HEAD) ─────────────────────────
    ObjectID parent;
    if (head_read(&parent) == 0) {
        c.parent = parent;
        c.has_parent = 1;
    } else {
        c.has_parent = 0;
    }

    // ── 3. Author + timestamp ────────────────────────────────
    const char *author = pes_author();
    if (!author) return -1;

    snprintf(c.author, sizeof(c.author), "%s", author);
    c.timestamp = (uint64_t)time(NULL);

    // ── 4. Message ────────────────────────────────────────────
    snprintf(c.message, sizeof(c.message), "%s", message);

    // ── 5. Serialize commit ───────────────────────────────────
    void *data = NULL;
    size_t len = 0;

    if (commit_serialize(&c, &data, &len) != 0) {
        return -1;
    }

    // ── 6. Write object to store ─────────────────────────────
    ObjectID new_id;
    if (object_write(OBJ_COMMIT, data, len, &new_id) != 0) {
        free(data);
        return -1;
    }

    free(data);

    // ── 7. Update HEAD atomically ────────────────────────────
    if (head_update(&new_id) != 0) {
        return -1;
    }

    // ── 8. Return result ─────────────────────────────────────
    if (commit_id_out) {
        *commit_id_out = new_id;
    }
    printf("1\n");
if (tree_from_index(&c.tree) != 0) {
    printf("FAILED: tree_from_index\n");
    return -1;
}

printf("2\n");
ObjectID parent;
if (head_read(&parent) != 0) {
    printf("FAILED: head_read\n");
    return -1;
}

printf("3\n");
const char *author = pes_author();
if (!author) {
    printf("FAILED: pes_author\n");
    return -1;
}

printf("4\n");
void *data = NULL;
size_t len = 0;
if (commit_serialize(&c, &data, &len) != 0) {
    printf("FAILED: serialize\n");
    return -1;
}

printf("5\n");
ObjectID new_id;
if (object_write(OBJ_COMMIT, data, len, &new_id) != 0) {
    printf("FAILED: object_write\n");
    return -1;
}

printf("6\n");
if (head_update(&new_id) != 0) {
    printf("FAILED: head_update\n");
    return -1;
}

printf("SUCCESS\n");

    return 0;
}
#endif // COMMIT_H
