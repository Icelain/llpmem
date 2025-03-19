#ifndef PMEM_LOCK_FREE_LIST_H
#define PMEM_LOCK_FREE_LIST_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct node {
    int value;
    _Atomic(struct node *) next;
} PmemNode;

PmemNode *createPmemNode(int value);

static inline PmemNode *getNextPtrPmem(PmemNode *node);

static inline bool isMarkedPmem(PmemNode *node);

static inline PmemNode *getMarkedPtrPmem(PmemNode *node);

void insertValuePmem(PmemNode *head, int value);

PmemNode *findPmemNode(PmemNode *head, int value);

bool deleteValuePmem(PmemNode *head, int value);

int removeMarkedPmemNodes(PmemNode *head);

void cleanupListPmem(PmemNode *head);

void traversePmemNode(PmemNode *head);

PMEMobjpool *getPmemObjectPool(const char *path);
void closePmemObjectPool(PMEMobjpool *pool);

#endif /* PMEM_LOCK_FREE_LIST_H */