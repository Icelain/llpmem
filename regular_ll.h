#ifndef LOCK_FREE_LIST_H
#define LOCK_FREE_LIST_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct node {
    int value;
    _Atomic(struct node *) next;
} Node;

Node *createNode(int value);

static inline Node *getNextPtr(Node *node);

static inline bool isMarked(Node *node);

static inline Node *getMarkedPtr(Node *node);

void insertValue(Node *head, int value);

Node *findNode(Node *head, int value);

bool deleteValue(Node *head, int value);

int removeMarkedNodes(Node *head);

void cleanupList(Node *head);

void traverseNode(Node *head);

#endif /* LOCK_FREE_LIST_H */