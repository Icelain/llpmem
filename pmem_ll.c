#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

bool file_exists(const char *path) {
    FILE *file = fopen(path, "r");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}

PMEMobjpool *getPmemObjectPool(const char *path) {

    PMEMobjpool *pop;

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

    return pop;
}

void closePmemObjectPool(PMEMobjpool *pool) { pmemobj_close(pool); }

typedef struct node {
    int value;
    _Atomic(struct node *) next;
} PmemNode;

PmemNode *createPmemNode(int value) {
    PmemNode *res = (PmemNode *)malloc(sizeof(PmemNode));
    if (res == NULL) {
        perror("Failed to allocate memory for node");
        exit(EXIT_FAILURE);
    }
    res->value = value;
    atomic_store(&res->next, NULL);
    return res;
}

static inline PmemNode *getNextPtrPmem(PmemNode *node) {
    return (PmemNode *)((uintptr_t)atomic_load(&node->next) & ~0x1);
}

static inline bool isMarkedPmem(PmemNode *node) {
    return (uintptr_t)atomic_load(&node->next) & 0x1;
}

static inline PmemNode *getMarkedPtrPmem(PmemNode *node) {
    return (PmemNode *)((uintptr_t)node | 0x1);
}

void insertValuePmem(PmemNode *head, int value) {
    PmemNode *newNode = createPmemNode(value);
    PmemNode *prev, *curr;

    while (true) {
        prev = head;
        curr = getNextPtrPmem(prev);

        while (curr != NULL) {
            if (isMarkedPmem(prev)) {
                break;
            }
            prev = curr;
            curr = getNextPtrPmem(curr);
        }

        if (isMarkedPmem(prev)) {
            continue;
        }

        if (atomic_compare_exchange_strong(&prev->next, &curr, newNode)) {
            return;
        }
    }
}

PmemNode *findPmemNode(PmemNode *head, int value) {
    PmemNode *current = head;

    while (current != NULL) {
        if (current->value == value && !isMarkedPmem(current)) {
            return current;
        }
        current = getNextPtrPmem(current);
    }

    return NULL;
}

bool deleteValuePmem(PmemNode *head, int value) {
    PmemNode *curr, *next;

    while (true) {
        curr = head;

        while (curr != NULL && (curr->value != value || isMarkedPmem(curr))) {
            curr = getNextPtrPmem(curr);
        }

        if (curr == NULL) {
            return false;
        }

        next = getNextPtrPmem(curr);

        if (atomic_compare_exchange_strong(&curr->next, &next,
                                           getMarkedPtrPmem(next))) {
            return true;
        }
    }
}

int removeMarkedPmemNodes(PmemNode *head) {
    int removed_count = 0;
    PmemNode *prev, *curr, *next;

    while (true) {
        bool retry = false;
        prev = head;
        curr = getNextPtrPmem(prev);

        while (curr != NULL) {
            next = getNextPtrPmem(curr);

            if (isMarkedPmem(curr)) {
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

void cleanupListPmem(PmemNode *head) {
    PmemNode *current = head;
    while (current != NULL) {
        PmemNode *next = getNextPtrPmem(current);
        free(current);
        current = next;
    }
}

void traversePmemNode(PmemNode *head) {
    if (head == NULL) {
        printf("Empty list\n");
        return;
    }

    PmemNode *curr = head;
    while (curr != NULL) {
        if (!isMarkedPmem(curr)) {
            printf("{%d}", curr->value);

            PmemNode *next = getNextPtrPmem(curr);
            if (next != NULL && !isMarkedPmem(next)) {
                printf("->");
            }
        }
        curr = getNextPtrPmem(curr);
    }
    printf("\n");
}