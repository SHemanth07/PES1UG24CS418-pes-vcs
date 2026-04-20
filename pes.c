#include "pes.h"
#include "index.h"
#include "commit.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

// ─── Forward declarations ────────────────────────────────────────────────────
// (Implemented in object.c / commit.c / index.c / tree.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── cmd_init ────────────────────────────────────────────────────────────────

void cmd_init(void) {
    // Create .pes directory structure
    const char *dirs[] = {
        PES_DIR,
        OBJECTS_DIR,
        REFS_DIR,
        NULL
    };

    for (int i = 0; dirs[i]; i++) {
        if (mkdir(dirs[i], 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "error: failed to create %s: %s\n",
                    dirs[i], strerror(errno));
            return;
        }
    }

    // Write HEAD pointing to main branch
    FILE *f = fopen(HEAD_FILE, "w");
    if (!f) {
        fprintf(stderr, "error: failed to create HEAD\n");
        return;
    }
    fprintf(f, "ref: refs/heads/main\n");
    fclose(f);

    printf("Initialized empty PES repository in %s/\n", PES_DIR);
}

// ─── cmd_add ─────────────────────────────────────────────────────────────────

void cmd_add(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: pes add <file>...\n");
        return;
    }

    Index index;
    if (index_load(&index) != 0) {
        fprintf(stderr, "error: failed to load index\n");
        return;
    }

    for (int i = 2; i < argc; i++) {
        if (index_add(&index, argv[i]) != 0) {
            fprintf(stderr, "error: failed to add '%s'\n", argv[i]);
        }
    }
}

// ─── cmd_status ──────────────────────────────────────────────────────────────

void cmd_status(void) {
    Index index;
    if (index_load(&index) != 0) {
        fprintf(stderr, "error: failed to load index\n");
        return;
    }
    index_status(&index);
}

// ─── cmd_log callback ────────────────────────────────────────────────────────

static void log_callback(const ObjectID *id, const Commit *commit, void *ctx) {
    (void)ctx;
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);

    printf("commit %s\n", hex);
    printf("Author: %s\n", commit->author);
    printf("Date:   %lu\n", (unsigned long)commit->timestamp);
    printf("\n    %s\n\n", commit->message);
}

void cmd_log(void) {
    if (commit_walk(log_callback, NULL) != 0) {
        fprintf(stderr, "error: no commits yet (or failed to read history)\n");
    }
}

// ─── cmd_commit ──────────────────────────────────────────────────────────────
// Parse -m <message> and create a commit.

void cmd_commit(int argc, char *argv[]) {
    const char *message = NULL;

    // Look for -m flag
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "-m") == 0) {
            message = argv[i + 1];
            break;
        }
    }

    if (!message) {
        fprintf(stderr, "error: commit requires a message (-m \"message\")\n");
        return;
    }

    ObjectID commit_id;
    if (commit_create(message, &commit_id) != 0) {
        fprintf(stderr, "error: commit failed\n");
        return;
    }

    // Print first 12 hex chars of the commit hash
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&commit_id, hex);
    hex[12] = '\0'; // truncate to 12 chars

    printf("Committed: %s... %s\n", hex, message);
}

// ─── PROVIDED: Phase 5 Command Wrappers ─────────────────────────────────────

// Stubs for branch/checkout — implement in Phase 5 if required.
// These are declared as weak so the linker won't fail if they exist elsewhere.

__attribute__((weak)) void branch_list(void) {
    printf("* main\n");
}

__attribute__((weak)) int branch_create(const char *name) {
    (void)name;
    fprintf(stderr, "error: branch_create not yet implemented\n");
    return -1;
}

__attribute__((weak)) int branch_delete(const char *name) {
    (void)name;
    fprintf(stderr, "error: branch_delete not yet implemented\n");
    return -1;
}

__attribute__((weak)) int checkout(const char *target) {
    (void)target;
    fprintf(stderr, "error: checkout not yet implemented\n");
    return -1;
}

void cmd_branch(int argc, char *argv[]) {
    if (argc == 2) {
        branch_list();
    } else if (argc == 3) {
        if (branch_create(argv[2]) == 0) {
            printf("Created branch '%s'\n", argv[2]);
        } else {
            fprintf(stderr, "error: failed to create branch '%s'\n", argv[2]);
        }
    } else if (argc == 4 && strcmp(argv[2], "-d") == 0) {
        if (branch_delete(argv[3]) == 0) {
            printf("Deleted branch '%s'\n", argv[3]);
        } else {
            fprintf(stderr, "error: failed to delete branch '%s'\n", argv[3]);
        }
    } else {
        fprintf(stderr, "Usage:\n pes branch\n pes branch <name>\n pes branch -d <name>\n");
    }
}

void cmd_checkout(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: pes checkout <branch_or_commit>\n");
        return;
    }
    const char *target = argv[2];
    if (checkout(target) == 0) {
        printf("Switched to '%s'\n", target);
    } else {
        fprintf(stderr, "error: checkout failed. Do you have uncommitted changes?\n");
    }
}

// ─── Command dispatch ────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: pes <command> [args]\n");
        fprintf(stderr, "\nCommands:\n");
        fprintf(stderr, "  init              Create a new PES repository\n");
        fprintf(stderr, "  add <file>...     Stage files for commit\n");
        fprintf(stderr, "  status            Show working directory status\n");
        fprintf(stderr, "  commit -m <msg>   Create a commit from staged files\n");
        fprintf(stderr, "  log               Show commit history\n");
        fprintf(stderr, "  branch            List, create, or delete branches\n");
        fprintf(stderr, "  checkout <ref>    Switch branches or restore working tree\n");
        return 1;
    }

    const char *cmd = argv[1];

    if      (strcmp(cmd, "init")     == 0) cmd_init();
    else if (strcmp(cmd, "add")      == 0) cmd_add(argc, argv);
    else if (strcmp(cmd, "status")   == 0) cmd_status();
    else if (strcmp(cmd, "commit")   == 0) cmd_commit(argc, argv);
    else if (strcmp(cmd, "log")      == 0) cmd_log();
    else if (strcmp(cmd, "branch")   == 0) cmd_branch(argc, argv);
    else if (strcmp(cmd, "checkout") == 0) cmd_checkout(argc, argv);
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        fprintf(stderr, "Run 'pes' with no arguments for usage.\n");
        return 1;
    }

    return 0;
}
