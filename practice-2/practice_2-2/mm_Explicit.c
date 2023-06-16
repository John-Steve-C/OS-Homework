/*
 * This is the Implicit Idle Linked-list, I write it by the code from CSAPP
 * Also there are some bugs in CSAPP's code, like
 * - CHUNKSIZE is too large
 * - size in extend_heap need to be the multiple of 8 Bytes,
 *   but the parameter we send into the function is the multiple(DSIZE),
 *   so we needn't extend too much space.
 *
 * And I only use first-fit to scan free blocks
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))

/* Basic constants and macros */
#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define BSIZE 16 /* empty block size (HEADER+prev+succ+FOOTER)*/
#define CHUNKSIZE (1<<8) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y)? (x) : (y))
#define MIN(x, y) ((x) < (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *) (p))
#define SET(p, val) (*(unsigned int *) (p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)     // *p is 32-bit, the high 28 bit saved the block size
#define GET_ALLOC(p) (GET(p) & 0x1)     // = 1 means this block has been allocated

// below macros are different from those in Implicit linklist, because there are (pred and succ) after HEADER

/* Given block ptr bp, compute address of its header and footer */
#define HEADER(bp) ((char *) (bp) - WSIZE - DSIZE)
#define FOOTER(bp) ((char *) (bp) - DSIZE + GET_SIZE(HEADER(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLOCK(bp) ((char *) (bp) + GET_SIZE(HEADER(bp)))
#define PREV_BLOCK(bp) ((char *) (bp) - GET_SIZE(((char *) (bp) - DSIZE - DSIZE)))

// used for explicit linklist
// succ & prev is the distance from heap_list
// read succ address = succ + heap_list
// in linux64, sizeof(long) = 8 = sizeof(void*)
#define GET_NEXT_FREE_BLOCK(bp) (GET((char *) (bp) - WSIZE)                                 \
                                    ? ((int *) ((long) (GET((char *) (bp) - WSIZE)) +       \
                                                (long) (heap_list)))                        \
                                    : NULL)
// read prev address
#define GET_PREV_FREE_BLOCK(bp) (GET((char *) (bp) - DSIZE)                                 \
                                    ? ((int *) ((long) (GET((char *) (bp) - DSIZE)) +       \
                                                (long) (heap_list)))                        \
                                    : NULL)
// write val to next block
#define SET_NEXT_FREE_BLOCK(bp, val) ( val \
                                        ? (SET(((char *) (bp) - WSIZE), (val - (long) (heap_list)))) \
                                        : (SET(((char *) (bp) - WSIZE), (val))) )
#define SET_PREV_FREE_BLOCK(bp, val) ( val \
                                        ? (SET(((char *) (bp) - DSIZE), (val - (long) (heap_list)))) \
                                        : (SET(((char *) (bp) - DSIZE), (val))) )

// choose which kind of fit
#define FIRST_FIT

// point at the pos after the prologue block (size = 4 * WSIZE)
char *heap_list;
char *last_fit_pos; // used for next-fit
char *free_list;    // the head of free block linked-list
// LIFO (last_in, first_out)

static void remove_free_block(void *bp) {
    if (bp == NULL || GET_ALLOC(HEADER(bp))) return;

    void *prev_free_block = GET_PREV_FREE_BLOCK(bp);
    void *next_free_block = GET_NEXT_FREE_BLOCK(bp);

    if (!prev_free_block && !next_free_block) free_list = NULL;
    else if (!prev_free_block && next_free_block) {
        free_list = next_free_block;
        SET_PREV_FREE_BLOCK(next_free_block, 0);
    } else if (prev_free_block && !next_free_block) {
        SET_NEXT_FREE_BLOCK(prev_free_block, 0);
    } else {
        SET_NEXT_FREE_BLOCK(prev_free_block, (long) next_free_block);
        SET_PREV_FREE_BLOCK(next_free_block, (long) prev_free_block);
    }

   mm_checkheap(1);
}

// insert before head
static void insert_free_block(void *bp) {
    if (bp == NULL || GET_ALLOC(HEADER(bp))) return;

    if (free_list == NULL) {    // first insert
        free_list = bp;
        SET_PREV_FREE_BLOCK(bp, 0);
        SET_NEXT_FREE_BLOCK(bp, 0);
        return;
    }

    SET_PREV_FREE_BLOCK(bp, 0);
    SET_NEXT_FREE_BLOCK(bp, (long) free_list);
    SET_PREV_FREE_BLOCK(free_list, (long) bp);
    free_list = bp;
}

static void *merge_block(void *bp) {
    size_t prev_alloc = GET_ALLOC(FOOTER(PREV_BLOCK(bp)));
    size_t next_alloc = GET_ALLOC(HEADER(NEXT_BLOCK(bp)));
    size_t size = GET_SIZE(HEADER(bp));

    if (prev_alloc && next_alloc) {}
    else if (prev_alloc && !next_alloc) {
        remove_free_block(NEXT_BLOCK(bp));

        size += GET_SIZE(HEADER(NEXT_BLOCK(bp)));
        SET(HEADER(bp), PACK(size, 0));
        SET(FOOTER(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        remove_free_block(PREV_BLOCK(bp));

        size += GET_SIZE(HEADER(PREV_BLOCK(bp)));
        SET(FOOTER(bp), PACK(size, 0));
        SET(HEADER(PREV_BLOCK(bp)), PACK(size, 0));
        bp = PREV_BLOCK(bp);
    } else {
        remove_free_block(NEXT_BLOCK(bp));
        remove_free_block(PREV_BLOCK(bp));

        size += GET_SIZE(HEADER(PREV_BLOCK(bp))) + GET_SIZE(FOOTER(NEXT_BLOCK(bp)));
        SET(HEADER(PREV_BLOCK(bp)), PACK(size, 0));
        SET(FOOTER(NEXT_BLOCK(bp)), PACK(size, 0));
        bp = PREV_BLOCK(bp);
    }

    insert_free_block(bp);
    return bp;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size = words;

    // Allocate an even number of words to maintain alignment
//    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    // words is the multiple of 8(DSIZE)
    if ((long) (bp = mem_sbrk(size)) == -1) return NULL;

    // Initialize free block header/footer and the epilogue header
    SET(HEADER(bp), PACK(size, 0));
    SET(FOOTER(bp), PACK(size, 0));

    SET_PREV_FREE_BLOCK(bp, 0);
    SET_NEXT_FREE_BLOCK(bp, 0);

    SET(HEADER(NEXT_BLOCK(bp)), PACK(0, 1));

    return merge_block(bp);
}

static void *find_fit(size_t size) {

#ifdef FIRST_FIT
    // first-fit search
    for (void *bp = free_list; bp != NULL; bp = GET_NEXT_FREE_BLOCK(bp)) {
        if (size <= GET_SIZE(HEADER(bp))) return bp;
    }
    return NULL;
#endif

}

static void place(void *bp, size_t size) {
    size_t csize = GET_SIZE(HEADER(bp));
    remove_free_block(bp);

    if ((csize - size) >= (2 * BSIZE)) {
        // split
        SET(HEADER(bp), PACK(size, 1));
        SET(FOOTER(bp), PACK(size, 1));

        bp = NEXT_BLOCK(bp);
        SET(HEADER(bp), PACK(csize - size, 0));
        SET(FOOTER(bp), PACK(csize - size, 0));
        SET_PREV_FREE_BLOCK(bp, 0);
        SET_NEXT_FREE_BLOCK(bp, 0);
        merge_block(bp);    // merge after split will improve performance
    } else {
        SET(HEADER(bp), PACK(csize, 1));
        SET(FOOTER(bp), PACK(csize, 1));
    }

   mm_checkheap(1);
}

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void) {
    // Create the initial empty heap
    if ((heap_list = mem_sbrk(8 * WSIZE)) == (void *) -1) return -1;

    SET(heap_list, 0);                                        // Alignment padding
    SET(heap_list + (WSIZE * 1), PACK(BSIZE, 1));     // Prologue header
    SET(heap_list + (WSIZE * 2), 0);                      // Prologue prev
    SET(heap_list + (WSIZE * 3), 0);                      // Prologue succ
    SET(heap_list + (WSIZE * 4), PACK(BSIZE, 1));    // Prologue footer
    SET(heap_list + (WSIZE * 5), PACK(0, 1));   // Epilogue header
    SET(heap_list + (WSIZE * 6), 0);                      // Epilogue prev
    SET(heap_list + (WSIZE * 7), 0);                      // Epilogue succ

    heap_list += (BSIZE);   // prologue
    free_list = NULL;

    // Extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE) == NULL) return -1;
    return 0;
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
void *malloc(size_t size) {
    size_t asize, extend_size;  // asize: adjusted block size
    char *bp;

    if (size == 0) return NULL;

    // Adjust block size to include overhead and alignment reqs
    // obviously it's the multiple of (BSIZE)
//    if (size <= DSIZE) asize = BSIZE;
//    else asize = BSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    asize = DSIZE * ((size - 1 + DSIZE) / DSIZE) + BSIZE;


    // Search the free list for a fit
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    } else {
        // No fit found. Get more memory and place the block
        extend_size = MAX(asize, CHUNKSIZE);
        if ((bp = extend_heap(extend_size)) == NULL) return NULL;
        place(bp, asize);
        return bp;
    }
}

/*
 * free - change the allocation bit into 0 of HEADER & FOOTER
 *        and remember to merge the block
 *        Also clean the prev & succ free block pos
 */
void free(void *ptr) {
    if (ptr == NULL) return;
    size_t size = GET_SIZE(HEADER(ptr));
    SET(HEADER(ptr), PACK(size, 0));
    SET(FOOTER(ptr), PACK(size, 0));
    SET_PREV_FREE_BLOCK(ptr, 0);
    SET_NEXT_FREE_BLOCK(ptr, 0);
    merge_block(ptr);

   mm_checkheap(1);
}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.  I'm too lazy
 *      to do better.
 */
void *realloc(void *oldptr, size_t size) {
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0) {
        free(oldptr);
        return NULL;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if (oldptr == NULL) {
        return malloc(size);
    }

    newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if (!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = GET_SIZE(HEADER(oldptr));
    if (size < oldsize) oldsize = size;
    memcpy(newptr, oldptr, oldsize);

    /* Free the old block. */
    free(oldptr);

    return newptr;
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc(size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *newptr;

    newptr = malloc(bytes);
    memset(newptr, 0, bytes);

    return newptr;
}

/*
 * mm_checkheap - There are no bugs in my code, so I don't need to check,
 *      so nah!
 */
void mm_checkheap(int verbose) {
    /*Get gcc to be quiet. */
    verbose = verbose;

    char *ptr;

    // check epilogue and prologue blocks
    if (GET_SIZE(HEADER(heap_list)) != BSIZE ||
        GET_ALLOC(HEADER(heap_list)) != 1 ||
        GET_SIZE(FOOTER(heap_list)) != BSIZE || GET_ALLOC(FOOTER(heap_list)) != 1)
        printf("Prologue block error\n");

    ptr = heap_list;
    while (GET_SIZE(HEADER(ptr)) != 0) {
        ptr = NEXT_BLOCK(ptr);
    }
    if (GET_SIZE(HEADER(ptr)) != 0 || GET_ALLOC(HEADER(ptr)) != 1)
        printf("Epilogue block error\n");

    // check the boundary of heap
    if (mem_heap_lo() + BSIZE != heap_list) {
        printf("mem_heap_lo: %p, heap_head: %p\n", mem_heap_lo(), heap_list);
        printf("Heap boundary error\n");
    }
    if (mem_heap_hi() + 1 != (void *) ptr) {
        printf("mem_heap_hi: %p, heap_end: %p\n", mem_heap_hi(), ptr);
        printf("Heap boundary error\n");
    }

    // check the header and footer of each block
    ptr = heap_list;
    while (GET_SIZE(HEADER(ptr)) != 0) {
        // check the consistency of prev and next pointers
        if (PREV_BLOCK(NEXT_BLOCK(ptr)) != ptr) {
            printf("Prev and next pointers error at %p\n", ptr);
        }

        // check the consistency of header and footer
        if (GET_SIZE(HEADER(ptr)) != GET_SIZE(FOOTER(ptr))) {
            printf("Header and footer size error at %p\n", ptr);
        } else if (GET_ALLOC(HEADER(ptr)) != GET_ALLOC(FOOTER(ptr)))
            printf("Header and footer alloc error\n");

        // address alignment
        if ((unsigned long long) ptr % DSIZE != 0)
            printf("Block alignment error\n");

        // check the continuous of heap
        if (ptr + GET_SIZE(HEADER(ptr)) != NEXT_BLOCK(ptr))
            printf("Block continuous error 1\n");
        if (ptr != heap_list) {
            if (FOOTER(PREV_BLOCK(ptr)) != ptr - BSIZE)
                printf("Block continuous error 2\n");
        }

        ptr = NEXT_BLOCK(ptr);
    }

    // check merge
    int cnt = 0;
    ptr = heap_list;
    while (GET_SIZE(HEADER(ptr)) != 0) {
        if (GET_ALLOC(HEADER(ptr)) == 0 && GET_ALLOC(HEADER(NEXT_BLOCK(ptr))) == 0)
            printf("Merge error at block %p\n", ptr);
        ptr = NEXT_BLOCK(ptr);
        cnt++;
    }

    // check the free list
    ptr = free_list;
    while (ptr != NULL) {
        if (!((char *) mem_heap_lo() < ptr && ptr < (char *) mem_heap_hi()))
            printf("Free list boundary error\n");

        if (GET_ALLOC(HEADER(ptr)) != 0)
            printf("Allocated block in the free list at %p\n", ptr);

        if (GET_PREV_FREE_BLOCK(ptr) != NULL &&
            (char *) GET_NEXT_FREE_BLOCK(GET_PREV_FREE_BLOCK(ptr)) != ptr)
            printf("Prev and next pointer error at %p\n", ptr);

        void *tmp = heap_list;
        while (GET_SIZE(HEADER(ptr)) != 0) {
            if (tmp == ptr)
                break;
            tmp = NEXT_BLOCK(tmp);
        }
        if (GET_SIZE(HEADER(tmp)) == 0)
            printf("Block in free list is not in the heap\n");
        ptr = (char *) GET_NEXT_FREE_BLOCK(ptr);
    }
}
