#ifndef OBJECT_H
#define OBJECT_H

#include <stddef.h>
#include <stdint.h>

#define HASH_SIZE 32

// Represents a SHA-256 hash
typedef struct {
    uint8_t hash[HASH_SIZE];
} ObjectID;

// Convert binary hash → hex string (64 chars + null)
void hash_to_hex(const ObjectID *id, char *hex_out);

// Convert hex string → binary hash
void hex_to_hash(const char *hex, ObjectID *id);

// Write object to store (.pes/objects)
// type = "blob" or "tree"
// Returns 0 on success, -1 on error
int object_write(const char *type,
                 const void *data,
                 size_t size,
                 ObjectID *out);

#endif
