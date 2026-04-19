// object.c — Content-addressable object store
/
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions:     object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

// Get the filesystem path where an object should be stored.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── FIXED: type_to_str ─────────────────────────────────────────────────────

static const char* type_to_str(ObjectType type) {
    switch (type) {
        case OBJ_BLOB: return "blob";
        case OBJ_TREE: return "tree";
        case OBJ_COMMIT: return "commit";
        default: return "unknown";
    }
}

// ─── TODO IMPLEMENTED ───────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {

    const char *type_str = type_to_str(type);

    // 1. Build header: "type size\0"
    char header[128];
    int header_len = sprintf(header, "%s %zu", type_str, len);
    header[header_len] = '\0';
    header_len++;

    // 2. Build full object
    size_t total_size = header_len + len;
    char *full = malloc(total_size);
    if (!full) return -1;

    memcpy(full, header, header_len);
    memcpy(full + header_len, data, len);

    // 3. Compute hash
    ObjectID id;
    compute_hash(full, total_size, &id);

    // 4. Deduplication
    if (object_exists(&id)) {
        if (id_out) *id_out = id;
        free(full);
        return 0;
    }

    // 5. Build path
    char path[512];
    object_path(&id, path, sizeof(path));

    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    *strrchr(dir, '/') = '\0';

    mkdir(PES_DIR, 0755);
    mkdir(OBJECTS_DIR, 0755);

    // FIX: create shard directory properly (recursive safe enough for lab)
    char shard_dir[512];
    snprintf(shard_dir, sizeof(shard_dir), "%s", dir);
    mkdir(shard_dir, 0755);

    // 6. Temp file
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s/tmpXXXXXX", shard_dir);

    int fd = mkstemp(tmp);
    if (fd < 0) {
        free(full);
        return -1;
    }

    // 7. Write
    if (write(fd, full, total_size) != (ssize_t)total_size) {
        close(fd);
        free(full);
        return -1;
    }

    fsync(fd);
    close(fd);

    // 8. Atomic rename
    rename(tmp, path);

    // 9. fsync directory
    int dfd = open(shard_dir, O_RDONLY);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }

    if (id_out) *id_out = id;

    free(full);
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {

    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    if (fread(buf, 1, size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return -1;
    }

    fclose(f);

    // integrity check
    ObjectID check;
    compute_hash(buf, size, &check);

    if (memcmp(check.hash, id->hash, HASH_SIZE) != 0) {
        free(buf);
        return -1;
    }

    char *sep = memchr(buf, '\0', size);
    if (!sep) {
        free(buf);
        return -1;
    }

    size_t header_len = sep - buf;

    if (strncmp(buf, "blob", 4) == 0) *type_out = OBJ_BLOB;
    else if (strncmp(buf, "tree", 4) == 0) *type_out = OBJ_TREE;
    else if (strncmp(buf, "commit", 6) == 0) *type_out = OBJ_COMMIT;
    else {
        free(buf);
        return -1;
    }

    size_t data_len = size - header_len - 1;

    void *data = malloc(data_len);
    if (!data) {
        free(buf);
        return -1;
    }

    memcpy(data, sep + 1, data_len);

    *data_out = data;
    *len_out = data_len;

    free(buf);
    return 0;
}
