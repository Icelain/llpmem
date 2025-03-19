#ifndef LOCK_FREE_LIST_H
#define LOCK_FREE_LIST_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

// Node structure with atomic next pointer
typedef struct node {
    int value;
    _Atomic(struct node *) next;
} Node;

// Create a new node
Node *createNode(int value);

// Insert a value at the end of the list (lock-free)
void insertValue(Node *head, int value);

// Find a node with the given value
Node *findNode(Node *head, int value);

// Delete a node with the given value (uses logical deletion followed by
// physical)
bool deleteValue(Node *head, int value);

// Clean up helper function to free all nodes including marked ones
void cleanupList(Node *head);

// Traverse and print the list, skipping marked nodes
void traverseNode(Node *head);

// Helper functions for manipulating marked pointers
static inline Node *getNextPtr(Node *node);
static inline bool isMarked(Node *node);
static inline Node *getMarkedPtr(Node *node);

#endif /* LOCK_FREE_LIST_H */