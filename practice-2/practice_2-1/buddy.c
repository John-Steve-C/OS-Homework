#include "buddy.h"
#include <stdlib.h>
#include <string.h>

#define NULL ((void *)0)
#define MAXRANK (16)

struct block {
    struct block *next, *pre;
    void *addr;
    int rank, used; // saved rank = real rank -1
    // because page size = 4096 * 2^(rank-1)
};

// 1 rank = 4K = 4096 Byte

struct block *free_area[MAXRANK];    // linked list
int free_cnt[MAXRANK], pageCnt;
void *start_addr, *end_addr;

struct block **page_to_block;    // map
// 内存中的一个 page 作为 block 的起始时，对应的 block

static void add_block(void *p, int rank) {
    struct block *new_block = malloc(sizeof(struct block)), *blk = free_area[rank];

    while (blk->next != NULL) blk = blk->next;
    blk->next = new_block;

    new_block->rank = rank;
    new_block->addr = p;
    new_block->pre = blk;
    new_block->next = NULL;
    new_block->used = 0;

    free_cnt[rank]++;
    page_to_block[(p - start_addr) / 4096] = new_block; // block start page
}

static void remove_block(struct block *blk) {
    if (blk->pre != NULL) blk->pre->next = blk->next;
    if (blk->next != NULL) blk->next->pre = blk->pre;

    free_cnt[blk->rank]--;
    page_to_block[(blk->addr - start_addr) / 4096] = NULL;
    free(blk);
}

__attribute((destructor)) void free_All() { // 析构函数
    for (int i = 0; i < MAXRANK; i++) {
        struct block *las = NULL;
        for (struct block *now = free_area[i]; now != NULL; now = now->next) {
            if (las != NULL) free(las);
            las = now;
        }
        if (las != NULL) free(las);
    }

    free(page_to_block);
}

int init_page(void *p, int pgcount) {
    pageCnt = pgcount;
    start_addr = p;
    end_addr = p + pgcount * 4096;

    page_to_block = malloc(pgcount * sizeof(struct block *));
    for (int i = 0; i < pgcount; ++i) page_to_block[i] = NULL;
    for (int i = 0; i < MAXRANK; ++i) {
        free_cnt[i] = 0;
        // head_block, no info
        free_area[i] = malloc(sizeof(struct block));
        free_area[i]->pre = free_area[i]->next = NULL;
        free_area[i]->rank = i;
        free_area[i]->addr = NULL;
        free_area[i]->used = 0;
    }

    // 二进制拆分, pgcount = 2^10 + 2^8 + ...
    // 页面大小为 4K * 2^(rank-1)
    for (int r = MAXRANK - 1; r >= 0; --r) {
        int cnt = pgcount / (1 << r);
//        free_cnt[i] = cnt;
        pgcount %= (1 << r);
        for (int j = 0; j < cnt; ++j) {
            add_block(p, r);
            p += (1 << r) * 4096;
        }
    }

    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAXRANK) return -EINVAL;
    rank--;
    int target_rank;
    struct block *target_block = NULL;
    for (int r = rank; r < MAXRANK; ++r) {
        if (!free_cnt[r]) continue;
        for (struct block *b = free_area[r]->next; b != NULL; b = b->next) {
            if (!b->used) {
                target_block = b;
                target_rank = r;
                break;
            }
        }
        break;
    }
    if (!target_block) return -ENOSPC;

    if (target_rank > rank) {
        // split target_block
        void *addr = target_block->addr;
        remove_block(target_block);
        for (int r = target_rank - 1; r >= rank; --r) add_block(addr + (1 << r) * 4096, r);
        // this block is allocated in fact
        add_block(addr, rank);
        page_to_block[(addr - start_addr) / 4096]->used = 1;
        free_cnt[rank]--;
        return addr;
    } else {
        // normal allocation
        target_block->used = 1;
        free_cnt[target_rank]--;
        return target_block->addr;
    }
}

int return_pages(void *p) {
    if (p < start_addr || p > end_addr || (p - start_addr) % 4096) return -EINVAL;
    int pos = (p - start_addr) / 4096;
    if (page_to_block[pos] == NULL) return -EINVAL;   // 该 page 不是 block 的起始，即未被分配

    // free target block
    int rank = page_to_block[pos]->rank;
    page_to_block[pos]->used = 0;
    free_cnt[rank]++;

    // 循环的合并过程
    while (1) {
        // find buddy: same rank and continuous address
        void *p_buddy;  // p's neighbor (需要按顺序两两分组)
        if ((p - start_addr) % (4096 * (1 << (rank + 1)))) p_buddy = p - 4096 * (1 << rank);    // left
        else p_buddy = p + 4096 * (1 << rank);  // right
        // check if the neighbor is real buddy
        int pos_buddy = (p_buddy - start_addr) / 4096;
        if (pos_buddy >= pageCnt || page_to_block[pos_buddy] == NULL || page_to_block[pos_buddy]->rank != rank ||
            page_to_block[pos_buddy]->used)
            break;    // p don't have buddy
        else {
            // remove p and buddy
            remove_block(page_to_block[pos_buddy]);
            remove_block(page_to_block[(p - start_addr) / 4096]);
        }
        // 合并 p & buddy 到上一级
        if (p > p_buddy) p = p_buddy;   // assure p be the start page of the block
        add_block(p, rank + 1);
        rank++;
    }

    return OK;
}

int query_ranks(void *p) {
    if (p < start_addr || p > end_addr || (p - start_addr) % 4096) return -EINVAL;
    return page_to_block[(p - start_addr) / 4096]->rank + 1;
    // 未被分配的页面按照其最大的 rank 进行查询 ?
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAXRANK) return -EINVAL;
    rank--;
    int cnt = 0;
    for (struct block *b = free_area[rank]->next; b != NULL; b = b->next) {
        if (!b->used) cnt++;
    }
    return cnt;
}
