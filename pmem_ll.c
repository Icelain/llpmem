#include <libpmemobj.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

POBJ_LAYOUT_BEGIN(list_layout);
POBJ_LAYOUT_ROOT(list_layout, struct list_root);
POBJ_LAYOUT_TOID(list_layout, struct node);
POBJ_LAYOUT_END(list_layout);

struct list_root {
    TOID(struct node) head;
};

struct node {
    int value;
    _Atomic TOID(struct node) next;
};

// Helper function to check if a file exists
bool file_exists(const char *path) {
    if (access(path, F_OK) == 0) {
        return true;
    }
    return false;
}

PMEMobjpool *get_pmem_pool(const char *path, size_t pool_size) {
    PMEMobjpool *pop;

    if (!file_exists(path)) {
        // Create new pool
        pop = pmemobj_create(path, POBJ_LAYOUT_NAME(list_layout), pool_size,
                             0666);
        if (pop == NULL) {
            perror("pmemobj_create");
            return NULL;
        }
    } else {
        // Open existing pool
        pop = pmemobj_open(path, POBJ_LAYOUT_NAME(list_layout));
        if (pop == NULL) {
            perror("pmemobj_open");
            return NULL;
        }
    }

    return pop;
}

TOID(struct node) create_node(PMEMobjpool *pop, int value) {
    TOID(struct node) node_oid;
    PMEMoid raw_oid;

    // Allocate the node in persistent memory
    int ret = pmemobj_zalloc(pop, &raw_oid, sizeof(struct node),
                             TOID_TYPE_NUM(struct node));
    if (ret) {
        perror("pmemobj_zalloc");
        return TOID_NULL(struct node);
    }

    // Convert to typed OID
    TOID_ASSIGN(node_oid, raw_oid);

    // Initialize the node
    D_RW(node_oid)->value = value;
    atomic_store(&D_RW(node_oid)->next, TOID_NULL(struct node));

    // Persist changes to make sure they are durable
    pmemobj_persist(pop, D_RW(node_oid), sizeof(struct node));

    return node_oid;
}

// Helper to get the actual pointer (clear mark bit)
static inline TOID(struct node) get_unmarked_next(TOID(struct node) node_oid) {
    if (TOID_IS_NULL(node_oid))
        return TOID_NULL(struct node);

    // Get the raw OID and clear the lowest bit
    PMEMoid raw_oid = *(PMEMoid *)&D_RO(node_oid)->next;
    raw_oid.off &= ~(uintptr_t)0x1;

    // Convert back to typed OID
    TOID(struct node) result;
    TOID_ASSIGN(result, raw_oid);
    return result;
}

// Check if node is marked for deletion
static inline bool is_marked(TOID(struct node) node_oid) {
    if (TOID_IS_NULL(node_oid))
        return false;

    PMEMoid raw_oid = *(PMEMoid *)&D_RO(node_oid)->next;
    return (raw_oid.off & 0x1) == 0x1;
}

// Mark a node pointer
static inline TOID(struct node) get_marked_next(TOID(struct node) node_oid) {
    if (TOID_IS_NULL(node_oid))
        return TOID_NULL(struct node);

    // Get the raw OID and set the lowest bit
    PMEMoid raw_oid = *(PMEMoid *)&node_oid;
    raw_oid.off |= 0x1;

    // Convert back to typed OID
    TOID(struct node) result;
    TOID_ASSIGN(result, raw_oid);
    return result;
}

void insert_value(PMEMobjpool *pop, TOID(struct list_root) root, int value) {
    TOID(struct node) new_node = create_node(pop, value);
    TOID(struct node) prev, curr;

    while (true) {
        prev = D_RO(root)->head;
        curr = get_unmarked_next(prev);

        while (!TOID_IS_NULL(curr)) {
            if (is_marked(prev)) {
                break;
            }
            prev = curr;
            curr = get_unmarked_next(curr);
        }

        if (is_marked(prev)) {
            continue;
        }

        TOID(struct node) expected = TOID_NULL(struct node);
        atomic_store(&D_RW(new_node)->next, curr);
        pmemobj_persist(pop, &D_RW(new_node)->next, sizeof(TOID(struct node)));

        if (atomic_compare_exchange_strong(&D_RW(prev)->next, &expected,
                                           new_node)) {
            pmemobj_persist(pop, &D_RW(prev)->next, sizeof(TOID(struct node)));
            return;
        }
    }
}

TOID(struct node) find_node(TOID(struct list_root) root, int value) {
    TOID(struct node) current = D_RO(root)->head;

    while (!TOID_IS_NULL(current)) {
        if (D_RO(current)->value == value && !is_marked(current)) {
            return current;
        }
        current = get_unmarked_next(current);
    }

    return TOID_NULL(struct node);
}

bool delete_value(PMEMobjpool *pop, TOID(struct list_root) root, int value) {
    TOID(struct node) curr, next;

    while (true) {
        curr = D_RO(root)->head;

        while (!TOID_IS_NULL(curr) &&
               (D_RO(curr)->value != value || is_marked(curr))) {
            curr = get_unmarked_next(curr);
        }

        if (TOID_IS_NULL(curr)) {
            return false;
        }

        next = get_unmarked_next(curr);

        // Mark the node for deletion using CAS
        if (atomic_compare_exchange_strong(&D_RW(curr)->next, &next,
                                           get_marked_next(next))) {
            pmemobj_persist(pop, &D_RW(curr)->next, sizeof(TOID(struct node)));
            return true;
        }
    }
}

/* Remove nodes marked for deletion */
int remove_marked_nodes(PMEMobjpool *pop, TOID(struct list_root) root) {
    int removed_count = 0;
    TOID(struct node) prev, curr, next;

    while (true) {
        bool retry = false;
        prev = D_RO(root)->head;
        curr = get_unmarked_next(prev);

        while (!TOID_IS_NULL(curr)) {
            next = get_unmarked_next(curr);

            if (is_marked(curr)) {
                // Try to physically remove the marked node
                if (!atomic_compare_exchange_strong(&D_RW(prev)->next, &curr,
                                                    next)) {
                    retry = true;
                    break;
                }

                pmemobj_persist(pop, &D_RW(prev)->next,
                                sizeof(TOID(struct node)));
                pmemobj_free(&D_RW(curr).oid);
                removed_count++;
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

void traverse_list(TOID(struct list_root) root) {
    TOID(struct node) curr = D_RO(root)->head;

    if (TOID_IS_NULL(curr)) {
        printf("Empty list\n");
        return;
    }

    while (!TOID_IS_NULL(curr)) {
        if (!is_marked(curr)) {
            printf("{%d}", D_RO(curr)->value);

            TOID(struct node) next = get_unmarked_next(curr);
            if (!TOID_IS_NULL(next) && !is_marked(next)) {
                printf("->");
            }
        }
        curr = get_unmarked_next(curr);
    }
    printf("\n");
}

void initialize_list(PMEMobjpool *pop, TOID(struct list_root) root) {
    D_RW(root)->head = TOID_NULL(struct node);
    pmemobj_persist(pop, D_RW(root), sizeof(struct list_root));
}

void cleanup_list(PMEMobjpool *pop, TOID(struct list_root) root) {
    TOID(struct node) current = D_RO(root)->head;
    TOID(struct node) next;

    while (!TOID_IS_NULL(current)) {
        next = get_unmarked_next(current);
        pmemobj_free(&D_RW(current).oid);
        current = next;
    }

    D_RW(root)->head = TOID_NULL(struct node);
    pmemobj_persist(pop, D_RW(root), sizeof(struct list_root));
}
