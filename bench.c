#include "pmem_ll.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <pool_file>\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    PMEMobjpool *pop = get_pmem_pool(path, PMEMOBJ_MIN_POOL);

    if (pop == NULL) {
        fprintf(stderr, "Error: could not create or open pool\n");
        return 1;
    }

    TOID(struct list_root) root = POBJ_ROOT(pop, struct list_root);

    // Check if this is a fresh pool and initialize if needed
    if (!file_exists(path)) {
        initialize_list(pop, root);
        printf("Created new empty list\n");
    } else {
        printf("Opened existing list\n");
    }

    printf("Inserting values: 10, 20, 30\n");
    insert_value(pop, root, 10);
    insert_value(pop, root, 20);
    insert_value(pop, root, 30);

    printf("List contents: ");
    traverse_list(root);

    printf("Deleting value: 20\n");
    delete_value(pop, root, 20);

    printf("List after logical deletion: ");
    traverse_list(root);

    printf("Removing marked nodes\n");
    int removed = remove_marked_nodes(pop, root);
    printf("Removed %d nodes\n", removed);

    printf("List after physical deletion: ");
    traverse_list(root);

    pmemobj_close(pop);
    printf("Pool closed\n");

    return 0;
}