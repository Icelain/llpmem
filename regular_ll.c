#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Node structure with atomic next pointer
typedef struct node {
    int value;
    _Atomic(struct node *) next;
} Node;

Node *createNode(int value) {
    Node *res = (Node *)malloc(sizeof(Node));
    if (res == NULL) {
        perror("Failed to allocate memory for node");
        exit(EXIT_FAILURE);
    }
    res->value = value;
    atomic_store(&res->next, NULL);
    return res;
}

// get the next pointer without any mark bits
static inline Node *getNextPtr(Node *node) {
    return (Node *)((uintptr_t)atomic_load(&node->next) & ~0x1);
}

// check if node is marked for deletion
static inline bool isMarked(Node *node) {
    return (uintptr_t)atomic_load(&node->next) & 0x1;
}

// create a marked pointer
static inline Node *getMarkedPtr(Node *node) {
    return (Node *)((uintptr_t)node | 0x1);
}

// Insert a value at the end of the list (lock-free)
void insertValue(Node *head, int value) {
    Node *newNode = createNode(value);
    Node *prev, *curr;

    while (true) {
        // Find the last node
        prev = head;
        curr = getNextPtr(prev);

        while (curr != NULL) {
            if (isMarked(prev)) {
                break; // Skip marked nodes
            }
            prev = curr;
            curr = getNextPtr(curr);
        }

        if (isMarked(prev)) {
            continue; // Restart if we ended up on a marked node
        }

        // Try to append the node
        if (atomic_compare_exchange_strong(&prev->next, &curr, newNode)) {
            return; // Success
        }
        // If CAS failed, retry the entire operation
    }
}

// Find a node with the given value
Node *findNode(Node *head, int value) {
    Node *current = head;

    while (current != NULL) {
        if (current->value == value && !isMarked(current)) {
            return current;
        }
        current = getNextPtr(current);
    }

    return NULL;
}

// Delete a node with the given value (uses logical deletion followed by
// physical)
bool deleteValue(Node *head, int value) {
    Node *prev, *curr, *next;
    bool marked = false;

    while (true) {
        // Find the node
        prev = head;
        curr = getNextPtr(prev);

        while (curr != NULL && (curr->value != value || isMarked(curr))) {
            prev = curr;
            curr = getNextPtr(curr);
        }

        if (curr == NULL) {
            return false; // Value not found
        }

        // Mark the node for deletion (logical deletion)
        next = getNextPtr(curr);
        marked = false;

        if (!atomic_compare_exchange_strong(&curr->next, &next,
                                            getMarkedPtr(next))) {
            continue; // Failed to mark, retry
        }

        // Try to remove the node physically
        if (atomic_compare_exchange_strong(&prev->next, &curr, next)) {
            // Physical deletion succeeded, free memory
            // Note: In a real system with memory reclamation, you would use
            // a safe memory reclamation technique here
            free(curr);
        } else {
            // Physical deletion failed, will be handled by a future operation
            // This is okay in a lock-free algorithm
        }

        return true;
    }
}

// Clean up helper function to free all nodes including marked ones
void cleanupList(Node *head) {
    Node *current = head;
    while (current != NULL) {
        Node *next = getNextPtr(current);
        free(current);
        current = next;
    }
}

// Traverse and print the list, skipping marked nodes
void traverseNode(Node *head) {
    if (head == NULL) {
        printf("Empty list\n");
        return;
    }

    Node *curr = head;
    while (curr != NULL) {
        if (!isMarked(curr)) {
            printf("{%d}", curr->value);

            Node *next = getNextPtr(curr);
            if (next != NULL && !isMarked(next)) {
                printf("->");
            }
        }
        curr = getNextPtr(curr);
    }
    printf("\n");
}
