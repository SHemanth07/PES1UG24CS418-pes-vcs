#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/sha.h>

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) sprintf(hex_out + i*2, "%02x", id->hash[i]);
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i*2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(id_out->hash, &ctx);
}

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

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str;
    if      (type == OBJ_BLOB)   type_str = "blob";
    else if (type == OBJ_TREE)   type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);

    size_t total_len = (size_t)header_len + 1 + len;
    uint8_t *full_obj = malloc(total_len);
    if (!full_obj) return -1;

    memcpy(full_obj, header, header_len);
    full_obj[header_len] = '\0';
    memcpy(full_obj + header_len + 1, data, len);

    ObjectID id;
    compute_hash(full_obj, total_len, &id);
    *id_out = id;

    if (object_exists(&id)) { free(full_obj); return 0; }

    mkdir(PES_DIR,     0755);
    mkdir(OBJECTS_DIR, 0755);

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&id, hex);
    char shard_dir[512];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(shard_dir, 0755);

    char final_path[512];
    object_path(&id, final_path, sizeof(final_path));
    char tmp_path[520];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", final_path);

    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { free(full_obj); return -1; }

    ssize_t written = write(fd, full_obj, total_len);
    free(full_obj);
    if (written != (ssize_t)total_len) { close(fd); return -1; }

    fsync(fd);
    close(fd);

    if (rename(tmp_path, final_path) != 0) return -1;

    int dir_fd = open(shard_dir, O_RDONLY);
    if (dir_fd >= 0) { fsync(dir_fd); close(dir_fd); }

    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < 0) { fclose(f); return -1; }

    uint8_t *buf = malloc((size_t)file_size);
    if (!buf) { fclose(f); return -1; }

    if ((long)fread(buf, 1, (size_t)file_size, f) != file_size) {
        free(buf); fclose(f); return -1;
    }
    fclose(f);

    ObjectID computed;
    compute_hash(buf, (size_t)file_size, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(buf); return -1;
    }

    uint8_t *null_pos = memchr(buf, '\0', (size_t)file_size);
    if (!null_pos) { free(buf); return -1; }

    if      (strncmp((char*)buf, "blob ",   5) == 0) *type_out = OBJ_BLOB;
    else if (strncmp((char*)buf, "tree ",   5) == 0) *type_out = OBJ_TREE;
    else if (strncmp((char*)buf, "commit ", 7) == 0) *type_out = OBJ_COMMIT;
    else { free(buf); return -1; }

    size_t data_offset = (size_t)(null_pos - buf) + 1;
    size_t data_len    = (size_t)file_size - data_offset;

    uint8_t *out = malloc(data_len + 1);
    if (!out) { free(buf); return -1; }
    memcpy(out, buf + data_offset, data_len);
    out[data_len] = '\0';

    free(buf);
    *data_out = out;
    *len_out  = data_len;
    return 0;
}
