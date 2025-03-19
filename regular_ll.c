#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

// get next ptr without the marked node
static inline Node *getNextPtr(Node *node) {
    return (Node *)((uintptr_t)atomic_load(&node->next) & ~0x1);
}

// check if the given node is marked
static inline bool isMarked(Node *node) {
    return (uintptr_t)atomic_load(&node->next) & 0x1;
}

// get a marked pointer for the given node
static inline Node *getMarkedPtr(Node *node) {
    return (Node *)((uintptr_t)node | 0x1);
}

void insertValue(Node *head, int value) {
    Node *newNode = createNode(value);
    Node *prev, *curr;

    while (true) {
        prev = head;
        curr = getNextPtr(prev);

        while (curr != NULL) {
            if (isMarked(prev)) {
                break;
            }
            prev = curr;
            curr = getNextPtr(curr);
        }

        if (isMarked(prev)) {
            continue;
        }

        if (atomic_compare_exchange_strong(&prev->next, &curr, newNode)) {
            return;
        }
    }
}

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

bool deleteValue(Node *head, int value) {
    Node *curr, *next;

    while (true) {
        curr = head;

        while (curr != NULL && (curr->value != value || isMarked(curr))) {
            curr = getNextPtr(curr);
        }

        if (curr == NULL) {
            return false;
        }

        next = getNextPtr(curr);

        if (atomic_compare_exchange_strong(&curr->next, &next,
                                           getMarkedPtr(next))) {
            return true;
        }
    }
}

int removeMarkedNodes(Node *head) {
    int removed_count = 0;
    Node *prev, *curr, *next;

    while (true) {
        bool retry = false;
        prev = head;
        curr = getNextPtr(prev);

        while (curr != NULL) {
            next = getNextPtr(curr);

            if (isMarked(curr)) {
                if (!atomic_compare_exchange_strong(&prev->next, &curr, next)) {
                    retry = true;
                    break;
                }

                free(curr);
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

void cleanupList(Node *head) {
    Node *current = head;
    while (current != NULL) {
        Node *next = getNextPtr(current);
        free(current);
        current = next;
    }
}

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