/**
 * persistent_list.h
 *
 * Header file for persistent linked list implementation using libpmemobj
 */

#ifndef PERSISTENT_LIST_H
#define PERSISTENT_LIST_H

#include <libpmemobj.h>
#include <stdbool.h>

/* Define the layout for the persistent memory pool */
POBJ_LAYOUT_BEGIN(list_layout);
POBJ_LAYOUT_ROOT(list_layout, struct list_root);
POBJ_LAYOUT_TOID(list_layout, struct node);
POBJ_LAYOUT_END(list_layout);

/* Root structure holding the list head */
struct list_root {
    TOID(struct node) head;
};

/* Node structure for the linked list */
struct node {
    int value;
    _Atomic TOID(struct node) next;
};

/**
 * Check if a file exists
 *
 * @param path The path to check
 * @return true if the file exists, false otherwise
 */
bool file_exists(const char *path);

/**
 * Get or create a persistent memory pool
 *
 * @param path The path to the pool file
 * @param pool_size The size of the pool to create if it doesn't exist
 * @return Pointer to the memory pool, or NULL on error
 */
PMEMobjpool *get_pmem_pool(const char *path, size_t pool_size);

/**
 * Create a new node in persistent memory
 *
 * @param pop The persistent memory pool
 * @param value The value to store in the node
 * @return The node object ID
 */
TOID(struct node) create_node(PMEMobjpool *pop, int value);

/**
 * Insert a value into the list
 *
 * @param pop The persistent memory pool
 * @param root The root object of the list
 * @param value The value to insert
 */
void insert_value(PMEMobjpool *pop, TOID(struct list_root) root, int value);

/**
 * Find a node with the given value
 *
 * @param root The root object of the list
 * @param value The value to find
 * @return The node object ID, or TOID_NULL if not found
 */
TOID(struct node) find_node(TOID(struct list_root) root, int value);

/**
 * Delete a value from the list (logical deletion)
 *
 * @param pop The persistent memory pool
 * @param root The root object of the list
 * @param value The value to delete
 * @return true if the value was found and marked for deletion, false otherwise
 */
bool delete_value(PMEMobjpool *pop, TOID(struct list_root) root, int value);

/**
 * Remove nodes marked for deletion (physical deletion)
 *
 * @param pop The persistent memory pool
 * @param root The root object of the list
 * @return The number of nodes physically removed
 */
int remove_marked_nodes(PMEMobjpool *pop, TOID(struct list_root) root);

/**
 * Traverse and print the list
 *
 * @param root The root object of the list
 */
void traverse_list(TOID(struct list_root) root);

/**
 * Initialize a new empty list
 *
 * @param pop The persistent memory pool
 * @param root The root object of the list
 */
void initialize_list(PMEMobjpool *pop, TOID(struct list_root) root);

/**
 * Clean up the entire list
 *
 * @param pop The persistent memory pool
 * @param root The root object of the list
 */
void cleanup_list(PMEMobjpool *pop, TOID(struct list_root) root);

/* Helper functions for node pointer manipulation */

/**
 * Get the next node pointer with mark bit cleared
 *
 * @param node_oid The node object ID
 * @return The next node object ID with mark bit cleared
 */
static inline TOID(struct node) get_unmarked_next(TOID(struct node) node_oid);

/**
 * Check if a node is marked for deletion
 *
 * @param node_oid The node object ID
 * @return true if the node is marked for deletion, false otherwise
 */
static inline bool is_marked(TOID(struct node) node_oid);

/**
 * Get a marked version of a node pointer
 *
 * @param node_oid The node object ID
 * @return The node object ID with mark bit set
 */
static inline TOID(struct node) get_marked_next(TOID(struct node) node_oid);

#endif /* PERSISTENT_LIST_H */