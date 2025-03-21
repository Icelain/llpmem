
// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2025, Persistent Memory Example */
/*
 * persistent_lockfree_list.c - example of persistent lock-free linked list
 */
#include "pmemobj_list.h"
#include <ex_common.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

POBJ_LAYOUT_BEGIN(list);
POBJ_LAYOUT_ROOT(list, struct list_root);
POBJ_LAYOUT_TOID(list, struct list_node);
POBJ_LAYOUT_END(list);

struct list_node {
    int value;
    _Atomic(TOID(struct list_node)) next;
};

struct list_root {
    TOID(struct list_node) head;
};

// Get next pointer without the marked bit
static inline TOID(struct list_node) getNextPtr(TOID(struct list_node) node) {
    TOID(struct list_node) next = atomic_load(&D_RW(node)->next);
    return TOID_ASSIGN(struct list_node, ((uintptr_t)next.oid.off & ~0x1));
}

// Check if the given node is marked for deletion
static inline bool isMarked(TOID(struct list_node) node) {
    TOID(struct list_node) next = atomic_load(&D_RW(node)->next);
    return (uintptr_t)next.oid.off & 0x1;
}

// Get a marked pointer for the given node
static inline TOID(struct list_node) getMarkedPtr(TOID(struct list_node) node) {
    PMEMoid oid = node.oid;
    oid.off = (uintptr_t)oid.off | 0x1;
    return TOID_ASSIGN(struct list_node, oid);
}

// Create a new node in persistent memory
TOID(struct list_node) createPersistentNode(PMEMobjpool *pop, int value) {
    TOID(struct list_node) node;

    TX_BEGIN(pop) {
        node = TX_NEW(struct list_node);
        TX_ADD_DIRECT(&D_RW(node)->value);
        D_RW(node)->value = value;
        TX_ADD_DIRECT(&D_RW(node)->next);
        TOID_ASSIGN(D_RW(node)->next, OID_NULL);
    }
    TX_ONABORT {
        fprintf(stderr, "Transaction aborted when creating node\n");
        abort();
    }
    TX_END

    return node;
}

// Insert a value at the end of the list
void insertValue(PMEMobjpool *pop, TOID(struct list_root) root, int value) {
    TOID(struct list_node) newNode = createPersistentNode(pop, value);
    TOID(struct list_node) prev, curr;

    while (true) {
        prev = D_RW(root)->head;
        if (TOID_IS_NULL(prev)) {
            // Empty list, try to set the head
            TX_BEGIN(pop) {
                TX_ADD_FIELD(root, head);
                if (TOID_IS_NULL(D_RO(root)->head)) {
                    D_RW(root)->head = newNode;
                    TX_COMMIT;
                    return;
                }
            }
            TX_END
            continue;
        }

        curr = getNextPtr(prev);
        while (!TOID_IS_NULL(curr)) {
            if (isMarked(prev)) {
                break;
            }
            prev = curr;
            curr = getNextPtr(curr);
        }

        if (isMarked(prev)) {
            continue;
        }

        // Try to append the new node
        TX_BEGIN(pop) {
            TX_ADD_FIELD(prev, next);
            TOID(struct list_node) expected = curr;
            if (atomic_compare_exchange_strong(&D_RW(prev)->next, &expected,
                                               newNode)) {
                TX_COMMIT;
                return;
            }
        }
        TX_END
    }
}

// Find a node with the specified value
TOID(struct list_node) findNode(TOID(struct list_root) root, int value) {
    TOID(struct list_node) current = D_RO(root)->head;

    while (!TOID_IS_NULL(current)) {
        if (D_RO(current)->value == value && !isMarked(current)) {
            return current;
        }
        current = getNextPtr(current);
    }

    return TOID_NULL(struct list_node);
}

// Mark a node for deletion (logical delete)
bool markNodeForDeletion(PMEMobjpool *pop, TOID(struct list_root) root,
                         int value) {
    TOID(struct list_node) curr, next;

    while (true) {
        curr = D_RO(root)->head;

        while (!TOID_IS_NULL(curr) &&
               (D_RO(curr)->value != value || isMarked(curr))) {
            curr = getNextPtr(curr);
        }

        if (TOID_IS_NULL(curr)) {
            return false; // Value not found
        }

        next = getNextPtr(curr);

        // Mark the node for deletion
        TX_BEGIN(pop) {
            TX_ADD_FIELD(curr, next);
            TOID(struct list_node) expected = next;
            if (atomic_compare_exchange_strong(&D_RW(curr)->next, &expected,
                                               getMarkedPtr(next))) {
                TX_COMMIT;
                return true;
            }
        }
        TX_END
    }
}

// Physically remove marked nodes and reclaim memory
int removeMarkedNodes(PMEMobjpool *pop, TOID(struct list_root) root) {
    int removed_count = 0;
    TOID(struct list_node) prev, curr, next;

    while (true) {
        bool retry = false;
        prev = D_RO(root)->head;

        // Handle special case of head node marked for deletion
        if (!TOID_IS_NULL(prev) && isMarked(prev)) {
            curr = getNextPtr(prev);
            TX_BEGIN(pop) {
                TX_ADD_FIELD(root, head);
                TX_FREE(prev);
                D_RW(root)->head = curr;
                removed_count++;
            }
            TX_ONABORT { retry = true; }
            TX_END

            if (retry) {
                continue;
            }
            prev = D_RO(root)->head;
        }

        if (TOID_IS_NULL(prev)) {
            break; // Empty list
        }

        curr = getNextPtr(prev);
        while (!TOID_IS_NULL(curr)) {
            next = getNextPtr(curr);

            if (isMarked(curr)) {
                TX_BEGIN(pop) {
                    TX_ADD_FIELD(prev, next);
                    TOID(struct list_node) expected = curr;
                    if (!atomic_compare_exchange_strong(&D_RW(prev)->next,
                                                        &expected, next)) {
                        retry = true;
                        TX_ABORT(EINVAL);
                    }
                    TX_FREE(curr);
                    removed_count++;
                }
                TX_ONABORT {
                    retry = true;
                    break;
                }
                TX_END

                if (retry) {
                    break;
                }
                curr = next;
            } else {
                prev = curr;
                curr = next;
            }
        }

        if (!retry) {
            break;
        }
    }

    return removed_count;
}

// Traverse and print the list (only unmarked nodes)
void traverseList(TOID(struct list_root) root) {
    TOID(struct list_node) curr = D_RO(root)->head;

    if (TOID_IS_NULL(curr)) {
        printf("Empty list\n");
        return;
    }

    while (!TOID_IS_NULL(curr)) {
        if (!isMarked(curr)) {
            printf("{%d}", D_RO(curr)->value);
            TOID(struct list_node) next = getNextPtr(curr);
            if (!TOID_IS_NULL(next) && !isMarked(next)) {
                printf("->");
            }
        }
        curr = getNextPtr(curr);
    }
    printf("\n");
}

// Clean up all nodes in the list
void cleanupList(PMEMobjpool *pop, TOID(struct list_root) root) {
    TOID(struct list_node) current = D_RO(root)->head;
    TOID(struct list_node) next;

    TX_BEGIN(pop) {
        while (!TOID_IS_NULL(current)) {
            next = getNextPtr(current);
            TX_FREE(current);
            current = next;
        }
        TX_SET(root, head, TOID_NULL(struct list_node));
    }
    TX_ONABORT {
        fprintf(stderr, "Transaction aborted during cleanup\n");
        abort();
    }
    TX_END
}

static void print_help(void) {
    printf("usage: persistent_lockfree_list <pool> <option> [<value>]\n");
    printf("\tAvailable options:\n");
    printf("\tinsert <value> - Insert integer value into the list\n");
    printf("\tdelete <value> - Mark node with value for deletion\n");
    printf("\tcleanup - Remove all marked nodes\n");
    printf("\tfind <value> - Find value in the list\n");
    printf("\tprint - Print all unmarked values in the list\n");
    printf("\tclear - Remove all nodes from the list\n");
}

int main(int argc, const char *argv[]) {
    PMEMobjpool *pop;
    const char *path;

    if (argc < 3) {
        print_help();
        return 0;
    }

    path = argv[1];

    // Create or open the persistent memory pool
    if (file_exists(path) != 0) {
        if ((pop = pmemobj_create(path, POBJ_LAYOUT_NAME(list),
                                  PMEMOBJ_MIN_POOL, 0666)) == NULL) {
            perror("failed to create pool\n");
            return -1;
        }
    } else {
        if ((pop = pmemobj_open(path, POBJ_LAYOUT_NAME(list))) == NULL) {
            perror("failed to open pool\n");
            return -1;
        }
    }

    TOID(struct list_root) root = POBJ_ROOT(pop, struct list_root);

    if (strcmp(argv[2], "insert") == 0) {
        if (argc == 4) {
            int value = atoi(argv[3]);
            insertValue(pop, root, value);
            printf("Inserted value %d into the list\n", value);
        } else {
            print_help();
        }
    } else if (strcmp(argv[2], "delete") == 0) {
        if (argc == 4) {
            int value = atoi(argv[3]);
            if (markNodeForDeletion(pop, root, value)) {
                printf("Marked value %d for deletion\n", value);
            } else {
                printf("Value %d not found in the list\n", value);
            }
        } else {
            print_help();
        }
    } else if (strcmp(argv[2], "cleanup") == 0) {
        int count = removeMarkedNodes(pop, root);
        printf("Removed %d marked nodes from the list\n", count);
    } else if (strcmp(argv[2], "find") == 0) {
        if (argc == 4) {
            int value = atoi(argv[3]);
            TOID(struct list_node) node = findNode(root, value);
            if (!TOID_IS_NULL(node)) {
                printf("Found value %d in the list\n", value);
            } else {
                printf("Value %d not found in the list\n", value);
            }
        } else {
            print_help();
        }
    } else if (strcmp(argv[2], "print") == 0) {
        printf("List contents: ");
        traverseList(root);
    } else if (strcmp(argv[2], "clear") == 0) {
        cleanupList(pop, root);
        printf("List cleared\n");
    } else {
        print_help();
    }

    pmemobj_close(pop);
    return 0;
}